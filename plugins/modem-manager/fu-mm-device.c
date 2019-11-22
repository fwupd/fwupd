/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-io-channel.h"
#include "fu-archive.h"
#include "fu-mm-device.h"
#include "fu-device-private.h"
#include "fu-mm-utils.h"
#include "fu-qmi-pdc-updater.h"

/* Amount of time for the modem to boot in fastboot mode. */
#define FU_MM_DEVICE_REMOVE_DELAY_RE_ENUMERATE	20000	/* ms */

/* Amount of time for the modem to be re-probed and exposed in MM after being
 * uninhibited. The timeout is long enough to cover the worst case, where the
 * modem boots without SIM card inserted (and therefore the initialization
 * may be very slow) and also where carrier config switching is explicitly
 * required (e.g. if switching from the default (DF) to generic (GC).*/
#define FU_MM_DEVICE_REMOVE_DELAY_REPROBE	120000	/* ms */

struct _FuMmDevice {
	FuDevice			 parent_instance;
	MMManager			*manager;

	/* ModemManager-based devices will have MMObject and inhibition_uid set,
	 * udev-based ones won't (as device is already inhibited) */
	MMObject			*omodem;
	gchar				*inhibition_uid;

	/* Properties read from the ModemManager-exposed modem, and to be
	 * propagated to plain udev-exposed modem objects. We assume that
	 * the firmware upgrade operation doesn't change the USB layout, and
	 * therefore the USB interface of the modem device that was an
	 * AT-capable TTY is assumed to be the same one after the upgrade.
	 */
	MMModemFirmwareUpdateMethod	 update_methods;
	gchar				*detach_fastboot_at;
	gint				 port_at_ifnum;

	/* fastboot detach handling */
	gchar				*port_at;
	FuIOChannel			*io_channel;

	/* qmi-pdc update logic */
	gchar				*port_qmi;
	FuQmiPdcUpdater			*qmi_pdc_updater;
	GArray				*qmi_pdc_active_id;
	guint				 attach_idle;
};

enum {
	SIGNAL_ATTACH_FINISHED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE (FuMmDevice, fu_mm_device, FU_TYPE_DEVICE)

static void
fu_mm_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuMmDevice *self = FU_MM_DEVICE (device);
	if (self->port_at != NULL)
		fu_common_string_append_kv (str, idt, "AtPort", self->port_at);
	if (self->port_qmi != NULL)
		fu_common_string_append_kv (str, idt, "QmiPort", self->port_qmi);
}

const gchar *
fu_mm_device_get_inhibition_uid (FuMmDevice *device)
{
	g_return_val_if_fail (FU_IS_MM_DEVICE (device), NULL);
	return device->inhibition_uid;
}

MMModemFirmwareUpdateMethod
fu_mm_device_get_update_methods (FuMmDevice *device)
{
	g_return_val_if_fail (FU_IS_MM_DEVICE (device), MM_MODEM_FIRMWARE_UPDATE_METHOD_NONE);
	return device->update_methods;
}

const gchar *
fu_mm_device_get_detach_fastboot_at (FuMmDevice *device)
{
	g_return_val_if_fail (FU_IS_MM_DEVICE (device), NULL);
	return device->detach_fastboot_at;
}

gint
fu_mm_device_get_port_at_ifnum (FuMmDevice *device)
{
	g_return_val_if_fail (FU_IS_MM_DEVICE (device), -1);
	return device->port_at_ifnum;
}

static gboolean
fu_mm_device_probe_default (FuDevice *device, GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE (device);
	MMModemFirmware *modem_fw;
	MMModem *modem = mm_object_peek_modem (self->omodem);
	MMModemPortInfo *ports = NULL;
	const gchar **device_ids;
	const gchar *version;
	guint n_ports = 0;
	g_autoptr(MMFirmwareUpdateSettings) update_settings = NULL;
	g_autofree gchar *device_sysfs_path = NULL;

	/* inhibition uid is the modem interface 'Device' property, which may
	 * be the device sysfs path or a different user-provided id */
	self->inhibition_uid = mm_modem_dup_device (modem);

	/* find out what update methods we should use */
	modem_fw = mm_object_peek_modem_firmware (self->omodem);
	update_settings = mm_modem_firmware_get_update_settings (modem_fw);
	self->update_methods = mm_firmware_update_settings_get_method (update_settings);
	if (self->update_methods == MM_MODEM_FIRMWARE_UPDATE_METHOD_NONE) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "modem cannot be put in programming mode");
		return FALSE;
	}

	/* various fastboot commands */
	if (self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT) {
		const gchar *tmp;
		tmp = mm_firmware_update_settings_get_fastboot_at (update_settings);
		if (tmp == NULL) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "modem does not set fastboot command");
			return FALSE;
		}
		self->detach_fastboot_at = g_strdup (tmp);
	}

	/* get GUIDs */
	device_ids = mm_firmware_update_settings_get_device_ids (update_settings);
	if (device_ids == NULL || device_ids[0] == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "modem did not specify any device IDs");
		return FALSE;
	}

	/* get version string, which is fw_ver+config_ver */
	version = mm_firmware_update_settings_get_version (update_settings);
	if (version == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "modem did not specify a firmware version");
		return FALSE;
	}

	/* look for the AT and QMI/MBIM ports */
	if (!mm_modem_get_ports (modem, &ports, &n_ports)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "failed to get port information");
		return FALSE;
	}
	if (self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT) {
		for (guint i = 0; i < n_ports; i++) {
			if (ports[i].type == MM_MODEM_PORT_TYPE_AT) {
				self->port_at = g_strdup_printf ("/dev/%s", ports[i].name);
				break;
			}
		}
	}
	if (self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_QMI_PDC) {
		for (guint i = 0; i < n_ports; i++) {
			if ((ports[i].type == MM_MODEM_PORT_TYPE_QMI) ||
			    (ports[i].type == MM_MODEM_PORT_TYPE_MBIM)) {
				self->port_qmi = g_strdup_printf ("/dev/%s", ports[i].name);
				break;
			}
		}
	}
	mm_modem_port_info_array_free (ports, n_ports);

	/* an at port is required for fastboot */
	if ((self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT) &&
	    (self->port_at == NULL)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "failed to find AT port");
		return FALSE;
	}

	/* a qmi port is required for qmi-pdc */
	if ((self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_QMI_PDC) &&
	    (self->port_qmi == NULL)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "failed to find QMI port");
		return FALSE;
	}

	/* if we have the at port reported, get sysfs path and interface number */
	if (self->port_at != NULL) {
		fu_mm_utils_get_port_info (self->port_at, &device_sysfs_path, &self->port_at_ifnum, NULL);
	} else if (self->port_qmi != NULL) {
		fu_mm_utils_get_port_info (self->port_qmi, &device_sysfs_path, NULL, NULL);
	} else {
		g_warn_if_reached ();
	}

	/* if no device sysfs file, error out */
	if (device_sysfs_path == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "failed to find device sysfs path");
		return FALSE;
	}

	/* add properties to fwupd device */
	fu_device_set_physical_id (device, device_sysfs_path);
	if (mm_modem_get_manufacturer (modem) != NULL)
		fu_device_set_vendor (device, mm_modem_get_manufacturer (modem));
	if (mm_modem_get_model (modem) != NULL)
		fu_device_set_name (device, mm_modem_get_model (modem));
	fu_device_set_version (device, version, FWUPD_VERSION_FORMAT_PLAIN);
	for (guint i = 0; device_ids[i] != NULL; i++)
		fu_device_add_instance_id (device, device_ids[i]);

	/* convert the instance IDs to GUIDs */
	fu_device_convert_instance_ids (device);

	return TRUE;
}

static gboolean
fu_mm_device_probe_udev (FuDevice *device, GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE (device);

	/* an at port is required for fastboot */
	if ((self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT) &&
	    (self->port_at == NULL)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "failed to find AT port");
		return FALSE;
	}

	/* a qmi port is required for qmi-pdc */
	if ((self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_QMI_PDC) &&
	    (self->port_qmi == NULL)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "failed to find QMI port");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_mm_device_probe (FuDevice *device, GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE (device);

	if (self->omodem) {
		return fu_mm_device_probe_default (device, error);
	} else {
		return fu_mm_device_probe_udev (device, error);
	}
}

static gboolean
fu_mm_device_at_cmd (FuMmDevice *self, const gchar *cmd, GError **error)
{
	const gchar *buf;
	gsize bufsz = 0;
	g_autoptr(GBytes) at_req  = NULL;
	g_autoptr(GBytes) at_res  = NULL;
	g_autofree gchar *cmd_cr = g_strdup_printf ("%s\r\n", cmd);

	/* command */
	at_req = g_bytes_new (cmd_cr, strlen (cmd_cr));
	if (g_getenv ("FWUPD_MODEM_MANAGER_VERBOSE") != NULL)
		fu_common_dump_bytes (G_LOG_DOMAIN, "writing", at_req);
	if (!fu_io_channel_write_bytes (self->io_channel, at_req, 1500,
					FU_IO_CHANNEL_FLAG_FLUSH_INPUT, error)) {
		g_prefix_error (error, "failed to write %s: ", cmd);
		return FALSE;
	}

	/* response */
	at_res = fu_io_channel_read_bytes (self->io_channel, -1, 1500,
					   FU_IO_CHANNEL_FLAG_SINGLE_SHOT, error);
	if (at_res == NULL) {
		g_prefix_error (error, "failed to read response for %s: ", cmd);
		return FALSE;
	}
	if (g_getenv ("FWUPD_MODEM_MANAGER_VERBOSE") != NULL)
		fu_common_dump_bytes (G_LOG_DOMAIN, "read", at_res);
	buf = g_bytes_get_data (at_res, &bufsz);
	if (bufsz < 6) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "failed to read valid response for %s", cmd);
		return FALSE;
	}
	if (memcmp (buf, "\r\nOK\r\n", 6) != 0) {
		g_autofree gchar *tmp = g_strndup (buf + 2, bufsz - 4);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "failed to read valid response for %s: %s",
			     cmd, tmp);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_mm_device_io_open (FuMmDevice *self, GError **error)
{
	/* open device */
	self->io_channel = fu_io_channel_new_file (self->port_at, error);
	if (self->io_channel == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_mm_device_io_close (FuMmDevice *self, GError **error)
{
	if (!fu_io_channel_shutdown (self->io_channel, error))
		return FALSE;
	g_clear_object (&self->io_channel);
	return TRUE;
}

static gboolean
fu_mm_device_detach_fastboot (FuDevice *device, GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE (device);
	g_autoptr(FuDeviceLocker) locker  = NULL;

	/* boot to fastboot mode */
	locker = fu_device_locker_new_full (device,
					    (FuDeviceLockerFunc) fu_mm_device_io_open,
					    (FuDeviceLockerFunc) fu_mm_device_io_close,
					    error);
	if (locker == NULL)
		return FALSE;
	if (!fu_mm_device_at_cmd (self, "AT", error))
		return FALSE;
	if (!fu_mm_device_at_cmd (self, self->detach_fastboot_at, error)) {
		g_prefix_error (error, "rebooting into fastboot not supported: ");
		return FALSE;
	}

	/* success */
	fu_device_set_remove_delay (device, FU_MM_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_mm_device_detach (FuDevice *device, GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE (device);
	g_autoptr(FuDeviceLocker) locker  = NULL;

	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;

	/* This plugin supports currently two methods to download firmware:
	 * fastboot and qmi-pdc. A modem may require one of those, or both,
	 * depending on the update type or the modem type.
	 *
	 * The first time this detach() method is executed is always for a
	 * FuMmDevice that was created from a MM-exposed modem, which is the
	 * moment when we're going to decide the amount of retries we need to
	 * flash all firmware.
	 *
	 * If the FuMmModem is created from a MM-exposed modem and...
	 *  a) we only support fastboot, we just trigger the fastboot detach.
	 *  b) we only support qmi-pdc, we just exit without any detach.
	 *  c) we support both fastboot and qmi-pdc, we will set the
	 *     ANOTHER_WRITE_REQUIRED flag in the device and we'll trigger
	 *     the fastboot detach.
	 *
	 * If the FuMmModem is created from udev events...
	 *  d) it means we're in the extra required write that was flagged
	 *     in an earlier detach(), and we need to perform the qmi-pdc
	 *     update procedure at this time, so we just exit without any
	 *     detach.
	 */

	/* FuMmDevice created from MM... */
	if (self->omodem != NULL) {
		/* both fastboot and qmi-pdc supported? another write required */
		if ((self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT) &&
		    (self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_QMI_PDC)) {
			g_debug ("both fastboot and qmi-pdc supported, so the upgrade requires another write");
			fu_device_add_flag (device, FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED);
		}
		/* fastboot */
		if (self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT)
			return fu_mm_device_detach_fastboot (device, error);
		/* otherwise, assume we don't need any detach */
		return TRUE;
	}

	/* FuMmDevice created from udev...
	 * assume we don't need any detach */
	return TRUE;
}

typedef struct {
	gchar		*filename;
	GBytes		*bytes;
	GArray		*digest;
	gboolean	 active;
} FuMmFileInfo;

static void
fu_mm_file_info_free (FuMmFileInfo *file_info)
{
	g_clear_pointer (&file_info->digest, g_array_unref);
	g_free (file_info->filename);
	g_bytes_unref (file_info->bytes);
	g_free (file_info);
}

typedef struct {
	FuMmDevice	*device;
	GError		*error;
	GPtrArray	*file_infos;
	gsize		 total_written;
	gsize		 total_bytes;
} FuMmArchiveIterateCtx;

static gboolean
fu_mm_should_be_active (const gchar *version,
			const gchar *filename)
{
	g_auto(GStrv) split = NULL;
	g_autofree gchar *carrier_id = NULL;

	/* The filename of the mcfg file is composed of a "mcfg." prefix, then the
	 * carrier code, followed by the carrier version, and finally a ".mbn"
	 * prefix. Here we try to guess, based on the carrier code, whether the
	 * specific mcfg file should be activated after the firmware upgrade
	 * operation.
	 *
	 * This logic requires that the previous device version includes the carrier
	 * code also embedded in the version string. E.g. "xxxx.VF.xxxx". If we find
	 * this match, we assume this is the active config to use.
	 */

	split = g_strsplit (filename, ".", -1);
	if (g_strv_length (split) < 4)
		return FALSE;
	if (g_strcmp0 (split[0], "mcfg") != 0)
		return FALSE;

	carrier_id = g_strdup_printf (".%s.", split[1]);
	return (g_strstr_len (version, -1, carrier_id) != NULL);
}

static gboolean
fu_mm_qmi_pdc_archive_iterate_mcfg (FuArchive	*archive,
				    const gchar	*filename,
				    GBytes	*bytes,
				    gpointer	 user_data,
				    GError	**error)
{
	FuMmArchiveIterateCtx *ctx = user_data;
	FuMmFileInfo *file_info;

	/* filenames should be named as 'mcfg.*.mbn', e.g.: mcfg.A2.018.mbn */
	if (!g_str_has_prefix (filename, "mcfg.") || !g_str_has_suffix (filename, ".mbn"))
		return TRUE;

	file_info = g_new0 (FuMmFileInfo, 1);
	file_info->filename = g_strdup (filename);
	file_info->bytes = g_bytes_ref (bytes);
	file_info->active = fu_mm_should_be_active (fu_device_get_version (FU_DEVICE (ctx->device)), filename);
	g_ptr_array_add (ctx->file_infos, file_info);
	ctx->total_bytes += g_bytes_get_size (file_info->bytes);
	return TRUE;
}

static gboolean
fu_mm_device_qmi_open (FuMmDevice *self, GError **error)
{
	self->qmi_pdc_updater = fu_qmi_pdc_updater_new (self->port_qmi);
	return fu_qmi_pdc_updater_open (self->qmi_pdc_updater, error);
}

static gboolean
fu_mm_device_qmi_close (FuMmDevice *self, GError **error)
{
	g_autoptr(FuQmiPdcUpdater) updater = NULL;

	updater = g_steal_pointer (&self->qmi_pdc_updater);
	return fu_qmi_pdc_updater_close (updater, error);
}

static gboolean
fu_mm_device_qmi_close_no_error (FuMmDevice *self, GError **error)
{
	g_autoptr(FuQmiPdcUpdater) updater = NULL;

	updater = g_steal_pointer (&self->qmi_pdc_updater);
	fu_qmi_pdc_updater_close (updater, NULL);
	return TRUE;
}

static gboolean
fu_mm_device_write_firmware_qmi_pdc (FuDevice *device, GBytes *fw, GArray **active_id, GError **error)
{
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GPtrArray) file_infos = g_ptr_array_new_with_free_func ((GDestroyNotify)fu_mm_file_info_free);
	gint active_i = -1;
	FuMmArchiveIterateCtx archive_context = {
		.device = FU_MM_DEVICE (device),
		.error = NULL,
		.file_infos = file_infos,
		.total_written = 0,
		.total_bytes = 0,
	};

	/* decompress entire archive ahead of time */
	archive = fu_archive_new (fw, FU_ARCHIVE_FLAG_IGNORE_PATH, error);
	if (archive == NULL)
		return FALSE;

	/* boot to fastboot mode */
	locker = fu_device_locker_new_full (device,
					    (FuDeviceLockerFunc) fu_mm_device_qmi_open,
					    (FuDeviceLockerFunc) fu_mm_device_qmi_close,
					    error);
	if (locker == NULL)
		return FALSE;

	/* process the list of MCFG files to write */
	if (!fu_archive_iterate (archive,
				 fu_mm_qmi_pdc_archive_iterate_mcfg,
				 &archive_context,
				 error))
		return FALSE;

	for (guint i = 0; i < file_infos->len; i++) {
		FuMmFileInfo *file_info = g_ptr_array_index (file_infos, i);
		file_info->digest = fu_qmi_pdc_updater_write (archive_context.device->qmi_pdc_updater,
							      file_info->filename,
							      file_info->bytes,
							      &archive_context.error);
		if (file_info->digest == NULL) {
			g_prefix_error (&archive_context.error,
					"Failed to write file '%s':", file_info->filename);
			break;
		}
		/* if we wrongly detect more than one, just assume the latest one; this
		 * is not critical, it may just take a bit more time to perform the
		 * automatic carrier config switching in ModemManager */
		if (file_info->active)
			active_i = i;
	}

	/* set expected active configuration */
	if (active_i >= 0 && active_id != NULL) {
		FuMmFileInfo *file_info = g_ptr_array_index (file_infos, active_i);
		*active_id = g_array_ref (file_info->digest);
	}

	if (archive_context.error != NULL) {
		g_propagate_error (error, archive_context.error);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_mm_device_write_firmware (FuDevice *device,
			     FuFirmware *firmware,
			     FwupdInstallFlags flags,
			     GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE (device);
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) array = NULL;

	/* get default image */
	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	/* lock device */
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;

	/* qmi pdc write operation */
	if (self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_QMI_PDC)
		return fu_mm_device_write_firmware_qmi_pdc (device, fw, &self->qmi_pdc_active_id, error);

	g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
		     "unsupported update method");
	return FALSE;
}

static gboolean
fu_mm_device_attach_qmi_pdc (FuMmDevice *self, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* ignore action if there is no active id specified */
	if (self->qmi_pdc_active_id == NULL)
		return TRUE;

	/* errors closing may be expected if the device really reboots itself */
	locker = fu_device_locker_new_full (self,
					    (FuDeviceLockerFunc) fu_mm_device_qmi_open,
					    (FuDeviceLockerFunc) fu_mm_device_qmi_close_no_error,
					    error);
	if (locker == NULL)
		return FALSE;

	if (!fu_qmi_pdc_updater_activate (self->qmi_pdc_updater, self->qmi_pdc_active_id, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_mm_device_attach_noop_idle (gpointer user_data)
{
	FuMmDevice *self = FU_MM_DEVICE (user_data);
	self->attach_idle = 0;
	g_signal_emit (self, signals [SIGNAL_ATTACH_FINISHED], 0);
	return G_SOURCE_REMOVE;
}

static gboolean
fu_mm_device_attach_qmi_pdc_idle (gpointer user_data)
{
	FuMmDevice *self = FU_MM_DEVICE (user_data);
	g_autoptr(GError) error = NULL;

	if (!fu_mm_device_attach_qmi_pdc (self, &error))
		g_warning ("qmi-pdc attach operation failed: %s", error->message);
	else
		g_debug ("qmi-pdc attach operation successful");

	self->attach_idle = 0;
	g_signal_emit (self, signals [SIGNAL_ATTACH_FINISHED], 0);
	return G_SOURCE_REMOVE;
}

static gboolean
fu_mm_device_attach (FuDevice *device, GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE (device);
	g_autoptr(FuDeviceLocker) locker  = NULL;

	/* lock device */
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;

	/* we want this attach operation to be triggered asynchronously, because the engine
	 * must learn that it has to wait for replug before we actually trigger the reset. */
	if (self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_QMI_PDC)
		self->attach_idle = g_idle_add ((GSourceFunc) fu_mm_device_attach_qmi_pdc_idle, self);
	else
		self->attach_idle = g_idle_add ((GSourceFunc) fu_mm_device_attach_noop_idle, self);

	/* wait for re-probing after uninhibiting */
	fu_device_set_remove_delay (device, FU_MM_DEVICE_REMOVE_DELAY_REPROBE);
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static void
fu_mm_device_init (FuMmDevice *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_USE_RUNTIME_VERSION);
	fu_device_set_summary (FU_DEVICE (self), "Mobile broadband device");
	fu_device_add_icon (FU_DEVICE (self), "network-modem");
}

static void
fu_mm_device_finalize (GObject *object)
{
	FuMmDevice *self = FU_MM_DEVICE (object);
	if (self->attach_idle)
		g_source_remove (self->attach_idle);
	if (self->qmi_pdc_active_id)
		g_array_unref (self->qmi_pdc_active_id);
	g_object_unref (self->manager);
	if (self->omodem != NULL)
		g_object_unref (self->omodem);
	g_free (self->detach_fastboot_at);
	g_free (self->port_at);
	g_free (self->port_qmi);
	g_free (self->inhibition_uid);
	G_OBJECT_CLASS (fu_mm_device_parent_class)->finalize (object);
}

static void
fu_mm_device_class_init (FuMmDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	object_class->finalize = fu_mm_device_finalize;
	klass_device->to_string = fu_mm_device_to_string;
	klass_device->probe = fu_mm_device_probe;
	klass_device->detach = fu_mm_device_detach;
	klass_device->write_firmware = fu_mm_device_write_firmware;
	klass_device->attach = fu_mm_device_attach;

	signals [SIGNAL_ATTACH_FINISHED] =
		g_signal_new ("attach-finished",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

FuMmDevice *
fu_mm_device_new (MMManager *manager, MMObject *omodem)
{
	FuMmDevice *self = g_object_new (FU_TYPE_MM_DEVICE, NULL);
	self->manager = g_object_ref (manager);
	self->omodem = g_object_ref (omodem);
	self->port_at_ifnum = -1;
	return self;
}

FuPluginMmInhibitedDeviceInfo *
fu_plugin_mm_inhibited_device_info_new (FuMmDevice *device)
{
	FuPluginMmInhibitedDeviceInfo *info;

	info = g_new0 (FuPluginMmInhibitedDeviceInfo, 1);
	info->physical_id = g_strdup (fu_device_get_physical_id (FU_DEVICE (device)));
	info->vendor = g_strdup (fu_device_get_vendor (FU_DEVICE (device)));
	info->name = g_strdup (fu_device_get_name (FU_DEVICE (device)));
	info->version = g_strdup (fu_device_get_version (FU_DEVICE (device)));
	info->guids = fu_device_get_guids (FU_DEVICE (device));
	info->update_methods = fu_mm_device_get_update_methods (device);
	info->detach_fastboot_at = g_strdup (fu_mm_device_get_detach_fastboot_at (device));
	info->port_at_ifnum = fu_mm_device_get_port_at_ifnum (device);
	info->inhibited_uid = g_strdup (fu_mm_device_get_inhibition_uid (device));

	return info;
}

void
fu_plugin_mm_inhibited_device_info_free (FuPluginMmInhibitedDeviceInfo *info)
{
	g_free (info->inhibited_uid);
	g_free (info->physical_id);
	g_free (info->vendor);
	g_free (info->name);
	g_free (info->version);
	if (info->guids)
		g_ptr_array_unref (info->guids);
	g_free (info->detach_fastboot_at);
	g_free (info);
}

FuMmDevice *
fu_mm_device_udev_new (MMManager *manager,
		       FuPluginMmInhibitedDeviceInfo *info)
{
	FuMmDevice *self = g_object_new (FU_TYPE_MM_DEVICE, NULL);
	g_debug ("creating udev-based mm device at %s", info->physical_id);
	self->manager = g_object_ref (manager);
	fu_device_set_physical_id (FU_DEVICE (self), info->physical_id);
	fu_device_set_vendor (FU_DEVICE (self), info->vendor);
	fu_device_set_name (FU_DEVICE (self), info->name);
	fu_device_set_version (FU_DEVICE (self), info->version, FWUPD_VERSION_FORMAT_PLAIN);
	self->update_methods = info->update_methods;
	self->detach_fastboot_at = g_strdup (info->detach_fastboot_at);
	self->port_at_ifnum = info->port_at_ifnum;

	for (guint i = 0; i < info->guids->len; i++)
		fu_device_add_guid (FU_DEVICE (self), g_ptr_array_index (info->guids, i));

	return self;
}

void
fu_mm_device_udev_add_port (FuMmDevice	*self,
			    const gchar	*subsystem,
			    const gchar	*path,
			    gint	 ifnum)
{
	g_return_if_fail (FU_IS_MM_DEVICE (self));

	/* cdc-wdm ports always added unless one already set */
	if (g_str_equal (subsystem, "usbmisc") &&
	    (self->port_qmi == NULL)) {
		g_debug ("added QMI port %s (%s)", path, subsystem);
		self->port_qmi = g_strdup (path);
		return;
	}

	if (g_str_equal (subsystem, "tty") &&
	    (self->port_at == NULL) &&
	    (ifnum >= 0) && (ifnum == self->port_at_ifnum)) {
		g_debug ("added AT port %s (%s)", path, subsystem);
		self->port_at = g_strdup (path);
		return;
	}

	/* otherwise, ignore all other ports */
	g_debug ("ignoring port %s (%s)", path, subsystem);
}
