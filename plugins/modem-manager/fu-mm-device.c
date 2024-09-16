/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <string.h>

#include "fu-cinterion-fdl-updater.h"
#include "fu-firehose-updater.h"
#include "fu-mbim-qdu-updater.h"
#include "fu-mm-device.h"
#include "fu-qmi-pdc-updater.h"
#include "fu-sahara-loader.h"

/* Amount of time for the modem to boot in fastboot mode. */
#define FU_MM_DEVICE_REMOVE_DELAY_RE_ENUMERATE 20000 /* ms */

/* Amount of time for the modem to be re-probed and exposed in MM after being
 * uninhibited. The timeout is long enough to cover the worst case, where the
 * modem boots without SIM card inserted (and therefore the initialization
 * may be very slow) and also where carrier config switching is explicitly
 * required (e.g. if switching from the default (DF) to generic (GC).*/
#define FU_MM_DEVICE_REMOVE_DELAY_REPROBE 210000 /* ms */

#define FU_MM_DEVICE_AT_RETRIES 3
#define FU_MM_DEVICE_AT_DELAY	3000 /* ms */

/* Amount of time for the modem to get firmware version */
#define MAX_WAIT_TIME_SECS 240 /* s */

struct _FuMmDevice {
	FuDevice parent_instance;
	MMManager *manager;

	/* ModemManager-based devices will have MMObject and inhibition_uid set,
	 * udev-based ones won't (as device is already inhibited) */
	MMObject *omodem;
	gchar *inhibition_uid;

	/* Properties read from the ModemManager-exposed modem, and to be
	 * propagated to plain udev-exposed modem objects. We assume that
	 * the firmware upgrade operation doesn't change the USB layout, and
	 * therefore the USB interface of the modem device that was an
	 * AT-capable TTY is assumed to be the same one after the upgrade.
	 */
	MMModemFirmwareUpdateMethod update_methods;
	gchar *detach_fastboot_at;
	gchar *branch_at;

	/* fastboot detach handling */
	gchar *port_at;
	FuIOChannel *io_channel;

	/* qmi-pdc update logic */
	gchar *port_qmi;
	FuQmiPdcUpdater *qmi_pdc_updater;
	GArray *qmi_pdc_active_id;
	guint attach_idle;

	/* mbim-qdu update logic */
	gchar *port_mbim;
	FuMbimQduUpdater *mbim_qdu_updater;

	/* firehose update handling */
	gchar *port_qcdm;
	gchar *port_edl;
	gchar *firehose_prog_file;
	FuSaharaLoader *sahara_loader;
	FuFirehoseUpdater *firehose_updater;

	/* for sahara */
	FuUdevDevice *udev_device;

	/* cinterion-fdl update handling */
	FuCinterionFdlUpdater *cinterion_fdl_updater;

	/* firmware path */
	gchar *firmware_path;
	gchar *restore_firmware_path;
};

enum { SIGNAL_ATTACH_FINISHED, SIGNAL_LAST };

static guint signals[SIGNAL_LAST] = {0};

G_DEFINE_TYPE(FuMmDevice, fu_mm_device, FU_TYPE_DEVICE)

static void
fu_mm_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuMmDevice *self = FU_MM_DEVICE(device);
	fwupd_codec_string_append(str, idt, "AtPort", self->port_at);
	fwupd_codec_string_append(str, idt, "QmiPort", self->port_qmi);
	fwupd_codec_string_append(str, idt, "MbimPort", self->port_mbim);
	fwupd_codec_string_append(str, idt, "QcdmPort", self->port_qcdm);
}

const gchar *
fu_mm_device_get_inhibition_uid(FuMmDevice *device)
{
	g_return_val_if_fail(FU_IS_MM_DEVICE(device), NULL);
	return device->inhibition_uid;
}

MMModemFirmwareUpdateMethod
fu_mm_device_get_update_methods(FuMmDevice *device)
{
	g_return_val_if_fail(FU_IS_MM_DEVICE(device), MM_MODEM_FIRMWARE_UPDATE_METHOD_NONE);
	return device->update_methods;
}

static gboolean
fu_mm_device_validate_firmware_update_method(FuMmDevice *self, GError **error)
{
	static const MMModemFirmwareUpdateMethod supported_combinations[] = {
	    MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT,
	    MM_MODEM_FIRMWARE_UPDATE_METHOD_QMI_PDC | MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT,
	    MM_MODEM_FIRMWARE_UPDATE_METHOD_MBIM_QDU,
	    MM_MODEM_FIRMWARE_UPDATE_METHOD_FIREHOSE,
#if MM_CHECK_VERSION(1, 19, 1)
	    MM_MODEM_FIRMWARE_UPDATE_METHOD_FIREHOSE | MM_MODEM_FIRMWARE_UPDATE_METHOD_SAHARA,
#endif
#if MM_CHECK_VERSION(1, 24, 0)
	    MM_MODEM_FIRMWARE_UPDATE_METHOD_CINTERION_FDL,
#endif
	};
	g_autofree gchar *methods_str = NULL;

	methods_str = mm_modem_firmware_update_method_build_string_from_mask(self->update_methods);
	for (guint i = 0; i < G_N_ELEMENTS(supported_combinations); i++) {
		if (supported_combinations[i] == self->update_methods) {
			g_info("valid firmware update combination: %s", methods_str);
			return TRUE;
		}
	}

	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "invalid firmware update combination: %s",
		    methods_str);
	return FALSE;
}

static void
fu_mm_device_add_instance_id(FuDevice *dev, const gchar *device_id)
{
	if (g_pattern_match_simple("???\\VID_????", device_id)) {
		fu_device_add_instance_id_full(dev, device_id, FU_DEVICE_INSTANCE_FLAG_QUIRKS);
		return;
	}
	if (g_pattern_match_simple("???\\VID_????&PID_????", device_id)) {
		fu_device_add_instance_id(dev, device_id);
		return;
	}
	if (g_pattern_match_simple("???\\VID_????&PID_????&REV_????", device_id)) {
		if (fu_device_has_private_flag(dev, FU_DEVICE_PRIVATE_FLAG_ADD_INSTANCE_ID_REV))
			fu_device_add_instance_id(dev, device_id);
		return;
	}
	if (g_pattern_match_simple("???\\VID_????&PID_????&REV_????&CARRIER_*", device_id)) {
		if (!fu_device_has_private_flag(dev, FU_MM_DEVICE_FLAG_USE_BRANCH))
			fu_device_add_instance_id(dev, device_id);
		return;
	}
	g_warning("failed to add instance ID %s", device_id);
}

static gboolean
fu_mm_device_ensure_udev_device(FuMmDevice *self, GError **error)
{
	FuContext *ctx = fu_device_get_context(FU_DEVICE(self));
	g_autoptr(FuBackend) backend = NULL;
	g_autoptr(FuUdevDevice) udev_device = NULL;

	backend = fu_context_get_backend_by_name(ctx, "udev", error);
	if (backend == NULL)
		return FALSE;
	udev_device = FU_UDEV_DEVICE(
	    fu_backend_create_device(backend, fu_device_get_physical_id(FU_DEVICE(self)), error));
	if (udev_device == NULL) {
		g_prefix_error(error,
			       "failed to create udev device for %s: ",
			       fu_device_get_physical_id(FU_DEVICE(self)));
		return FALSE;
	}
	if (!fu_device_probe(FU_DEVICE(udev_device), error))
		return FALSE;
	fu_mm_device_set_udev_device(self, udev_device);

	/* success */
	return TRUE;
}

static gboolean
fu_mm_device_probe_default(FuDevice *device, GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE(device);
	MMModemFirmware *modem_fw;
	MMModem *modem = mm_object_peek_modem(self->omodem);
	MMModemPortInfo *ports = NULL;
	const gchar **device_ids;
	const gchar *version;
	guint n_ports = 0;
	g_autoptr(MMFirmwareUpdateSettings) update_settings = NULL;
	const gchar *sysfs_path;

	/* inhibition uid is the modem interface 'Device' property, which may
	 * be the device sysfs path or a different user-provided id */
	self->inhibition_uid = mm_modem_dup_device(modem);

	/* find out what update methods we should use */
	modem_fw = mm_object_peek_modem_firmware(self->omodem);
	update_settings = mm_modem_firmware_get_update_settings(modem_fw);
	self->update_methods = mm_firmware_update_settings_get_method(update_settings);
	if (self->update_methods == MM_MODEM_FIRMWARE_UPDATE_METHOD_NONE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "modem cannot be put in programming mode");
		return FALSE;
	}

	/* make sure the combination is supported */
	if (!fu_mm_device_validate_firmware_update_method(self, error))
		return FALSE;

	/* various fastboot commands */
	if (self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT) {
		const gchar *tmp;
		tmp = mm_firmware_update_settings_get_fastboot_at(update_settings);
		if (tmp == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "modem does not set fastboot command");
			return FALSE;
		}
		self->detach_fastboot_at = g_strdup(tmp);
	}

	/* get GUIDs */
	device_ids = mm_firmware_update_settings_get_device_ids(update_settings);
	if (device_ids == NULL || device_ids[0] == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "modem did not specify any device IDs");
		return FALSE;
	}

	/* get version string, which is fw_ver+config_ver */
	version = mm_firmware_update_settings_get_version(update_settings);
	if (version == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "modem did not specify a firmware version");
		return FALSE;
	}

	/* look for the AT and QMI/MBIM ports */
	if (!mm_modem_get_ports(modem, &ports, &n_ports)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "failed to get port information");
		return FALSE;
	}
#if MM_CHECK_VERSION(1, 24, 0)
	if (self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_CINTERION_FDL) {
		for (guint i = 0; i < n_ports; i++) {
			if (ports[i].type == MM_MODEM_PORT_TYPE_AT) {
				self->port_at = g_strdup_printf("/dev/%s", ports[i].name);
				break;
			}
		}
		fu_device_add_protocol(device, "com.cinterion.fdl");
	}
#endif // MM_CHECK_VERSION(1, 24, 0)
	if (self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT) {
		for (guint i = 0; i < n_ports; i++) {
			if (ports[i].type == MM_MODEM_PORT_TYPE_AT) {
				self->port_at = g_strdup_printf("/dev/%s", ports[i].name);
				break;
			}
		}
		fu_device_add_protocol(device, "com.google.fastboot");
	}
	if (self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_QMI_PDC) {
		for (guint i = 0; i < n_ports; i++) {
			if ((ports[i].type == MM_MODEM_PORT_TYPE_QMI) ||
			    (ports[i].type == MM_MODEM_PORT_TYPE_MBIM)) {
				self->port_qmi = g_strdup_printf("/dev/%s", ports[i].name);
				break;
			}
		}
		/* only set if fastboot wasn't already set */
		if (fu_device_get_protocols(device)->len == 0)
			fu_device_add_protocol(device, "com.qualcomm.qmi_pdc");
	}
	if (self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_MBIM_QDU) {
		for (guint i = 0; i < n_ports; i++) {
			if (ports[i].type == MM_MODEM_PORT_TYPE_MBIM) {
				self->port_mbim = g_strdup_printf("/dev/%s", ports[i].name);
				break;
			}
		}
		fu_device_add_protocol(device, "com.qualcomm.mbim_qdu");
	}
	if (self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_FIREHOSE) {
		for (guint i = 0; i < n_ports; i++) {
			if ((ports[i].type == MM_MODEM_PORT_TYPE_QCDM) ||
			    (ports[i].type == MM_MODEM_PORT_TYPE_IGNORED &&
			     g_strstr_len(ports[i].name, -1, "qcdm") != NULL))
				self->port_qcdm = g_strdup_printf("/dev/%s", ports[i].name);
			else if (ports[i].type == MM_MODEM_PORT_TYPE_MBIM)
				self->port_mbim = g_strdup_printf("/dev/%s", ports[i].name);
			/* to read secboot status */
			else if (ports[i].type == MM_MODEM_PORT_TYPE_AT)
				self->port_at = g_strdup_printf("/dev/%s", ports[i].name);
		}
		fu_device_add_protocol(device, "com.qualcomm.firehose");
	}

	mm_modem_port_info_array_free(ports, n_ports);

	/* an at port is required for fastboot */
	if ((self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT) &&
	    (self->port_at == NULL)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "failed to find AT port");
		return FALSE;
	}

	/* a qmi port is required for qmi-pdc */
	if ((self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_QMI_PDC) &&
	    (self->port_qmi == NULL)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "failed to find QMI port");
		return FALSE;
	}

	/* a mbim port is required for mbim-qdu */
	if ((self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_MBIM_QDU) &&
	    (self->port_mbim == NULL)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "failed to find MBIM port");
		return FALSE;
	}

	/* a qcdm or mbim port is required for firehose */
	if ((self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_FIREHOSE) &&
	    (self->port_qcdm == NULL && self->port_mbim == NULL)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "failed to find QCDM port");
		return FALSE;
	}

#if MM_CHECK_VERSION(1, 22, 0)
	/* get the FuUdevDevice for the MM physical device */
	sysfs_path = mm_modem_get_physdev(modem);
	if (sysfs_path == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no physdev set");
		return FALSE;
	}
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "physdev not supported on ModemManager < 1.22");
	return FALSE;
#endif
	fu_device_set_physical_id(device, sysfs_path);
	if (!fu_mm_device_ensure_udev_device(self, error))
		return FALSE;

	/* add properties to fwupd device */
	if (mm_modem_get_manufacturer(modem) != NULL)
		fu_device_set_vendor(device, mm_modem_get_manufacturer(modem));
	if (mm_modem_get_model(modem) != NULL)
		fu_device_set_name(device, mm_modem_get_model(modem));

	/* only for modems that opt-in */
	if (fu_device_has_private_flag(device, FU_MM_DEVICE_FLAG_USE_BRANCH))
		fu_device_set_branch(device, mm_modem_get_carrier_configuration(modem));

	fu_device_set_version(device, version);

	/* filter these */
	for (guint i = 0; device_ids[i] != NULL; i++)
		fu_mm_device_add_instance_id(device, device_ids[i]);

	/* fix up vendor name */
	if (g_strcmp0(fu_device_get_vendor(device), "QUALCOMM INCORPORATED") == 0)
		fu_device_set_vendor(device, "Qualcomm");

	/* success */
	return TRUE;
}

static gboolean
fu_mm_device_probe_udev(FuDevice *device, GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE(device);

	/* an at port is required for fastboot */
	if ((self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT) &&
	    (self->port_at == NULL)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "failed to find AT port");
		return FALSE;
	}

#if MM_CHECK_VERSION(1, 24, 0)
	if ((self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_CINTERION_FDL) &&
	    (self->port_at == NULL)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "failed to find AT port");
		return FALSE;
	}
#endif // MM_CHECK_VERSION(1, 24, 0)

	/* a qmi port is required for qmi-pdc */
	if ((self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_QMI_PDC) &&
	    (self->port_qmi == NULL)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "failed to find QMI port");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_mm_device_probe(FuDevice *device, GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE(device);

	if (self->omodem)
		return fu_mm_device_probe_default(device, error);
	return fu_mm_device_probe_udev(device, error);
}

static gboolean
fu_mm_device_io_open_qcdm(FuMmDevice *self, GError **error)
{
	/* sanity check */
	if (self->port_qcdm == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no QCDM port provided for filename");
		return FALSE;
	}

	/* open device */
	self->io_channel =
	    fu_io_channel_new_file(self->port_qcdm,
				   FU_IO_CHANNEL_OPEN_FLAG_READ | FU_IO_CHANNEL_OPEN_FLAG_WRITE,
				   error);
	if (self->io_channel == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_mm_device_qcdm_cmd(FuMmDevice *self, const guint8 *cmd, gsize cmd_len, GError **error)
{
	g_autoptr(GBytes) qcdm_req = NULL;
	g_autoptr(GBytes) qcdm_res = NULL;

	/* command */
	qcdm_req = g_bytes_new(cmd, cmd_len);
	fu_dump_bytes(G_LOG_DOMAIN, "writing", qcdm_req);
	if (!fu_io_channel_write_bytes(self->io_channel,
				       qcdm_req,
				       1500,
				       FU_IO_CHANNEL_FLAG_FLUSH_INPUT,
				       error)) {
		g_prefix_error(error, "failed to write qcdm command: ");
		return FALSE;
	}

	/* response */
	qcdm_res = fu_io_channel_read_bytes(self->io_channel,
					    -1,
					    1500,
					    FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
					    error);
	if (qcdm_res == NULL) {
		g_prefix_error(error, "failed to read qcdm response: ");
		return FALSE;
	}
	fu_dump_bytes(G_LOG_DOMAIN, "read", qcdm_res);

	/* command == response */
	if (g_bytes_compare(qcdm_res, qcdm_req) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to read valid qcdm response");
		return FALSE;
	}

	return TRUE;
}

typedef struct {
	const gchar *cmd;
	gboolean has_response;
} FuMmDeviceAtCmdHelper;

static gboolean
fu_mm_device_at_cmd_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE(device);
	FuMmDeviceAtCmdHelper *helper = (FuMmDeviceAtCmdHelper *)user_data;
	const gchar *buf;
	gsize bufsz = 0;
	g_autoptr(GBytes) at_req = NULL;
	g_autoptr(GBytes) at_res = NULL;
	g_autofree gchar *cmd_cr = g_strdup_printf("%s\r\n", helper->cmd);

	/* command */
	at_req = g_bytes_new(cmd_cr, strlen(cmd_cr));
	fu_dump_bytes(G_LOG_DOMAIN, "writing", at_req);
	if (!fu_io_channel_write_bytes(self->io_channel,
				       at_req,
				       1500,
				       FU_IO_CHANNEL_FLAG_FLUSH_INPUT,
				       error)) {
		g_prefix_error(error, "failed to write %s: ", helper->cmd);
		return FALSE;
	}

	/* AT command has no response, return TRUE */
	if (!helper->has_response) {
		g_debug("no response expected for AT command: '%s', assuming succeed", helper->cmd);
		return TRUE;
	}

	/* response */
	at_res = fu_io_channel_read_bytes(self->io_channel,
					  -1,
					  1500,
					  FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
					  error);
	if (at_res == NULL) {
		g_prefix_error(error, "failed to read response for %s: ", helper->cmd);
		return FALSE;
	}
	fu_dump_bytes(G_LOG_DOMAIN, "read", at_res);
	buf = g_bytes_get_data(at_res, &bufsz);

	/*
	 * the first time the modem returns may be the command itself with one \n missing.
	 * this is because the modem AT has enabled echo
	 */
	if (g_strrstr(buf, helper->cmd) != NULL && bufsz == strlen(helper->cmd) + 1) {
		g_bytes_unref(at_res);
		at_res = fu_io_channel_read_bytes(self->io_channel,
						  -1,
						  1500,
						  FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
						  error);
		if (at_res == NULL) {
			g_prefix_error(error, "failed to read response for %s: ", helper->cmd);
			return FALSE;
		}
		buf = g_bytes_get_data(at_res, &bufsz);
	}

	if (bufsz < 6) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to read valid response for %s",
			    helper->cmd);
		return FALSE;
	}

	/* return error if AT command failed */
	if (g_strrstr(buf, "\r\nOK\r\n") == NULL) {
		g_autofree gchar *tmp = g_strndup(buf + 2, bufsz - 4);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to read valid response for %s: %s",
			    helper->cmd,
			    tmp);
		return FALSE;
	}

	/* set firmware branch if returned */
	if (self->branch_at != NULL && g_strcmp0(helper->cmd, self->branch_at) == 0) {
		/*
		 * example AT+GETFWBRANCH response:
		 *
		 * \r\nFOSS-002 \r\n\r\nOK\r\n
		 *
		 * remove \r\n, and OK to get branch name
		 */
		g_auto(GStrv) parts = g_strsplit(buf, "\r\n", -1);

		for (int j = 0; parts[j] != NULL; j++) {
			/* Ignore empty strings, and OK responses */
			if (g_strcmp0(parts[j], "") != 0 && g_strcmp0(parts[j], "OK") != 0) {
				/* Set branch */
				fu_device_set_branch(FU_DEVICE(self), parts[j]);
				g_info("firmware branch reported as '%s'", parts[j]);
				break;
			}
		}
	}

	if (g_strcmp0(helper->cmd, "AT+QSECBOOT=\"status\"") == 0) {
		/*
		 * example AT+QSECBOOT="status" response:
		 *
		 * \r\n+QSECBOOT: "STATUS",1\r\n\r\nOK\r\n
		 *
		 * Secure boot status:
		 * 1 - enabled
		 * 0 - disabled
		 */
		g_auto(GStrv) parts = g_strsplit(buf, "\r\n", -1);

		for (int j = 0; parts[j] != NULL; j++) {
			if (g_strcmp0(parts[j], "+QSECBOOT: \"status\",1") == 0) {
				fu_device_add_flag(FU_DEVICE(self),
						   FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
				break;
			}
			if (g_strcmp0(parts[j], "+QSECBOOT: \"status\",0") == 0) {
				fu_device_add_flag(FU_DEVICE(self),
						   FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
				break;
			}
		}
	}

	if (g_strcmp0(helper->cmd, "AT+QCFG=\"secbootstat\"") == 0) {
		/*
		 * example AT+QSECBOOT="status" response:
		 *
		 * \r\n+QSECBOOT: "STATUS",1\r\n\r\nOK\r\n
		 *
		 * Secure boot status:
		 * 1 - enabled
		 * 0 - disabled
		 */
		g_auto(GStrv) parts = g_strsplit(buf, "\r\n", -1);

		for (int j = 0; parts[j] != NULL; j++) {
			if (g_strcmp0(parts[j], "+QCFG: \"secbootstat\",1") == 0) {
				fu_device_add_flag(FU_DEVICE(self),
						   FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
				break;
			}
			if (g_strcmp0(parts[j], "+QCFG: \"secbootstat\",0") == 0) {
				fu_device_add_flag(FU_DEVICE(self),
						   FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
				break;
			}
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_mm_device_at_cmd(FuMmDevice *self, const gchar *cmd, gboolean has_response, GError **error)
{
	FuMmDeviceAtCmdHelper helper = {.cmd = cmd, .has_response = has_response};
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_mm_device_at_cmd_cb,
				    FU_MM_DEVICE_AT_RETRIES,
				    FU_MM_DEVICE_AT_DELAY,
				    &helper,
				    error);
}

static gboolean
fu_mm_device_io_open(FuMmDevice *self, GError **error)
{
	/* sanity check */
	if (self->port_at == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no AT port provided for filename");
		return FALSE;
	}

	/* open device */
	self->io_channel =
	    fu_io_channel_new_file(self->port_at,
				   FU_IO_CHANNEL_OPEN_FLAG_READ | FU_IO_CHANNEL_OPEN_FLAG_WRITE,
				   error);
	if (self->io_channel == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_mm_device_io_close(FuMmDevice *self, GError **error)
{
	if (self->io_channel != NULL) {
		if (!fu_io_channel_shutdown(self->io_channel, error))
			return FALSE;
		g_clear_object(&self->io_channel);
	}
	return TRUE;
}

#if MM_CHECK_VERSION(1, 24, 0)
static gboolean
fu_mm_device_cinterion_fdl_open(FuMmDevice *self, GError **error)
{
	self->cinterion_fdl_updater = fu_cinterion_fdl_updater_new(self->port_at);
	return fu_cinterion_fdl_updater_open(self->cinterion_fdl_updater, error);
}

static gboolean
fu_mm_device_cinterion_fdl_close(FuMmDevice *self, GError **error)
{
	g_autoptr(FuCinterionFdlUpdater) updater = NULL;

	updater = g_steal_pointer(&self->cinterion_fdl_updater);
	return fu_cinterion_fdl_updater_close(updater, error);
}

static gboolean
fu_mm_device_detach_fdl(FuDevice *device, FuProgress *progress, GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE(device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	locker = fu_device_locker_new_full(device,
					   (FuDeviceLockerFunc)fu_mm_device_io_open,
					   (FuDeviceLockerFunc)fu_mm_device_io_close,
					   error);

	if (locker == NULL)
		return FALSE;
	if (!fu_mm_device_at_cmd(self, "AT", TRUE, error))
		return FALSE;
	if (!fu_mm_device_at_cmd(self, "AT^SFDL", TRUE, error)) {
		g_prefix_error(error, "enabling firmware download mode not supported: ");
		return FALSE;
	}

	if (!fu_device_locker_close(locker, error))
		return FALSE;

	/* wait 15 s before reopening port */
	fu_device_sleep(device, 15000);

	locker = fu_device_locker_new_full(self,
					   (FuDeviceLockerFunc)fu_mm_device_cinterion_fdl_open,
					   (FuDeviceLockerFunc)fu_mm_device_cinterion_fdl_close,
					   error);
	if (locker == NULL)
		return FALSE;

	return fu_cinterion_fdl_updater_wait_ready(self->cinterion_fdl_updater, device, error);
}
#endif // MM_CHECK_VERSION(1, 24, 0)

static gboolean
fu_mm_device_detach_fastboot(FuDevice *device, GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE(device);
	g_autoptr(FuDeviceLocker) locker = NULL;
	gboolean has_response = TRUE;

	/* boot to fastboot mode */
	locker = fu_device_locker_new_full(device,
					   (FuDeviceLockerFunc)fu_mm_device_io_open,
					   (FuDeviceLockerFunc)fu_mm_device_io_close,
					   error);

	/* expect response for fastboot AT command */
	if (fu_device_has_private_flag(FU_DEVICE(self),
				       FU_MM_DEVICE_FLAG_DETACH_AT_FASTBOOT_HAS_NO_RESPONSE)) {
		has_response = FALSE;
	}

	if (locker == NULL)
		return FALSE;
	if (!fu_mm_device_at_cmd(self, "AT", TRUE, error))
		return FALSE;
	if (!fu_mm_device_at_cmd(self, self->detach_fastboot_at, has_response, error)) {
		g_prefix_error(error, "rebooting into fastboot not supported: ");
		return FALSE;
	}

	/* success */
	fu_device_set_remove_delay(device, FU_MM_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_mm_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE(device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;

	/* This plugin supports several methods to download firmware:
	 * fastboot, qmi-pdc, firehose. A modem may require one of those,
	 * or several, depending on the update type or the modem type.
	 *
	 * The first time this detach() method is executed is always for a
	 * FuMmDevice that was created from a MM-exposed modem, which is the
	 * moment when we're going to decide the amount of retries we need to
	 * flash all firmware.
	 *
	 * If the FuMmModem is created from a MM-exposed modem and...
	 *  a) we only support fastboot, we just trigger the fastboot detach.
	 *  b) we support both fastboot and qmi-pdc, we will set the
	 *     ANOTHER_WRITE_REQUIRED flag in the device and we'll trigger
	 *     the fastboot detach.
	 *  c) we only support firehose, skip detach and switch to embedded
	 *     downloader mode (EDL) during write_firmware.
	 *
	 * If the FuMmModem is created from udev events...
	 *  c) it means we're in the extra required write that was flagged
	 *     in an earlier detach(), and we need to perform the qmi-pdc
	 *     update procedure at this time, so we just exit without any
	 *     detach.
	 */

	/* FuMmDevice created from MM... */
	if (self->omodem != NULL) {
		/* both fastboot and qmi-pdc supported? another write required */
		if ((self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT) &&
		    (self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_QMI_PDC)) {
			g_debug("both fastboot and qmi-pdc supported, so the upgrade requires "
				"another write");
			fu_device_add_flag(device, FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED);
		}
		/* fastboot */
		if (self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT)
			return fu_mm_device_detach_fastboot(device, error);
#if MM_CHECK_VERSION(1, 24, 0)
		if (self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_CINTERION_FDL)
			return fu_mm_device_detach_fdl(device, progress, error);
#endif // MM_CHECK_VERSION(1, 24, 0)
		/* otherwise, assume we don't need any detach */
		return TRUE;
	}

	/* FuMmDevice created from udev...
	 * assume we don't need any detach */
	return TRUE;
}

typedef struct {
	gchar *filename;
	GBytes *bytes;
	GArray *digest;
	gboolean active;
} FuMmFileInfo;

static void
fu_mm_device_file_info_free(FuMmFileInfo *file_info)
{
	g_clear_pointer(&file_info->digest, g_array_unref);
	g_free(file_info->filename);
	g_bytes_unref(file_info->bytes);
	g_free(file_info);
}

typedef struct {
	FuMmDevice *self;
	GError *error;
	GPtrArray *file_infos;
} FuMmArchiveIterateCtx;

static gboolean
fu_mm_device_should_be_active(const gchar *version, const gchar *filename)
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

	split = g_strsplit(filename, ".", -1);
	if (g_strv_length(split) < 4)
		return FALSE;
	if (g_strcmp0(split[0], "mcfg") != 0)
		return FALSE;

	carrier_id = g_strdup_printf(".%s.", split[1]);
	return (g_strstr_len(version, -1, carrier_id) != NULL);
}

static gboolean
fu_mm_device_qmi_pdc_archive_iterate_mcfg(FuArchive *archive,
					  const gchar *filename,
					  GBytes *bytes,
					  gpointer user_data,
					  GError **error)
{
	FuMmArchiveIterateCtx *ctx = user_data;
	FuMmFileInfo *file_info;

	/* filenames should be named as 'mcfg.*.mbn', e.g.: mcfg.A2.018.mbn */
	if (!g_str_has_prefix(filename, "mcfg.") || !g_str_has_suffix(filename, ".mbn"))
		return TRUE;

	file_info = g_new0(FuMmFileInfo, 1);
	file_info->filename = g_strdup(filename);
	file_info->bytes = g_bytes_ref(bytes);
	file_info->active =
	    fu_mm_device_should_be_active(fu_device_get_version(FU_DEVICE(ctx->self)), filename);
	g_ptr_array_add(ctx->file_infos, file_info);
	return TRUE;
}

static gboolean
fu_mm_device_qmi_open(FuMmDevice *self, GError **error)
{
	self->qmi_pdc_updater = fu_qmi_pdc_updater_new(self->port_qmi);
	return fu_qmi_pdc_updater_open(self->qmi_pdc_updater, error);
}

static gboolean
fu_mm_device_qmi_close(FuMmDevice *self, GError **error)
{
	g_autoptr(FuQmiPdcUpdater) updater = NULL;

	updater = g_steal_pointer(&self->qmi_pdc_updater);
	return fu_qmi_pdc_updater_close(updater, error);
}

static gboolean
fu_mm_device_qmi_close_no_error(FuMmDevice *self, GError **error)
{
	g_autoptr(FuQmiPdcUpdater) updater = NULL;

	updater = g_steal_pointer(&self->qmi_pdc_updater);
	fu_qmi_pdc_updater_close(updater, NULL);
	return TRUE;
}

static gboolean
fu_mm_device_write_firmware_qmi_pdc(FuMmDevice *self,
				    GBytes *fw,
				    GArray **active_id,
				    GError **error)
{
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GPtrArray) file_infos =
	    g_ptr_array_new_with_free_func((GDestroyNotify)fu_mm_device_file_info_free);
	gint active_i = -1;
	FuMmArchiveIterateCtx archive_context = {
	    .self = self,
	    .error = NULL,
	    .file_infos = file_infos,
	};

	/* decompress entire archive ahead of time */
	archive = fu_archive_new(fw, FU_ARCHIVE_FLAG_IGNORE_PATH, error);
	if (archive == NULL)
		return FALSE;

	/* boot to fastboot mode */
	locker = fu_device_locker_new_full(FU_DEVICE(self),
					   (FuDeviceLockerFunc)fu_mm_device_qmi_open,
					   (FuDeviceLockerFunc)fu_mm_device_qmi_close,
					   error);
	if (locker == NULL)
		return FALSE;

	/* process the list of MCFG files to write */
	if (!fu_archive_iterate(archive,
				fu_mm_device_qmi_pdc_archive_iterate_mcfg,
				&archive_context,
				error))
		return FALSE;

	for (guint i = 0; i < file_infos->len; i++) {
		FuMmFileInfo *file_info = g_ptr_array_index(file_infos, i);
		file_info->digest = fu_qmi_pdc_updater_write(archive_context.self->qmi_pdc_updater,
							     file_info->filename,
							     file_info->bytes,
							     &archive_context.error);
		if (file_info->digest == NULL) {
			g_prefix_error(&archive_context.error,
				       "Failed to write file '%s':",
				       file_info->filename);
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
		FuMmFileInfo *file_info = g_ptr_array_index(file_infos, active_i);
		*active_id = g_array_ref(file_info->digest);
	}

	if (archive_context.error != NULL) {
		g_propagate_error(error, archive_context.error);
		return FALSE;
	}

	return TRUE;
}

typedef struct {
	FuMmDevice *self; /* no ref */
	GMainLoop *mainloop;
	gchar *version;
	GError *error;
} FuMmGetFirmwareVersionCtx;

static gboolean
fu_mm_device_mbim_open(FuMmDevice *self, GError **error)
{
	self->mbim_qdu_updater = fu_mbim_qdu_updater_new(self->port_mbim);
	return fu_mbim_qdu_updater_open(self->mbim_qdu_updater, error);
}

static gboolean
fu_mm_device_mbim_close(FuMmDevice *self, GError **error)
{
	g_autoptr(FuMbimQduUpdater) updater = NULL;
	updater = g_steal_pointer(&self->mbim_qdu_updater);
	return fu_mbim_qdu_updater_close(updater, error);
}

static gboolean
fu_mm_device_locker_new_timeout(gpointer user_data)
{
	FuMmGetFirmwareVersionCtx *ctx = user_data;

	g_main_loop_quit(ctx->mainloop);

	return G_SOURCE_REMOVE;
}

static gboolean
fu_mm_device_get_firmware_version_mbim_timeout(gpointer user_data)
{
	FuMmGetFirmwareVersionCtx *ctx = user_data;
	FuMmDevice *self = FU_MM_DEVICE(ctx->self);

	g_clear_error(&ctx->error);
	ctx->version = fu_mbim_qdu_updater_check_ready(self->mbim_qdu_updater, &ctx->error);
	g_main_loop_quit(ctx->mainloop);

	return G_SOURCE_REMOVE;
}

static gchar *
fu_mm_device_get_firmware_version_mbim(FuMmDevice *self, GError **error)
{
	GTimer *timer = g_timer_new();
	g_autoptr(GMainLoop) mainloop = g_main_loop_new(NULL, FALSE);
	FuMmGetFirmwareVersionCtx ctx = {
	    .self = self,
	    .mainloop = mainloop,
	    .version = NULL,
	    .error = NULL,
	};

	while (ctx.version == NULL && g_timer_elapsed(timer, NULL) < MAX_WAIT_TIME_SECS) {
		g_autoptr(FuDeviceLocker) locker = NULL;

		g_clear_error(&ctx.error);
		locker = fu_device_locker_new_full(FU_DEVICE(self),
						   (FuDeviceLockerFunc)fu_mm_device_mbim_open,
						   (FuDeviceLockerFunc)fu_mm_device_mbim_close,
						   &ctx.error);

		if (locker == NULL) {
			g_timeout_add_seconds(20, fu_mm_device_locker_new_timeout, &ctx);
			g_main_loop_run(mainloop);
			continue;
		}

		g_timeout_add_seconds(10, fu_mm_device_get_firmware_version_mbim_timeout, &ctx);
		g_main_loop_run(mainloop);
	}

	g_timer_destroy(timer);

	if (ctx.version == NULL && ctx.error != NULL) {
		g_propagate_error(error, ctx.error);
		return NULL;
	}

	return ctx.version;
}

static gboolean
fu_mm_device_writeln(const gchar *fn, const gchar *buf, GError **error)
{
	g_autoptr(FuIOChannel) io = NULL;
	io = fu_io_channel_new_file(fn, FU_IO_CHANNEL_OPEN_FLAG_WRITE, error);
	if (io == NULL)
		return FALSE;
	return fu_io_channel_write_raw(io,
				       (const guint8 *)buf,
				       strlen(buf),
				       1000,
				       FU_IO_CHANNEL_FLAG_NONE,
				       error);
}

static gboolean
fu_mm_device_set_autosuspend_delay(FuMmDevice *self, guint timeout_ms, GError **error)
{
	g_autofree gchar *autosuspend_delay_filename = NULL;
	g_autofree gchar *buf = g_strdup_printf("%u", timeout_ms);

	/* autosuspend delay updated for a proper firmware update */
	autosuspend_delay_filename = g_build_filename(fu_device_get_physical_id(FU_DEVICE(self)),
						      "/power/autosuspend_delay_ms",
						      NULL);
	return fu_mm_device_writeln(autosuspend_delay_filename, buf, error);
}

static gboolean
fu_mm_device_write_firmware_mbim_qdu(FuDevice *device,
				     GBytes *fw,
				     FuProgress *progress,
				     GError **error)
{
	XbNode *part = NULL;
	const gchar *filename = NULL;
	const gchar *csum;
	FuMmDevice *self = FU_MM_DEVICE(device);
	g_autofree gchar *csum_actual = NULL;
	g_autofree gchar *version = NULL;
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GArray) digest = NULL;
	g_autoptr(GBytes) data_xml = NULL;
	g_autoptr(GBytes) data_part = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbSilo) silo = NULL;

	/* decompress entire archive ahead of time */
	archive = fu_archive_new(fw, FU_ARCHIVE_FLAG_IGNORE_PATH, error);
	if (archive == NULL)
		return FALSE;

	locker = fu_device_locker_new_full(device,
					   (FuDeviceLockerFunc)fu_mm_device_mbim_open,
					   (FuDeviceLockerFunc)fu_mm_device_mbim_close,
					   error);
	if (locker == NULL)
		return FALSE;

	/* load the manifest of operations */
	data_xml = fu_archive_lookup_by_fn(archive, "flashfile.xml", error);
	if (data_xml == NULL)
		return FALSE;
	if (!xb_builder_source_load_bytes(source, data_xml, XB_BUILDER_SOURCE_FLAG_NONE, error))
		return FALSE;
	xb_builder_import_source(builder, source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, error);
	if (silo == NULL)
		return FALSE;

	part = xb_silo_query_first(silo, "parts/part", error);
	if (part == NULL)
		return FALSE;
	filename = xb_node_get_attr(part, "filename");
	csum = xb_node_get_attr(part, "MD5");
	data_part = fu_archive_lookup_by_fn(archive, filename, error);
	if (data_part == NULL)
		return FALSE;
	csum_actual = g_compute_checksum_for_bytes(G_CHECKSUM_MD5, data_part);
	if (g_strcmp0(csum, csum_actual) != 0) {
		g_debug("[%s] MD5 not matched", filename);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "[%s] MD5 not matched",
			    filename);
		return FALSE;
	}
	g_debug("[%s] MD5 matched", filename);

	/* autosuspend delay updated for a proper firmware update */
	if (!fu_mm_device_set_autosuspend_delay(self, 20000, error))
		return FALSE;

	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	digest = fu_mbim_qdu_updater_write(self->mbim_qdu_updater,
					   filename,
					   data_part,
					   device,
					   progress,
					   error);
	if (digest == NULL)
		return FALSE;
	if (!fu_device_locker_close(locker, error))
		return FALSE;

	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_READ);
	version = fu_mm_device_get_firmware_version_mbim(self, error);
	if (version == NULL)
		return FALSE;

	return TRUE;
}

static gboolean
fu_mm_device_qcdm_switch_to_edl_cb(FuDevice *device, gpointer userdata, GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE(device);
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	static const guint8 emergency_download[] = {0x4b, 0x65, 0x01, 0x00, 0x54, 0x0f, 0x7e};

	locker = fu_device_locker_new_full(self,
					   (FuDeviceLockerFunc)fu_mm_device_io_open_qcdm,
					   (FuDeviceLockerFunc)fu_mm_device_io_close,
					   &error_local);

	if (locker == NULL) {
		/* FIXME: this should have been done at attach */
		// if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE)) {
		//	return fu_mm_device_find_edl_port(device, "wwan", error);
		// }
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}

	if (!fu_mm_device_qcdm_cmd(self,
				   emergency_download,
				   G_N_ELEMENTS(emergency_download),
				   error))
		return FALSE;

	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_FOUND,
		    "Device haven't switched to EDL yet");
	return FALSE;
}

#if MBIM_CHECK_VERSION(1, 27, 5)
static void
fu_mm_device_switch_to_edl_mbim_ready(MbimDevice *device, GAsyncResult *res, GMainLoop *loop)
{
	/* No need to check for a response since MBIM
	 * port goes away without sending one */

	g_main_loop_quit(loop);
}

static gboolean
fu_mm_device_mbim_switch_to_edl(FuDevice *device, GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE(device);
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(MbimMessage) message = NULL;
	g_autoptr(GMainLoop) mainloop = g_main_loop_new(NULL, FALSE);

	locker = fu_device_locker_new_full(device,
					   (FuDeviceLockerFunc)fu_mm_device_mbim_open,
					   (FuDeviceLockerFunc)fu_mm_device_mbim_close,
					   error);
	if (locker == NULL)
		return FALSE;

	message = mbim_message_qdu_quectel_reboot_set_new(MBIM_QDU_QUECTEL_REBOOT_TYPE_EDL, NULL);
	mbim_device_command(fu_mbim_qdu_updater_get_mbim_device(self->mbim_qdu_updater),
			    message,
			    5,
			    NULL,
			    (GAsyncReadyCallback)fu_mm_device_switch_to_edl_mbim_ready,
			    mainloop);

	g_main_loop_run(mainloop);

	return TRUE;
}
#endif // MBIM_CHECK_VERSION(1, 27, 5)

static gboolean
fu_mm_device_firehose_open(FuMmDevice *self, GError **error)
{
	self->firehose_updater = fu_firehose_updater_new(self->port_edl, self->sahara_loader);
	return fu_firehose_updater_open(self->firehose_updater, error);
}

static gboolean
fu_mm_device_firehose_close(FuMmDevice *self, GError **error)
{
	g_autoptr(FuFirehoseUpdater) updater = NULL;

	updater = g_steal_pointer(&self->firehose_updater);
	return fu_firehose_updater_close(updater, error);
}

static gboolean
fu_mm_device_firehose_write(FuMmDevice *self,
			    XbSilo *rawprogram_silo,
			    GPtrArray *rawprogram_actions,
			    FuProgress *progress,
			    GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	locker = fu_device_locker_new_full(self,
					   (FuDeviceLockerFunc)fu_mm_device_firehose_open,
					   (FuDeviceLockerFunc)fu_mm_device_firehose_close,
					   error);
	if (locker == NULL)
		return FALSE;
	return fu_firehose_updater_write(self->firehose_updater,
					 rawprogram_silo,
					 rawprogram_actions,
					 progress,
					 error);
}

#if MM_CHECK_VERSION(1, 19, 1)
static gboolean
fu_mm_device_sahara_open(FuMmDevice *self, GError **error)
{
	self->sahara_loader = fu_sahara_loader_new();
	return fu_sahara_loader_open(self->sahara_loader, FU_USB_DEVICE(self->udev_device), error);
}

static gboolean
fu_mm_device_sahara_close(FuMmDevice *self, GError **error)
{
	g_autoptr(FuSaharaLoader) loader = NULL;

	loader = g_steal_pointer(&self->sahara_loader);
	return fu_sahara_loader_close(loader, error);
}
#endif // MM_CHECK_VERSION(1, 19, 1)

static gboolean
fu_mm_device_setup_firmware_dir(FuMmDevice *self, GError **error)
{
	g_autofree gchar *cachedir = NULL;
	g_autofree gchar *mm_fw_dir = NULL;

	/* create a directory to store firmware files for modem-manager plugin */
	cachedir = fu_path_from_kind(FU_PATH_KIND_CACHEDIR_PKG);
	mm_fw_dir = g_build_filename(cachedir, "modem-manager", "firmware", NULL);
	if (g_mkdir_with_parents(mm_fw_dir, 0700) == -1) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to create '%s': %s",
			    mm_fw_dir,
			    g_strerror(errno));
		return FALSE;
	}

	if (!fu_kernel_set_firmware_search_path(mm_fw_dir, error))
		return FALSE;

	self->firmware_path = g_steal_pointer(&mm_fw_dir);

	return TRUE;
}

static gboolean
fu_mm_device_copy_firehose_prog(FuMmDevice *self, GBytes *prog, GError **error)
{
	g_autofree gchar *qcom_fw_dir = NULL;
	g_autofree gchar *firehose_file_path = NULL;

	if (self->firehose_prog_file == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "Firehose prog filename is not set for the device");
		return FALSE;
	}

	qcom_fw_dir = g_build_filename(self->firmware_path, "qcom", NULL);
	if (!fu_path_mkdir_parent(qcom_fw_dir, error))
		return FALSE;

	firehose_file_path = g_build_filename(qcom_fw_dir, self->firehose_prog_file, NULL);

	if (!fu_bytes_set_contents(firehose_file_path, prog, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_mm_device_prepare_firmware_search_path(FuMmDevice *self, GError **error)
{
	self->restore_firmware_path = fu_kernel_get_firmware_search_path(NULL);

	return fu_mm_device_setup_firmware_dir(self, error);
}

static gboolean
fu_mm_device_restore_firmware_search_path(FuMmDevice *self, GError **error)
{
	if (self->restore_firmware_path != NULL && strlen(self->restore_firmware_path) > 0)
		return fu_kernel_set_firmware_search_path(self->restore_firmware_path, error);

	return fu_kernel_reset_firmware_search_path(error);
}

static gboolean
fu_mm_device_write_firmware_firehose(FuDevice *device,
				     GBytes *fw,
				     FuProgress *progress,
				     GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE(device);
	MMModem *modem = mm_object_peek_modem(self->omodem);
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(XbSilo) firehose_rawprogram_silo = NULL;
	g_autoptr(GBytes) firehose_prog = NULL;
	g_autoptr(GBytes) firehose_rawprogram = NULL;
	g_autoptr(GPtrArray) firehose_rawprogram_actions = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 10, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 90, NULL);

	/* decompress entire archive ahead of time */
	archive = fu_archive_new(fw, FU_ARCHIVE_FLAG_IGNORE_PATH, error);
	if (archive == NULL)
		return FALSE;

	/* lookup and validate firehose-rawprogram actions */
	firehose_rawprogram = fu_archive_lookup_by_fn(archive, "firehose-rawprogram.xml", error);
	if (firehose_rawprogram == NULL)
		return FALSE;
	if (!fu_firehose_updater_validate_rawprogram(firehose_rawprogram,
						     archive,
						     &firehose_rawprogram_silo,
						     &firehose_rawprogram_actions,
						     error)) {
		g_prefix_error(error, "Invalid firehose rawprogram manifest: ");
		return FALSE;
	}

	/* lookup firehose-prog bootloader */
	firehose_prog = fu_archive_lookup_by_fn(archive, "firehose-prog.mbn", error);
	if (firehose_prog == NULL)
		return FALSE;
	fu_progress_step_done(progress);

	/* Firehose program needs to be loaded to the modem before firehose update process can
	 * start. Generally, modems use Sahara protocol to load the firehose binary.
	 *
	 * In case of MHI PCI modems, the mhi-pci-generic driver reads the firehose binary from the
	 * firmware-loader and writes it to the modem.
	 **/
	if (g_strv_contains(mm_modem_get_drivers(modem), "mhi-pci-generic") &&
	    self->port_qcdm != NULL) {
		/* modify firmware search path and restore it before function returns */
		locker = fu_device_locker_new_full(
		    self,
		    (FuDeviceLockerFunc)fu_mm_device_prepare_firmware_search_path,
		    (FuDeviceLockerFunc)fu_mm_device_restore_firmware_search_path,
		    error);
		if (locker == NULL)
			return FALSE;

		/* firehose modems that use mhi_pci drivers require firehose binary
		 * to be present in the firmware-loader search path. */
		if (!fu_mm_device_copy_firehose_prog(self, firehose_prog, error))
			return FALSE;

		/* FIXME: this should have been done in attach */

		/* trigger emergency download mode, up to 30s retrying until the QCDM
		 * port goes away; this takes us to the EDL (embedded downloader) execution
		 * environment */
		if (!fu_device_retry_full(FU_DEVICE(self),
					  fu_mm_device_qcdm_switch_to_edl_cb,
					  30,
					  1000,
					  NULL,
					  error))
			return FALSE;

		g_debug("found edl port: %s", self->port_edl);
	}
#if MM_CHECK_VERSION(1, 19, 1)
	else if ((FU_MM_DEVICE(self)->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_SAHARA) &&
		 self->port_mbim != NULL) {
		/* switch to emergency download (EDL) execution environment */
		if (!fu_mm_device_mbim_switch_to_edl(device, error))
			return FALSE;

		locker = fu_device_locker_new_full(self,
						   (FuDeviceLockerFunc)fu_mm_device_sahara_open,
						   (FuDeviceLockerFunc)fu_mm_device_sahara_close,
						   error);
		if (locker == NULL)
			return FALSE;

		/* use sahara port to load firehose binary */
		if (!fu_sahara_loader_run(self->sahara_loader, firehose_prog, error))
			return FALSE;
	}
#endif // MM_CHECK_VERSION(1, 19, 1)
	else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "suitable port not found");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* download all files in the firehose-rawprogram manifest via Firehose */
	if (!fu_mm_device_firehose_write(self,
					 firehose_rawprogram_silo,
					 firehose_rawprogram_actions,
					 fu_progress_get_child(progress),
					 error))
		return FALSE;
	fu_progress_step_done(progress);

	/* flag as restart again, the module is switching to modem mode */
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_RESTART);
	return TRUE;
}

#if MM_CHECK_VERSION(1, 24, 0)
static gboolean
fu_mm_device_write_firmware_fdl(FuDevice *device, GBytes *fw, FuProgress *progress, GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE(device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	locker = fu_device_locker_new_full(device,
					   (FuDeviceLockerFunc)fu_mm_device_cinterion_fdl_open,
					   (FuDeviceLockerFunc)fu_mm_device_cinterion_fdl_close,
					   error);
	if (locker == NULL)
		return FALSE;

	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);

	return fu_cinterion_fdl_updater_write(self->cinterion_fdl_updater,
					      progress,
					      device,
					      fw,
					      error);
}
#endif // MM_CHECK_VERSION(1, 24, 0)

static gboolean
fu_mm_device_write_firmware(FuDevice *device,
			    FuFirmware *firmware,
			    FuProgress *progress,
			    FwupdInstallFlags flags,
			    GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE(device);
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GBytes) fw = NULL;

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* lock device */
	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;

	/* qmi pdc write operation */
	if (self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_QMI_PDC)
		return fu_mm_device_write_firmware_qmi_pdc(self,
							   fw,
							   &self->qmi_pdc_active_id,
							   error);

	/* mbim qdu write operation */
	if (self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_MBIM_QDU)
		return fu_mm_device_write_firmware_mbim_qdu(device, fw, progress, error);

	/* firehose operation */
	if (self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_FIREHOSE)
		return fu_mm_device_write_firmware_firehose(device, fw, progress, error);

#if MM_CHECK_VERSION(1, 24, 0)
	if (self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_CINTERION_FDL)
		return fu_mm_device_write_firmware_fdl(device, fw, progress, error);
#endif // MM_CHECK_VERSION(1, 24, 0)

	g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "unsupported update method");
	return FALSE;
}

static gboolean
fu_mm_device_set_quirk_kv(FuDevice *device, const gchar *key, const gchar *value, GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE(device);

	/* load from quirks */
	if (g_strcmp0(key, "ModemManagerBranchAtCommand") == 0) {
		self->branch_at = g_strdup(value);
		return TRUE;
	}

	if (g_strcmp0(key, "ModemManagerFirehoseProgFile") == 0) {
		self->firehose_prog_file = g_strdup(value);
		return TRUE;
	}

	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static gboolean
fu_mm_device_attach_qmi_pdc(FuMmDevice *self, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* ignore action if there is no active id specified */
	if (self->qmi_pdc_active_id == NULL)
		return TRUE;

	/* errors closing may be expected if the device really reboots itself */
	locker = fu_device_locker_new_full(self,
					   (FuDeviceLockerFunc)fu_mm_device_qmi_open,
					   (FuDeviceLockerFunc)fu_mm_device_qmi_close_no_error,
					   error);
	if (locker == NULL)
		return FALSE;

	if (!fu_qmi_pdc_updater_activate(self->qmi_pdc_updater, self->qmi_pdc_active_id, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_mm_device_attach_noop_idle(gpointer user_data)
{
	FuMmDevice *self = FU_MM_DEVICE(user_data);
	self->attach_idle = 0;
	g_signal_emit(self, signals[SIGNAL_ATTACH_FINISHED], 0);
	return G_SOURCE_REMOVE;
}

static gboolean
fu_mm_device_attach_qmi_pdc_idle(gpointer user_data)
{
	FuMmDevice *self = FU_MM_DEVICE(user_data);
	g_autoptr(GError) error = NULL;

	if (!fu_mm_device_attach_qmi_pdc(self, &error))
		g_warning("qmi-pdc attach operation failed: %s", error->message);
	else
		g_debug("qmi-pdc attach operation successful");

	self->attach_idle = 0;
	g_signal_emit(self, signals[SIGNAL_ATTACH_FINISHED], 0);
	return G_SOURCE_REMOVE;
}

static gboolean
fu_mm_device_cleanup(FuDevice *device,
		     FuProgress *progress,
		     FwupdInstallFlags install_flags,
		     GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE(device);

	/* restore default configuration */
	if (self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_MBIM_QDU) {
		if (!fu_mm_device_set_autosuspend_delay(self, 2000, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_mm_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE(device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* lock device */
	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;

	/* we want this attach operation to be triggered asynchronously, because the engine
	 * must learn that it has to wait for replug before we actually trigger the reset. */
	if (self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_QMI_PDC)
		self->attach_idle = g_idle_add((GSourceFunc)fu_mm_device_attach_qmi_pdc_idle, self);
	else
		self->attach_idle = g_idle_add((GSourceFunc)fu_mm_device_attach_noop_idle, self);

#if MM_CHECK_VERSION(1, 24, 0)
	/* devices with fdl-based update won't replug */
	if (self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_CINTERION_FDL)
		return TRUE;
#endif // MM_CHECK_VERSION(1, 24, 0)

	/* wait for re-probing after uninhibiting */
	fu_device_set_remove_delay(device, FU_MM_DEVICE_REMOVE_DELAY_REPROBE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_mm_device_setup_branch_at(FuMmDevice *self, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* nothing to do if there is no AT port available or
	 * ModemManagerBranchAtCommand quirk is not set */
	if (self->port_at == NULL || self->branch_at == NULL)
		return TRUE;

	if (fu_device_has_flag(self, FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Firmware branches are not supported if the devices is signed");
		return FALSE;
	}

	/* Create IO channel to send AT commands to the modem */
	locker = fu_device_locker_new_full(self,
					   (FuDeviceLockerFunc)fu_mm_device_io_open,
					   (FuDeviceLockerFunc)fu_mm_device_io_close,
					   error);
	if (locker == NULL)
		return FALSE;

	if (!fu_mm_device_at_cmd(self, self->branch_at, TRUE, error))
		return FALSE;

	if (fu_device_get_branch(self) != NULL)
		g_info("using firmware branch: %s", fu_device_get_branch(self));
	else
		g_info("using firmware branch: default");

	return TRUE;
}

static void
fu_mm_device_setup_secboot_status_quectel(FuMmDevice *self)
{
	const gchar *version = fu_device_get_version(FU_DEVICE(self));
	g_autofree gchar *name = NULL;

	const gchar *at_cmd[] = {"AT+QSECBOOT=\"status\"", "AT+QCFG=\"secbootstat\"", NULL};

	struct {
		const gchar *name;
		const gchar *version;
	} secboot[] = {{"EM05GF", "EM05GFAR07A07M1G_01.005.01.005"},
		       {"EM05CE", "EM05CEFCR08A16M1G_LNV"},
		       {NULL, NULL}};

	if (self->port_at != NULL) {
		g_autoptr(FuDeviceLocker) locker = NULL;
		g_autoptr(GError) error_local = NULL;

		/* Create IO channel to send AT commands to the modem */
		locker = fu_device_locker_new_full(self,
						   (FuDeviceLockerFunc)fu_mm_device_io_open,
						   (FuDeviceLockerFunc)fu_mm_device_io_close,
						   &error_local);
		if (locker == NULL) {
			g_debug("failed to open AT port: %s", error_local->message);
			return;
		}

		/* try to query sec boot status with AT commands */
		for (guint i = 0; at_cmd[i] != NULL; i++) {
			g_autoptr(GError) error_loop = NULL;
			if (!fu_mm_device_at_cmd(self, at_cmd[i], TRUE, &error_loop)) {
				g_debug("AT command failed (%s): %s",
					at_cmd[i],
					error_loop->message);
			} else {
				return;
			}
		}
	}

	/* find model name and compare with table from Quectel */
	if (version == NULL)
		return;
	name = g_strndup(version, 6);
	for (guint i = 0; secboot[i].name != NULL; i++) {
		if (g_strcmp0(name, secboot[i].name) == 0) {
			if (fu_version_compare(version,
					       secboot[i].version,
					       FWUPD_VERSION_FORMAT_PLAIN) >= 0) {
				fu_device_add_flag(FU_DEVICE(self),
						   FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
			} else {
				fu_device_add_flag(FU_DEVICE(self),
						   FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
			}
			return;
		}
	}
}

static void
fu_mm_device_setup_secboot_status(FuDevice *device)
{
	FuMmDevice *self = FU_MM_DEVICE(device);

	if (fu_device_has_vendor_id(device, "USB:0x2C7C") ||
	    fu_device_has_vendor_id(device, "PCI:0x1EAC"))
		fu_mm_device_setup_secboot_status_quectel(self);
	else if (fu_device_has_vendor_id(device, "USB:0x2CB7")) {
		fu_device_add_private_flag(FU_DEVICE(self),
					   FU_DEVICE_PRIVATE_FLAG_SAVE_INTO_BACKUP_REMOTE);
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	}
}

static gboolean
fu_mm_device_setup(FuDevice *device, GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE(device);
	g_autoptr(GError) error_local = NULL;

	fu_mm_device_setup_secboot_status(device);

	if (!fu_mm_device_setup_branch_at(self, &error_local))
		g_warning("Failed to set firmware branch: %s", error_local->message);

	return TRUE;
}

static void
fu_mm_device_incorporate(FuDevice *device, FuDevice *donor_device)
{
	FuMmDevice *self = FU_MM_DEVICE(device);
	FuMmDevice *donor = FU_MM_DEVICE(donor_device);

	g_return_if_fail(FU_IS_MM_DEVICE(self));
	g_return_if_fail(FU_IS_MM_DEVICE(donor));

	self->update_methods = fu_mm_device_get_update_methods(donor);
	self->detach_fastboot_at = g_strdup(donor->detach_fastboot_at);
	self->inhibition_uid = g_strdup(fu_mm_device_get_inhibition_uid(donor));
	g_set_object(&self->manager, donor->manager);
}

static void
fu_mm_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static void
fu_mm_device_init(FuMmDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_RUNTIME_VERSION);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_MD_SET_VERFMT);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ADD_INSTANCE_ID_REV);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_summary(FU_DEVICE(self), "Mobile broadband device");
	fu_device_add_icon(FU_DEVICE(self), "modem");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_MM_DEVICE_FLAG_DETACH_AT_FASTBOOT_HAS_NO_RESPONSE);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_MM_DEVICE_FLAG_UNINHIBIT_MM_AFTER_FASTBOOT_REBOOT);
	fu_device_register_private_flag(FU_DEVICE(self), FU_MM_DEVICE_FLAG_USE_BRANCH);
}

static void
fu_mm_device_finalize(GObject *object)
{
	FuMmDevice *self = FU_MM_DEVICE(object);
	if (self->udev_device != NULL)
		g_object_unref(self->udev_device);
	if (self->attach_idle)
		g_source_remove(self->attach_idle);
	if (self->qmi_pdc_active_id)
		g_array_unref(self->qmi_pdc_active_id);
	if (self->manager != NULL)
		g_object_unref(self->manager);
	if (self->omodem != NULL)
		g_object_unref(self->omodem);
	g_free(self->detach_fastboot_at);
	g_free(self->branch_at);
	g_free(self->port_at);
	g_free(self->port_qmi);
	g_free(self->port_mbim);
	g_free(self->port_qcdm);
	g_free(self->inhibition_uid);
	g_free(self->firmware_path);
	g_free(self->restore_firmware_path);
	g_free(self->firehose_prog_file);
	G_OBJECT_CLASS(fu_mm_device_parent_class)->finalize(object);
}

static void
fu_mm_device_class_init(FuMmDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_mm_device_finalize;
	device_class->setup = fu_mm_device_setup;
	device_class->reload = fu_mm_device_setup;
	device_class->to_string = fu_mm_device_to_string;
	device_class->set_quirk_kv = fu_mm_device_set_quirk_kv;
	device_class->probe = fu_mm_device_probe;
	device_class->detach = fu_mm_device_detach;
	device_class->write_firmware = fu_mm_device_write_firmware;
	device_class->attach = fu_mm_device_attach;
	device_class->cleanup = fu_mm_device_cleanup;
	device_class->set_progress = fu_mm_device_set_progress;
	device_class->incorporate = fu_mm_device_incorporate;

	/**
	 * FuMmDevice::attach-finished:
	 * @self: the #FuMmDevice instance that emitted the signal
	 *
	 * The ::attach-finished signal is emitted when the device has attached.
	 **/
	signals[SIGNAL_ATTACH_FINISHED] = g_signal_new("attach-finished",
						       G_TYPE_FROM_CLASS(object_class),
						       G_SIGNAL_RUN_LAST,
						       0,
						       NULL,
						       NULL,
						       g_cclosure_marshal_VOID__VOID,
						       G_TYPE_NONE,
						       0);
}

FuMmDevice *
fu_mm_device_new(FuContext *ctx, MMManager *manager, MMObject *omodem)
{
	FuMmDevice *self = g_object_new(FU_TYPE_MM_DEVICE, "context", ctx, NULL);
	self->manager = g_object_ref(manager);
	self->omodem = g_object_ref(omodem);
	return self;
}

FuMmDevice *
fu_mm_device_shadow_new(FuMmDevice *device)
{
	FuMmDevice *shadow_device = NULL;
	shadow_device = g_object_new(FU_TYPE_MM_DEVICE,
				     "context",
				     fu_device_get_context(FU_DEVICE(device)),
				     NULL);
	fu_device_incorporate(FU_DEVICE(shadow_device),
			      FU_DEVICE(device),
			      FU_DEVICE_INCORPORATE_FLAG_ALL);
	return shadow_device;
}

void
fu_mm_device_set_udev_device(FuMmDevice *self, FuUdevDevice *udev_device)
{
	g_return_if_fail(FU_IS_MM_DEVICE(self));
	g_return_if_fail(FU_IS_UDEV_DEVICE(udev_device));

	g_set_object(&self->udev_device, udev_device);

	/* copy across any vendor IDs */
	if (udev_device != NULL) {
		fu_device_incorporate(FU_DEVICE(self),
				      FU_DEVICE(udev_device),
				      FU_DEVICE_INCORPORATE_FLAG_VENDOR_IDS);
	}
}

FuMmDevice *
fu_mm_device_udev_new(FuContext *ctx, MMManager *manager, FuMmDevice *shadow_device)
{
	FuMmDevice *self = g_object_new(FU_TYPE_MM_DEVICE, "context", ctx, NULL);
	g_debug("creating udev-based mm device at %s",
		fu_device_get_physical_id(FU_DEVICE(shadow_device)));
	fu_device_incorporate(FU_DEVICE(self),
			      FU_DEVICE(shadow_device),
			      FU_DEVICE_INCORPORATE_FLAG_ALL);
	return self;
}

void
fu_mm_device_udev_add_port(FuMmDevice *self, const gchar *subsystem, const gchar *path)
{
	g_return_if_fail(FU_IS_MM_DEVICE(self));

	if (g_str_equal(subsystem, "usbmisc") && self->port_qmi == NULL) {
		g_debug("added QMI port %s (%s)", path, subsystem);
		self->port_qmi = g_strdup(path);
		return;
	}

	if (g_str_equal(subsystem, "tty") && self->port_at == NULL) {
		g_debug("added AT port %s (%s)", path, subsystem);
		self->port_at = g_strdup(path);
		return;
	}

	/* otherwise, ignore all other ports */
	g_debug("ignoring port %s (%s)", path, subsystem);
}
