/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <string.h>

#include "fu-mbim-qdu-updater.h"
#include "fu-mm-device.h"
#include "fu-mm-utils.h"
#include "fu-qmi-pdc-updater.h"

#if MM_CHECK_VERSION(1, 17, 2)
#include "fu-firehose-updater.h"
#endif

#include "fu-sahara-loader.h"

/* Amount of time for the modem to boot in fastboot mode. */
#define FU_MM_DEVICE_REMOVE_DELAY_RE_ENUMERATE 20000 /* ms */

/* Amount of time for the modem to be re-probed and exposed in MM after being
 * uninhibited. The timeout is long enough to cover the worst case, where the
 * modem boots without SIM card inserted (and therefore the initialization
 * may be very slow) and also where carrier config switching is explicitly
 * required (e.g. if switching from the default (DF) to generic (GC).*/
#define FU_MM_DEVICE_REMOVE_DELAY_REPROBE 120000 /* ms */

#define FU_MM_DEVICE_AT_RETRIES 3
#define FU_MM_DEVICE_AT_DELAY	3000 /* ms */

/* Amount of time for the modem to get firmware version */
#define MAX_WAIT_TIME_SECS 150 /* s */

/**
 * FU_MM_DEVICE_FLAG_DETACH_AT_FASTBOOT_HAS_NO_RESPONSE
 *
 * If no AT response is expected when entering fastboot mode.
 */
#define FU_MM_DEVICE_FLAG_DETACH_AT_FASTBOOT_HAS_NO_RESPONSE (1 << 0)

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
	gint port_at_ifnum;
	gint port_qmi_ifnum;
	gint port_mbim_ifnum;

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
#if MBIM_CHECK_VERSION(1, 25, 3)
	FuMbimQduUpdater *mbim_qdu_updater;
#endif /* MBIM_CHECK_VERSION(1,25,3) */

	/* firehose update handling */
	gchar *port_qcdm;
	gchar *port_edl;
	FuSaharaLoader *sahara_loader;
#if MM_CHECK_VERSION(1, 17, 2)
	FuFirehoseUpdater *firehose_updater;
#endif
	/* for sahara */
	FuUsbDevice *usb_device;

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
	if (self->port_at != NULL)
		fu_common_string_append_kv(str, idt, "AtPort", self->port_at);
	if (self->port_qmi != NULL)
		fu_common_string_append_kv(str, idt, "QmiPort", self->port_qmi);
	if (self->port_mbim != NULL)
		fu_common_string_append_kv(str, idt, "MbimPort", self->port_mbim);
	if (self->port_qcdm != NULL)
		fu_common_string_append_kv(str, idt, "QcdmPort", self->port_qcdm);
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

const gchar *
fu_mm_device_get_detach_fastboot_at(FuMmDevice *device)
{
	g_return_val_if_fail(FU_IS_MM_DEVICE(device), NULL);
	return device->detach_fastboot_at;
}

gint
fu_mm_device_get_port_at_ifnum(FuMmDevice *device)
{
	g_return_val_if_fail(FU_IS_MM_DEVICE(device), -1);
	return device->port_at_ifnum;
}

gint
fu_mm_device_get_port_qmi_ifnum(FuMmDevice *device)
{
	g_return_val_if_fail(FU_IS_MM_DEVICE(device), -1);
	return device->port_qmi_ifnum;
}

gint
fu_mm_device_get_port_mbim_ifnum(FuMmDevice *device)
{
	g_return_val_if_fail(FU_IS_MM_DEVICE(device), -1);
	return device->port_mbim_ifnum;
}

static gboolean
validate_firmware_update_method(MMModemFirmwareUpdateMethod methods, GError **error)
{
	static const MMModemFirmwareUpdateMethod supported_combinations[] = {
		MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT,
		MM_MODEM_FIRMWARE_UPDATE_METHOD_QMI_PDC | MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT,
#if MM_CHECK_VERSION(1, 17, 1)
		MM_MODEM_FIRMWARE_UPDATE_METHOD_MBIM_QDU,
#endif /* MM_CHECK_VERSION(1,17,1) */
#if MM_CHECK_VERSION(1, 17, 2)
		MM_MODEM_FIRMWARE_UPDATE_METHOD_FIREHOSE,
#endif
#if MM_CHECK_VERSION(1, 19, 1)
		MM_MODEM_FIRMWARE_UPDATE_METHOD_FIREHOSE | MM_MODEM_FIRMWARE_UPDATE_METHOD_SAHARA,
#endif
	};
	g_autofree gchar *methods_str = NULL;

	methods_str = mm_modem_firmware_update_method_build_string_from_mask(methods);
	for (guint i = 0; i < G_N_ELEMENTS(supported_combinations); i++) {
		if (supported_combinations[i] == methods) {
			g_debug("valid firmware update combination: %s", methods_str);
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
	GPtrArray *vendors;
	g_autoptr(MMFirmwareUpdateSettings) update_settings = NULL;
	g_autofree gchar *device_sysfs_path = NULL;
	g_autofree gchar *device_bus = NULL;

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
	if (!validate_firmware_update_method(self->update_methods, error))
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
#if MM_CHECK_VERSION(1, 17, 1)
	if (self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_MBIM_QDU) {
		for (guint i = 0; i < n_ports; i++) {
			if (ports[i].type == MM_MODEM_PORT_TYPE_MBIM) {
				self->port_mbim = g_strdup_printf("/dev/%s", ports[i].name);
				break;
			}
		}
		fu_device_add_protocol(device, "com.qualcomm.mbim_qdu");
	}
#endif /* MM_CHECK_VERSION(1,17,1) */
#if MM_CHECK_VERSION(1, 17, 2)
	if (self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_FIREHOSE) {
		for (guint i = 0; i < n_ports; i++) {
			if (ports[i].type == MM_MODEM_PORT_TYPE_QCDM)
				self->port_qcdm = g_strdup_printf("/dev/%s", ports[i].name);
			else if (ports[i].type == MM_MODEM_PORT_TYPE_MBIM)
				self->port_mbim = g_strdup_printf("/dev/%s", ports[i].name);
		}
		fu_device_add_protocol(device, "com.qualcomm.firehose");
	}
#endif

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

#if MM_CHECK_VERSION(1, 17, 1)
	/* a mbim port is required for mbim-qdu */
	if ((self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_MBIM_QDU) &&
	    (self->port_mbim == NULL)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "failed to find MBIM port");
		return FALSE;
	}

#endif /* MM_CHECK_VERSION(1,17,1) */
#if MM_CHECK_VERSION(1, 17, 2)
	/* a qcdm or mbim port is required for firehose */
	if ((self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_FIREHOSE) &&
	    (self->port_qcdm == NULL && self->port_mbim == NULL)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "failed to find QCDM port");
		return FALSE;
	}
#endif

	if (self->port_at != NULL) {
		fu_mm_utils_get_port_info(self->port_at,
					  &device_bus,
					  &device_sysfs_path,
					  &self->port_at_ifnum,
					  NULL);
	}
	if (self->port_qmi != NULL) {
		g_autofree gchar *qmi_device_sysfs_path = NULL;
		g_autofree gchar *qmi_device_bus = NULL;
		fu_mm_utils_get_port_info(self->port_qmi,
					  &qmi_device_bus,
					  &qmi_device_sysfs_path,
					  &self->port_qmi_ifnum,
					  NULL);
		if (device_sysfs_path == NULL && qmi_device_sysfs_path != NULL) {
			device_sysfs_path = g_steal_pointer(&qmi_device_sysfs_path);
		} else if (g_strcmp0(device_sysfs_path, qmi_device_sysfs_path) != 0) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "mismatched device sysfs path: %s != %s",
				    device_sysfs_path,
				    qmi_device_sysfs_path);
			return FALSE;
		}
		if (device_bus == NULL && qmi_device_bus != NULL) {
			device_bus = g_steal_pointer(&qmi_device_bus);
		} else if (g_strcmp0(device_bus, qmi_device_bus) != 0) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "mismatched device bus: %s != %s",
				    device_bus,
				    qmi_device_bus);
			return FALSE;
		}
	}
	if (self->port_mbim != NULL) {
		g_autofree gchar *mbim_device_sysfs_path = NULL;
		g_autofree gchar *mbim_device_bus = NULL;
		fu_mm_utils_get_port_info(self->port_mbim,
					  &mbim_device_bus,
					  &mbim_device_sysfs_path,
					  &self->port_mbim_ifnum,
					  NULL);
		if (device_sysfs_path == NULL && mbim_device_sysfs_path != NULL) {
			device_sysfs_path = g_steal_pointer(&mbim_device_sysfs_path);
		} else if (g_strcmp0(device_sysfs_path, mbim_device_sysfs_path) != 0) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "mismatched device sysfs path: %s != %s",
				    device_sysfs_path,
				    mbim_device_sysfs_path);
			return FALSE;
		}
		if (device_bus == NULL && mbim_device_bus != NULL) {
			device_bus = g_steal_pointer(&mbim_device_bus);
		} else if (g_strcmp0(device_bus, mbim_device_bus) != 0) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "mismatched device bus: %s != %s",
				    device_bus,
				    mbim_device_bus);
			return FALSE;
		}
	}

	if (self->port_qcdm != NULL) {
		g_autofree gchar *qcdm_device_sysfs_path = NULL;
		g_autofree gchar *qcdm_device_bus = NULL;
		fu_mm_utils_get_port_info(self->port_qcdm,
					  &qcdm_device_bus,
					  &qcdm_device_sysfs_path,
					  NULL,
					  NULL);
		if (device_sysfs_path == NULL && qcdm_device_sysfs_path != NULL) {
			device_sysfs_path = g_steal_pointer(&qcdm_device_sysfs_path);
		} else if (g_strcmp0(device_sysfs_path, qcdm_device_sysfs_path) != 0) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "mismatched device sysfs path: %s != %s",
				    device_sysfs_path,
				    qcdm_device_sysfs_path);
			return FALSE;
		}
		if (device_bus == NULL && qcdm_device_bus != NULL) {
			device_bus = g_steal_pointer(&qcdm_device_bus);
		} else if (g_strcmp0(device_bus, qcdm_device_bus) != 0) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "mismatched device bus: %s != %s",
				    device_bus,
				    qcdm_device_bus);
			return FALSE;
		}
	}

	/* if no device sysfs file, error out */
	if (device_sysfs_path == NULL || device_bus == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "failed to find device details");
		return FALSE;
	}

	/* add properties to fwupd device */
	fu_device_set_physical_id(device, device_sysfs_path);
	if (mm_modem_get_manufacturer(modem) != NULL)
		fu_device_set_vendor(device, mm_modem_get_manufacturer(modem));
	if (mm_modem_get_model(modem) != NULL)
		fu_device_set_name(device, mm_modem_get_model(modem));
	fu_device_set_version(device, version);
	for (guint i = 0; device_ids[i] != NULL; i++)
		fu_device_add_instance_id(device, device_ids[i]);
	vendors = fu_device_get_vendor_ids(device);
	if (vendors == NULL || vendors->len == 0) {
		g_autofree gchar *path = NULL;
		g_autofree gchar *value_str = NULL;
		g_autoptr(GError) error_local = NULL;

		if (g_strcmp0(device_bus, "USB") == 0)
			path = g_build_filename(device_sysfs_path, "idVendor", NULL);
		else if (g_strcmp0(device_bus, "PCI") == 0)
			path = g_build_filename(device_sysfs_path, "vendor", NULL);

		if (path == NULL) {
			g_warning("failed to set vendor ID: unsupported bus: %s", device_bus);
		} else if (!g_file_get_contents(path, &value_str, NULL, &error_local)) {
			g_warning("failed to set vendor ID: %s", error_local->message);
		} else {
			guint64 value_int;

			/* note: the string value may be prefixed with '0x' (e.g. when reading
			 * the PCI 'vendor' attribute, or not prefixed with anything, as in the
			 * USB 'idVendor' attribute. */
			value_int = g_ascii_strtoull(value_str, NULL, 16);
			if (value_int > G_MAXUINT16) {
				g_warning("failed to set vendor ID: invalid value: %s", value_str);
			} else {
				g_autofree gchar *vendor_id =
				    g_strdup_printf("%s:0x%04X", device_bus, (guint)value_int);
				fu_device_add_vendor_id(device, vendor_id);
			}
		}
	}

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

	if (self->omodem) {
		return fu_mm_device_probe_default(device, error);
	} else {
		return fu_mm_device_probe_udev(device, error);
	}
}

#if MM_CHECK_VERSION(1, 17, 2)
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
	self->io_channel = fu_io_channel_new_file(self->port_qcdm, error);
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
	if (g_getenv("FWUPD_MODEM_MANAGER_VERBOSE") != NULL)
		fu_common_dump_bytes(G_LOG_DOMAIN, "writing", qcdm_req);
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
	if (g_getenv("FWUPD_MODEM_MANAGER_VERBOSE") != NULL)
		fu_common_dump_bytes(G_LOG_DOMAIN, "read", qcdm_res);

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
#endif /* MM_CHECK_VERSION(1,17,2) */

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
	if (g_getenv("FWUPD_MODEM_MANAGER_VERBOSE") != NULL)
		fu_common_dump_bytes(G_LOG_DOMAIN, "writing", at_req);
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
		g_debug("No response expected for AT command: '%s', assuming succeed", helper->cmd);
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
	if (g_getenv("FWUPD_MODEM_MANAGER_VERBOSE") != NULL)
		fu_common_dump_bytes(G_LOG_DOMAIN, "read", at_res);
	buf = g_bytes_get_data(at_res, &bufsz);
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
				g_debug("Firmware branch reported as '%s'", parts[j]);
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
	self->io_channel = fu_io_channel_new_file(self->port_at, error);
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
fu_mm_file_info_free(FuMmFileInfo *file_info)
{
	g_clear_pointer(&file_info->digest, g_array_unref);
	g_free(file_info->filename);
	g_bytes_unref(file_info->bytes);
	g_free(file_info);
}

typedef struct {
	FuMmDevice *device;
	GError *error;
	GPtrArray *file_infos;
} FuMmArchiveIterateCtx;

static gboolean
fu_mm_should_be_active(const gchar *version, const gchar *filename)
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
fu_mm_qmi_pdc_archive_iterate_mcfg(FuArchive *archive,
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
	    fu_mm_should_be_active(fu_device_get_version(FU_DEVICE(ctx->device)), filename);
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
fu_mm_device_write_firmware_qmi_pdc(FuDevice *device,
				    GBytes *fw,
				    GArray **active_id,
				    GError **error)
{
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GPtrArray) file_infos =
	    g_ptr_array_new_with_free_func((GDestroyNotify)fu_mm_file_info_free);
	gint active_i = -1;
	FuMmArchiveIterateCtx archive_context = {
	    .device = FU_MM_DEVICE(device),
	    .error = NULL,
	    .file_infos = file_infos,
	};

	/* decompress entire archive ahead of time */
	archive = fu_archive_new(fw, FU_ARCHIVE_FLAG_IGNORE_PATH, error);
	if (archive == NULL)
		return FALSE;

	/* boot to fastboot mode */
	locker = fu_device_locker_new_full(device,
					   (FuDeviceLockerFunc)fu_mm_device_qmi_open,
					   (FuDeviceLockerFunc)fu_mm_device_qmi_close,
					   error);
	if (locker == NULL)
		return FALSE;

	/* process the list of MCFG files to write */
	if (!fu_archive_iterate(archive,
				fu_mm_qmi_pdc_archive_iterate_mcfg,
				&archive_context,
				error))
		return FALSE;

	for (guint i = 0; i < file_infos->len; i++) {
		FuMmFileInfo *file_info = g_ptr_array_index(file_infos, i);
		file_info->digest =
		    fu_qmi_pdc_updater_write(archive_context.device->qmi_pdc_updater,
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

#if MM_CHECK_VERSION(1, 17, 1) && MBIM_CHECK_VERSION(1, 25, 3)
typedef struct {
	FuDevice *device;
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
	FuMmDevice *self = FU_MM_DEVICE(ctx->device);

	g_clear_error(&ctx->error);
	ctx->version = fu_mbim_qdu_updater_check_ready(self->mbim_qdu_updater, &ctx->error);
	g_main_loop_quit(ctx->mainloop);

	return G_SOURCE_REMOVE;
}

static gchar *
fu_mm_device_get_firmware_version_mbim(FuDevice *device, GError **error)
{
	GTimer *timer = g_timer_new();
	g_autoptr(GMainLoop) mainloop = g_main_loop_new(NULL, FALSE);
	FuMmGetFirmwareVersionCtx ctx = {
	    .device = device,
	    .mainloop = mainloop,
	    .version = NULL,
	    .error = NULL,
	};

	while (ctx.version == NULL && g_timer_elapsed(timer, NULL) < MAX_WAIT_TIME_SECS) {
		g_autoptr(FuDeviceLocker) locker = NULL;

		g_clear_error(&ctx.error);
		locker = fu_device_locker_new_full(device,
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
	int fd;
	g_autoptr(FuIOChannel) io = NULL;

	fd = open(fn, O_WRONLY);
	if (fd < 0) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "could not open %s", fn);
		return FALSE;
	}
	io = fu_io_channel_unix_new(fd);
	return fu_io_channel_write_raw(io,
				       (const guint8 *)buf,
				       strlen(buf),
				       1000,
				       FU_IO_CHANNEL_FLAG_NONE,
				       error);
}

static gboolean
fu_mm_device_write_firmware_mbim_qdu(FuDevice *device,
				     GBytes *fw,
				     FuProgress *progress,
				     GError **error)
{
	GBytes *data;
	XbNode *part = NULL;
	const gchar *filename = NULL;
	const gchar *csum;
	FuMmDevice *self = FU_MM_DEVICE(device);
	g_autofree gchar *device_sysfs_path = NULL;
	g_autofree gchar *autosuspend_delay_filename = NULL;
	g_autofree gchar *csum_actual = NULL;
	g_autofree gchar *version = NULL;
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
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
	data = fu_archive_lookup_by_fn(archive, "flashfile.xml", error);
	if (data == NULL)
		return FALSE;
	if (!xb_builder_source_load_bytes(source, data, XB_BUILDER_SOURCE_FLAG_NONE, error))
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
	data = fu_archive_lookup_by_fn(archive, filename, error);
	if (data == NULL)
		return FALSE;
	csum_actual = g_compute_checksum_for_bytes(G_CHECKSUM_MD5, data);
	if (g_strcmp0(csum, csum_actual) != 0) {
		g_debug("[%s] MD5 not matched", filename);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "[%s] MD5 not matched",
			    filename);
		return FALSE;
	} else {
		g_debug("[%s] MD5 matched", filename);
	}

	/* autosuspend delay updated for a proper firmware update */
	fu_mm_utils_get_port_info(self->port_mbim, NULL, &device_sysfs_path, NULL, NULL);
	autosuspend_delay_filename =
	    g_build_filename(device_sysfs_path, "/power/autosuspend_delay_ms", NULL);
	if (!fu_mm_device_writeln(autosuspend_delay_filename, "10000", error))
		return FALSE;

	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	fu_mbim_qdu_updater_write(self->mbim_qdu_updater, filename, data, device, progress, error);
	if (!fu_device_locker_close(locker, error))
		return FALSE;

	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_READ);
	version = fu_mm_device_get_firmware_version_mbim(device, error);
	if (version == NULL)
		return FALSE;

	return TRUE;
}

#endif /* MM_CHECK_VERSION(1,17,1) && MBIM_CHECK_VERSION(1,25,3) */

#if MM_CHECK_VERSION(1, 17, 2)
static gboolean
fu_mm_find_device_file(FuDevice *device, gpointer userdata, GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE(device);
	const gchar *subsystem = (const gchar *)userdata;

	return fu_mm_utils_find_device_file(fu_device_get_physical_id(FU_DEVICE(self)),
					    subsystem,
					    &self->port_edl,
					    error);
}

static gboolean
fu_mm_device_find_edl_port(FuDevice *device, const gchar *subsystem, GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE(device);

	g_clear_pointer(&self->port_edl, g_free);

	return fu_device_retry_full(device,
				    fu_mm_find_device_file,
				    30,
				    250,
				    (gpointer)subsystem,
				    error);
}

static gboolean
fu_mm_device_qcdm_switch_to_edl(FuDevice *device, gpointer userdata, GError **error)
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
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE)) {
			return fu_mm_device_find_edl_port(device, "wwan", error);
		}
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

#if MM_CHECK_VERSION(1, 19, 1) && MBIM_CHECK_VERSION(1, 25, 7)
static gboolean
fu_mm_device_sahara_open(FuMmDevice *self, GError **error)
{
	self->sahara_loader = fu_sahara_loader_new();
	return fu_sahara_loader_open(self->sahara_loader, self->usb_device, error);
}

static gboolean
fu_mm_device_sahara_close(FuMmDevice *self, GError **error)
{
	g_autoptr(FuSaharaLoader) loader = NULL;

	loader = g_steal_pointer(&self->sahara_loader);
	return fu_sahara_loader_close(loader, error);
}
#endif // MM_CHECK_VERSION(1, 19, 1) && MBIM_CHECK_VERSION(1, 25, 7)

static gboolean
fu_mm_setup_firmware_dir(FuMmDevice *self, GError **error)
{
	g_autofree gchar *cachedir = NULL;
	g_autofree gchar *mm_fw_dir = NULL;

	/* create a directory to store firmware files for modem-manager plugin */
	cachedir = fu_common_get_path(FU_PATH_KIND_CACHEDIR_PKG);
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

	if (!fu_common_set_firmware_search_path(mm_fw_dir, error))
		return FALSE;

	self->firmware_path = g_steal_pointer(&mm_fw_dir);

	return TRUE;
}

static gboolean
fu_mm_copy_firehose_prog(FuMmDevice *self, GBytes *prog, GError **error)
{
	g_autofree gchar *qcom_fw_dir = NULL;
	g_autofree gchar *firehose_file_path = NULL;

	qcom_fw_dir = g_build_filename(self->firmware_path, "qcom", NULL);
	if (!fu_common_mkdir_parent(qcom_fw_dir, error))
		return FALSE;

	firehose_file_path = g_build_filename(qcom_fw_dir, "prog_firehose_sdx24.mbn", NULL);

	if (!fu_common_set_contents_bytes(firehose_file_path, prog, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_mm_prepare_firmware_search_path(FuMmDevice *self, GError **error)
{
	self->restore_firmware_path = fu_common_get_firmware_search_path(NULL);

	return fu_mm_setup_firmware_dir(self, error);
}

static gboolean
fu_mm_restore_firmware_search_path(FuMmDevice *self, GError **error)
{
	if (self->restore_firmware_path != NULL && strlen(self->restore_firmware_path) > 0)
		return fu_common_set_firmware_search_path(self->restore_firmware_path, error);

	return fu_common_reset_firmware_search_path(error);
}

static gboolean
fu_mm_device_write_firmware_firehose(FuDevice *device,
				     GBytes *fw,
				     FuProgress *progress,
				     GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE(device);
	GBytes *firehose_rawprogram;
	GBytes *firehose_prog;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(XbSilo) firehose_rawprogram_silo = NULL;
	g_autoptr(GPtrArray) firehose_rawprogram_actions = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 1);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 10);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 90);

	/* decompress entire archive ahead of time */
	archive = fu_archive_new(fw, FU_ARCHIVE_FLAG_IGNORE_PATH, error);
	if (archive == NULL)
		return FALSE;

	/* lookup and validate firehose-rawprogram actions */
	firehose_rawprogram = fu_archive_lookup_by_fn(archive, "firehose-rawprogram.xml", error);
	if (firehose_rawprogram == NULL)
		return FALSE;
	if (!fu_firehose_validate_rawprogram(firehose_rawprogram,
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
	 * In case of MHI PCI modems, the mhi_wwan driver reads the firehose binary from the
	 * firmware-loader and writes it to the modem.
	 **/
	if (g_strrstr(fu_device_get_physical_id(device), "pci") && self->port_qcdm != NULL) {
		/* modify firmware search path and restore it before function returns */
		locker = fu_device_locker_new_full(
		    self,
		    (FuDeviceLockerFunc)fu_mm_prepare_firmware_search_path,
		    (FuDeviceLockerFunc)fu_mm_restore_firmware_search_path,
		    error);
		if (locker == NULL)
			return FALSE;

		/* firehose modems that use mhi_pci drivers require firehose binary
		 * to be present in the firmware-loader search path. */
		if (!fu_mm_copy_firehose_prog(self, firehose_prog, error))
			return FALSE;
		/* trigger emergency download mode, up to 30s retrying until the QCDM
		 * port goes away; this takes us to the EDL (embedded downloader) execution
		 * environment */
		if (!fu_device_retry_full(FU_DEVICE(self),
					  fu_mm_device_qcdm_switch_to_edl,
					  30,
					  1000,
					  NULL,
					  error))
			return FALSE;

		g_debug("found edl port: %s", self->port_edl);
	}
#if MM_CHECK_VERSION(1, 19, 1) && MBIM_CHECK_VERSION(1, 25, 7)
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
#endif // MM_CHECK_VERSION(1, 19, 1) && MBIM_CHECK_VERSION(1, 25, 7)
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

#endif /* MM_CHECK_VERSION(1,17,2) */

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
		return fu_mm_device_write_firmware_qmi_pdc(device,
							   fw,
							   &self->qmi_pdc_active_id,
							   error);

#if MM_CHECK_VERSION(1, 17, 1) && MBIM_CHECK_VERSION(1, 25, 3)
	/* mbim qdu write operation */
	if (self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_MBIM_QDU)
		return fu_mm_device_write_firmware_mbim_qdu(device, fw, progress, error);

#endif /* MM_CHECK_VERSION(1,17,1) && MBIM_CHECK_VERSION(1,25,3) */
#if MM_CHECK_VERSION(1, 17, 2)
	/* firehose operation */
	if (self->update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_FIREHOSE)
		return fu_mm_device_write_firmware_firehose(device, fw, progress, error);
#endif

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

	/* failed */
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "quirk key not supported");
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

	/* wait for re-probing after uninhibiting */
	fu_device_set_remove_delay(device, FU_MM_DEVICE_REMOVE_DELAY_REPROBE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_mm_device_setup_branch_at(FuMmDevice *self, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* Create IO channel to send AT commands to the modem */
	locker = fu_device_locker_new_full(self,
					   (FuDeviceLockerFunc)fu_mm_device_io_open,
					   (FuDeviceLockerFunc)fu_mm_device_io_close,
					   error);
	if (locker == NULL)
		return FALSE;

	/* firmware branch AT command may fail if not implemented,
	 * clear error if not supported */
	if (self->branch_at != NULL) {
		g_autoptr(GError) error_branch = NULL;
		if (!fu_mm_device_at_cmd(self, self->branch_at, TRUE, &error_branch))
			g_debug("unable to get firmware branch: %s", error_branch->message);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_mm_device_setup(FuDevice *device, GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE(device);

	if (self->port_at != NULL) {
		if (!fu_mm_device_setup_branch_at(self, error))
			return FALSE;
	}
	if (fu_device_get_branch(device) != NULL)
		g_debug("using firmware branch: %s", fu_device_get_branch(device));
	else
		g_debug("using firmware branch: default");

	return TRUE;
}

static void
fu_mm_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2); /* detach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94);	/* write */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2); /* attach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2);	/* reload */
}

static void
fu_mm_device_init(FuMmDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_USE_RUNTIME_VERSION);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_MD_SET_VERFMT);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_summary(FU_DEVICE(self), "Mobile broadband device");
	fu_device_add_icon(FU_DEVICE(self), "network-modem");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_MM_DEVICE_FLAG_DETACH_AT_FASTBOOT_HAS_NO_RESPONSE,
					"detach-at-fastboot-has-no-response");
}

static void
fu_mm_device_finalize(GObject *object)
{
	FuMmDevice *self = FU_MM_DEVICE(object);
	if (self->usb_device != NULL)
		g_object_unref(self->usb_device);
	if (self->attach_idle)
		g_source_remove(self->attach_idle);
	if (self->qmi_pdc_active_id)
		g_array_unref(self->qmi_pdc_active_id);
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
	G_OBJECT_CLASS(fu_mm_device_parent_class)->finalize(object);
}

static void
fu_mm_device_class_init(FuMmDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_mm_device_finalize;
	klass_device->setup = fu_mm_device_setup;
	klass_device->reload = fu_mm_device_setup;
	klass_device->to_string = fu_mm_device_to_string;
	klass_device->set_quirk_kv = fu_mm_device_set_quirk_kv;
	klass_device->probe = fu_mm_device_probe;
	klass_device->detach = fu_mm_device_detach;
	klass_device->write_firmware = fu_mm_device_write_firmware;
	klass_device->attach = fu_mm_device_attach;
	klass_device->set_progress = fu_mm_device_set_progress;

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
	self->port_at_ifnum = -1;
	self->port_qmi_ifnum = -1;
	self->port_mbim_ifnum = -1;
	return self;
}

FuPluginMmInhibitedDeviceInfo *
fu_plugin_mm_inhibited_device_info_new(FuMmDevice *device)
{
	FuPluginMmInhibitedDeviceInfo *info;

	info = g_new0(FuPluginMmInhibitedDeviceInfo, 1);
	info->physical_id = g_strdup(fu_device_get_physical_id(FU_DEVICE(device)));
	info->vendor = g_strdup(fu_device_get_vendor(FU_DEVICE(device)));
	info->name = g_strdup(fu_device_get_name(FU_DEVICE(device)));
	info->version = g_strdup(fu_device_get_version(FU_DEVICE(device)));
	info->guids = fu_device_get_guids(FU_DEVICE(device));
	info->update_methods = fu_mm_device_get_update_methods(device);
	info->detach_fastboot_at = g_strdup(fu_mm_device_get_detach_fastboot_at(device));
	info->port_at_ifnum = fu_mm_device_get_port_at_ifnum(device);
	info->port_qmi_ifnum = fu_mm_device_get_port_qmi_ifnum(device);
	info->port_mbim_ifnum = fu_mm_device_get_port_mbim_ifnum(device);
	info->inhibited_uid = g_strdup(fu_mm_device_get_inhibition_uid(device));

	return info;
}

void
fu_plugin_mm_inhibited_device_info_free(FuPluginMmInhibitedDeviceInfo *info)
{
	g_free(info->inhibited_uid);
	g_free(info->physical_id);
	g_free(info->vendor);
	g_free(info->name);
	g_free(info->version);
	if (info->guids)
		g_ptr_array_unref(info->guids);
	g_free(info->detach_fastboot_at);
	g_free(info);
}

FuUsbDevice *
fu_mm_device_get_usb_device(FuMmDevice *self)
{
	g_return_val_if_fail(FU_IS_MM_DEVICE(self), NULL);
	return self->usb_device;
}

void
fu_mm_device_set_usb_device(FuMmDevice *self, FuUsbDevice *usb_device)
{
	g_return_if_fail(FU_IS_MM_DEVICE(self));
	g_return_if_fail(FU_IS_USB_DEVICE(usb_device));
	g_set_object(&self->usb_device, usb_device);
}

FuMmDevice *
fu_mm_device_udev_new(FuContext *ctx, MMManager *manager, FuPluginMmInhibitedDeviceInfo *info)
{
	FuMmDevice *self = g_object_new(FU_TYPE_MM_DEVICE, "context", ctx, NULL);
	g_debug("creating udev-based mm device at %s", info->physical_id);
	self->manager = g_object_ref(manager);
	fu_device_set_physical_id(FU_DEVICE(self), info->physical_id);
	fu_device_set_vendor(FU_DEVICE(self), info->vendor);
	fu_device_set_name(FU_DEVICE(self), info->name);
	fu_device_set_version(FU_DEVICE(self), info->version);
	self->update_methods = info->update_methods;
	self->detach_fastboot_at = g_strdup(info->detach_fastboot_at);
	self->port_at_ifnum = info->port_at_ifnum;
	self->port_qmi_ifnum = info->port_qmi_ifnum;

	for (guint i = 0; i < info->guids->len; i++)
		fu_device_add_guid(FU_DEVICE(self), g_ptr_array_index(info->guids, i));

	return self;
}

void
fu_mm_device_udev_add_port(FuMmDevice *self, const gchar *subsystem, const gchar *path, gint ifnum)
{
	g_return_if_fail(FU_IS_MM_DEVICE(self));

	if (g_str_equal(subsystem, "usbmisc") && self->port_qmi == NULL && ifnum >= 0 &&
	    ifnum == self->port_qmi_ifnum) {
		g_debug("added QMI port %s (%s)", path, subsystem);
		self->port_qmi = g_strdup(path);
		return;
	}

	if (g_str_equal(subsystem, "tty") && self->port_at == NULL && ifnum >= 0 &&
	    ifnum == self->port_at_ifnum) {
		g_debug("added AT port %s (%s)", path, subsystem);
		self->port_at = g_strdup(path);
		return;
	}

	/* otherwise, ignore all other ports */
	g_debug("ignoring port %s (%s)", path, subsystem);
}
