/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-io-channel.h"
#include "fu-mm-device.h"

struct _FuMmDevice {
	FuDevice			 parent_instance;
	FuIOChannel			*io_channel;
	MMManager			*manager;
	MMObject			*omodem;
	MMModemFirmwareUpdateMethod	 update_method;
	gchar				*detach_fastboot_at;
	gchar				*detach_port_at;
};

G_DEFINE_TYPE (FuMmDevice, fu_mm_device, FU_TYPE_DEVICE)

static void
fu_mm_device_to_string (FuDevice *device, GString *str)
{
	FuMmDevice *self = FU_MM_DEVICE (device);
	g_string_append (str, "  FuMmDevice:\n");
	g_string_append_printf (str, "    path:\t\t\t%s\n",
				mm_object_get_path (self->omodem));
	if (self->update_method != MM_MODEM_FIRMWARE_UPDATE_METHOD_NONE) {
		g_autofree gchar *tmp = NULL;
		tmp = mm_modem_firmware_update_method_build_string_from_mask (self->update_method);
		g_string_append_printf (str, "    detach-kind:\t\t%s\n", tmp);
	}
	if (self->detach_port_at != NULL) {
		g_string_append_printf (str, "    at-port:\t\t\t%s\n",
					self->detach_port_at);
	}
}

static gboolean
fu_mm_device_probe (FuDevice *device, GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE (device);
	MMModemFirmware *modem_fw;
	MMModem *modem = mm_object_peek_modem (self->omodem);
	MMModemPortInfo *ports = NULL;
	const gchar **device_ids;
	const gchar *version;
	guint n_ports = 0;
	g_autoptr(MMFirmwareUpdateSettings) update_settings = NULL;

	/* find out what detach method we should use */
	modem_fw = mm_object_peek_modem_firmware (self->omodem);
	update_settings = mm_modem_firmware_get_update_settings (modem_fw);
	self->update_method = mm_firmware_update_settings_get_method (update_settings);
	if (self->update_method == MM_MODEM_FIRMWARE_UPDATE_METHOD_NONE) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "modem cannot be put in programming mode");
		return FALSE;
	}

	/* various fastboot commands */
	if (self->update_method & MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT) {
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
	} else {
		g_autofree gchar *str = NULL;
		str = mm_modem_firmware_update_method_build_string_from_mask (self->update_method);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "modem detach method %s not supported", str);
		return FALSE;
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

	/* add properties to fwupd device */
	fu_device_set_physical_id (device, mm_modem_get_device (modem));
	fu_device_set_vendor (device, mm_modem_get_manufacturer (modem));
	fu_device_set_name (device, mm_modem_get_model (modem));
	fu_device_set_version (device, version);
	for (guint i = 0; device_ids[i] != NULL; i++)
		fu_device_add_guid (device, device_ids[i]);

	/* look for the AT port */
	if (!mm_modem_get_ports (modem, &ports, &n_ports)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "failed to get port information");
		return FALSE;
	}
	for (guint i = 0; i < n_ports; i++) {
		if (ports[i].type == MM_MODEM_PORT_TYPE_AT) {
			self->detach_port_at = g_strdup_printf ("/dev/%s", ports[i].name);
			break;
		}
	}
	mm_modem_port_info_array_free (ports, n_ports);

	/* this is required for detaching */
	if (self->detach_port_at == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "failed to find AT port");
		return FALSE;
	}

	return TRUE;
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
	self->io_channel = fu_io_channel_new_file (self->detach_port_at, error);
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
	fu_device_set_remove_delay (device, FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_mm_device_inhibit (FuMmDevice *self, GError **error)
{
	MMModem *modem = mm_object_peek_modem (self->omodem);

	/* prevent NM from activating the modem */
	g_debug ("inhibit %s", mm_modem_get_device (modem));
	if (!mm_manager_inhibit_device_sync (self->manager,
					     mm_modem_get_device (modem),
					     NULL, error))
		return FALSE;

	/* success: the device will disappear */
	return TRUE;
}

static gboolean
fu_mm_device_uninhibit (FuMmDevice *self, GError **error)
{
	MMModem *modem = mm_object_peek_modem (self->omodem);

	/* allow NM to activate the modem */
	g_debug ("uninhibit %s", mm_modem_get_device (modem));
	if (!mm_manager_uninhibit_device_sync (self->manager,
					       mm_modem_get_device (modem),
					       NULL, error))
		return FALSE;

	/* success: the device will re-appear in a few seconds */
	return TRUE;
}

static gboolean
fu_mm_device_detach (FuDevice *device, GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE (device);
	g_autoptr(FuDeviceLocker) locker  = NULL;

	/* boot to fastboot mode */
	locker = fu_device_locker_new_full (device,
					    (FuDeviceLockerFunc) fu_mm_device_inhibit,
					    (FuDeviceLockerFunc) fu_mm_device_uninhibit,
					    error);
	if (locker == NULL)
		return FALSE;

	/* fastboot */
	if (self->update_method & MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT)
		return fu_mm_device_detach_fastboot (device, error);

	/* should not get here */
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "modem does not support detach");
	return FALSE;
}

static void
fu_mm_device_init (FuMmDevice *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_USE_RUNTIME_VERSION);
	fu_device_set_summary (FU_DEVICE (self), "Mobile broadband device");
	fu_device_add_icon (FU_DEVICE (self), "network-modem");
}

static void
fu_mm_device_finalize (GObject *object)
{
	FuMmDevice *self = FU_MM_DEVICE (object);
	g_object_unref (self->manager);
	g_object_unref (self->omodem);
	g_free (self->detach_fastboot_at);
	g_free (self->detach_port_at);
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
}

FuMmDevice *
fu_mm_device_new (MMManager *manager, MMObject *omodem)
{
	FuMmDevice *self = g_object_new (FU_TYPE_MM_DEVICE, NULL);
	self->manager = g_object_ref (manager);
	self->omodem = g_object_ref (omodem);
	return self;
}
