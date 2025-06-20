/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-mm-common.h"
#include "fu-mm-device.h"

/**
 * FuMmDevice
 *
 * A modem manager device.
 *
 * See also: #FuUdevDevice
 */

/* not strictly last, but the last we care about */
#define MM_MODEM_PORT_TYPE_LAST (MM_MODEM_PORT_TYPE_IGNORED + 1)

typedef struct {
	gboolean inhibited;
	gchar *branch_at;
	gchar *inhibition_uid;
	gchar *port[MM_MODEM_PORT_TYPE_LAST];
} FuMmDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuMmDevice, fu_mm_device, FU_TYPE_UDEV_DEVICE);

#define GET_PRIVATE(o) (fu_mm_device_get_instance_private(o))

#define FU_MM_DEVICE_AT_RETRIES 3

#define FU_MM_DEVICE_AT_DELAY 3000 /* ms */

enum { PROP_0, PROP_INHIBITED, PROP_LAST };

static void
fu_mm_device_set_branch_at(FuMmDevice *self, const gchar *branch_at)
{
	FuMmDevicePrivate *priv = GET_PRIVATE(self);
	if (g_strcmp0(priv->branch_at, branch_at) == 0)
		return;
	g_free(priv->branch_at);
	priv->branch_at = g_strdup(branch_at);
}

static void
fu_mm_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuMmDevice *self = FU_MM_DEVICE(device);
	FuMmDevicePrivate *priv = GET_PRIVATE(self);
	fwupd_codec_string_append(str, idt, "BranchAt", priv->branch_at);
	fwupd_codec_string_append_bool(str, idt, "Inhibited", priv->inhibited);
	fwupd_codec_string_append(str, idt, "InhibitionUid", priv->inhibition_uid);
	for (guint i = 0; i < MM_MODEM_PORT_TYPE_LAST; i++) {
		if (priv->port[i] != NULL) {
			g_autofree gchar *title =
			    g_strdup_printf("Port[%s]", fu_mm_device_port_type_to_string(i));
			fwupd_codec_string_append(str, idt, title, priv->port[i]);
		}
	}
}

const gchar *
fu_mm_device_get_inhibition_uid(FuMmDevice *self)
{
	FuMmDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_MM_DEVICE(self), NULL);
	return priv->inhibition_uid;
}

void
fu_mm_device_set_inhibited(FuMmDevice *self, gboolean inhibited)
{
	FuMmDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_MM_DEVICE(self));
	if (priv->inhibited == inhibited)
		return;
	priv->inhibited = inhibited;
	g_object_notify(G_OBJECT(self), "inhibited");
}

gboolean
fu_mm_device_get_inhibited(FuMmDevice *self)
{
	FuMmDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_MM_DEVICE(self), FALSE);
	return priv->inhibited;
}

gboolean
fu_mm_device_set_device_file(FuMmDevice *self, MMModemPortType port_type, GError **error)
{
	FuMmDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_MM_DEVICE(self), FALSE);
	g_return_val_if_fail(port_type < MM_MODEM_PORT_TYPE_LAST, FALSE);
	if (priv->port[port_type] == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no port for %s",
			    fu_mm_device_port_type_to_string(port_type));
		return FALSE;
	}
	fu_udev_device_set_device_file(FU_UDEV_DEVICE(self), priv->port[port_type]);
	return TRUE;
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

gboolean
fu_mm_device_set_autosuspend_delay(FuMmDevice *self, guint timeout_ms, GError **error)
{
	g_autofree gchar *autosuspend_delay_filename = NULL;
	g_autofree gchar *buf = g_strdup_printf("%u", timeout_ms);

	/* autosuspend delay updated for a proper firmware update */
	autosuspend_delay_filename = g_build_filename(fu_device_get_physical_id(FU_DEVICE(self)),
						      "/power/autosuspend_delay_ms",
						      NULL);
	if (!g_file_test(autosuspend_delay_filename, G_FILE_TEST_EXISTS)) {
		g_debug("%s does not exist, so skipping", autosuspend_delay_filename);
		return TRUE;
	}
	return fu_mm_device_writeln(autosuspend_delay_filename, buf, error);
}

void
fu_mm_device_add_instance_id(FuMmDevice *self, const gchar *device_id)
{
	g_autofree gchar *subsys_pid = NULL;
	g_autofree gchar *subsys_vid = NULL;
	g_auto(GStrv) instancestrs = NULL;
	g_auto(GStrv) subsys_instancestr = NULL;

	/* add vendor ID */
	if (g_pattern_match_simple("???\\VID_????", device_id) ||
	    g_pattern_match_simple("???\\VEN_????", device_id)) {
		g_autofree gchar *prefix = g_strndup(device_id, 3);
		g_autofree gchar *vendor_id = g_strdup_printf("%s:0x%s", prefix, device_id + 8);
		fu_device_add_vendor_id(FU_DEVICE(self), vendor_id);
	}

	/* parse the ModemManager InstanceID lookalike */
	subsys_instancestr = g_strsplit(device_id, "\\", 2);
	if (subsys_instancestr[1] == NULL)
		return;
	instancestrs = g_strsplit(subsys_instancestr[1], "&", -1);
	for (guint i = 0; instancestrs[i] != NULL; i++) {
		g_auto(GStrv) kv = g_strsplit(instancestrs[i], "_", 2);
		if (g_strcmp0(kv[0], "VID") == 0 || g_strcmp0(kv[0], "PID") == 0 ||
		    g_strcmp0(kv[0], "REV") == 0 || g_strcmp0(kv[0], "NAME") == 0 ||
		    g_strcmp0(kv[0], "CARRIER") == 0) {
			fu_device_add_instance_str(FU_DEVICE(self), kv[0], kv[1]);
		} else if (g_strcmp0(kv[0], "SSVID") == 0 && subsys_vid == NULL) {
			subsys_vid = g_strdup(kv[1]);
		} else if (g_strcmp0(kv[0], "SSPID") == 0 && subsys_pid == NULL) {
			subsys_pid = g_strdup(kv[1]);
		} else {
			g_debug("ignoring instance attribute '%s'", instancestrs[i]);
		}
	}

	/* convert nonstandard SSVID+SSPID to SUBSYS */
	if (subsys_vid != NULL && subsys_pid != NULL) {
		g_autofree gchar *subsys = g_strdup_printf("%s%s", subsys_vid, subsys_pid);
		fu_device_add_instance_str(FU_DEVICE(self), "SUBSYS", subsys);
	}

	/* add all possible instance IDs */
	fu_device_build_instance_id_full(FU_DEVICE(self),
					 FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					 NULL,
					 subsys_instancestr[0],
					 "VID",
					 NULL);
	fu_device_build_instance_id(FU_DEVICE(self),
				    NULL,
				    subsys_instancestr[0],
				    "VID",
				    "PID",
				    NULL);
	fu_device_build_instance_id(FU_DEVICE(self),
				    NULL,
				    subsys_instancestr[0],
				    "VID",
				    "PID",
				    "NAME",
				    NULL);
	fu_device_build_instance_id(FU_DEVICE(self),
				    NULL,
				    subsys_instancestr[0],
				    "VID",
				    "PID",
				    "SUBSYS",
				    NULL);
	fu_device_build_instance_id(FU_DEVICE(self),
				    NULL,
				    subsys_instancestr[0],
				    "VID",
				    "PID",
				    "SUBSYS",
				    "NAME",
				    NULL);
	if (fu_device_has_private_flag(FU_DEVICE(self),
				       FU_DEVICE_PRIVATE_FLAG_ADD_INSTANCE_ID_REV)) {
		fu_device_build_instance_id(FU_DEVICE(self),
					    NULL,
					    subsys_instancestr[0],
					    "VID",
					    "PID",
					    "REV",
					    NULL);
		fu_device_build_instance_id(FU_DEVICE(self),
					    NULL,
					    subsys_instancestr[0],
					    "VID",
					    "PID",
					    "REV",
					    "NAME",
					    NULL);
		fu_device_build_instance_id(FU_DEVICE(self),
					    NULL,
					    subsys_instancestr[0],
					    "VID",
					    "PID",
					    "SUBSYS",
					    "REV",
					    NULL);
	}
	if (!fu_device_has_private_flag(FU_DEVICE(self), FU_MM_DEVICE_FLAG_USE_BRANCH)) {
		fu_device_build_instance_id(FU_DEVICE(self),
					    NULL,
					    subsys_instancestr[0],
					    "VID",
					    "PID",
					    "CARRIER",
					    NULL);
		if (fu_device_has_private_flag(FU_DEVICE(self),
					       FU_DEVICE_PRIVATE_FLAG_ADD_INSTANCE_ID_REV)) {
			fu_device_build_instance_id(FU_DEVICE(self),
						    NULL,
						    subsys_instancestr[0],
						    "VID",
						    "PID",
						    "REV",
						    "CARRIER",
						    NULL);
			fu_device_build_instance_id(FU_DEVICE(self),
						    NULL,
						    subsys_instancestr[0],
						    "VID",
						    "PID",
						    "SUBSYS",
						    "REV",
						    "CARRIER",
						    NULL);
		}
	}
}

static void
fu_mm_device_add_port(FuMmDevice *self, MMModemPortType port_type, const gchar *device_file)
{
	FuMmDevicePrivate *priv = GET_PRIVATE(self);
	if (port_type >= MM_MODEM_PORT_TYPE_LAST)
		return;
	if (priv->port[port_type] != NULL)
		return;
	priv->port[port_type] = g_strdup(device_file);
}

gboolean
fu_mm_device_probe_from_omodem(FuMmDevice *self, MMObject *omodem, GError **error)
{
	FuMmDevicePrivate *priv = GET_PRIVATE(self);
	MMModemFirmware *modem_fw = mm_object_peek_modem_firmware(omodem);
	MMModem *modem = mm_object_peek_modem(omodem);
	MMModemPortInfo *used_ports = NULL;
	guint n_used_ports = 0;
#if MM_CHECK_VERSION(1, 26, 0)
	MMModemPortInfo *ignored_ports = NULL;
	guint n_ignored_ports = 0;
#endif // MM_CHECK_VERSION(1, 26, 0)
	const gchar **device_ids;
	const gchar *sysfs_path;
	const gchar *version;
	g_autoptr(MMFirmwareUpdateSettings) update_settings = NULL;

	/* inhibition uid is the modem interface 'Device' property, which may
	 * be the device sysfs path or a different user-provided id */
	priv->inhibition_uid = mm_modem_dup_device(modem);

	/* get the sysfs path for the MM physical device */
	sysfs_path = mm_modem_get_physdev(modem);
	if (sysfs_path == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no physdev set");
		return FALSE;
	}
	fu_device_set_physical_id(FU_DEVICE(self), sysfs_path);

	/* get GUIDs */
	update_settings = mm_modem_firmware_get_update_settings(modem_fw);
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

	fu_device_set_backend_id(FU_DEVICE(self), mm_object_get_path(omodem));

	/* look for the AT and QMI/MBIM ports */
	if (!mm_modem_get_ports(modem, &used_ports, &n_used_ports)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "failed to get port information");
		return FALSE;
	}
	for (guint i = 0; i < n_used_ports; i++) {
		g_autofree gchar *device_file = g_strdup_printf("/dev/%s", used_ports[i].name);
		if (used_ports[i].type >= MM_MODEM_PORT_TYPE_LAST)
			continue;
		if (used_ports[i].type == MM_MODEM_PORT_TYPE_IGNORED &&
		    g_pattern_match_simple("wwan*qcdm*", used_ports[i].name)) {
			fu_mm_device_add_port(self, MM_MODEM_PORT_TYPE_QCDM, device_file);
		} else {
			fu_mm_device_add_port(self, used_ports[i].type, device_file);
		}
	}
	mm_modem_port_info_array_free(used_ports, n_used_ports);

#if MM_CHECK_VERSION(1, 26, 0)
	if (!mm_modem_get_ignored_ports(modem, &ignored_ports, &n_ignored_ports)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "failed to get ignored port information");
		return FALSE;
	}
	for (guint i = 0; i < n_ignored_ports; i++) {
		g_autofree gchar *device_file = g_strdup_printf("/dev/%s", ignored_ports[i].name);
		if (ignored_ports[i].type >= MM_MODEM_PORT_TYPE_LAST)
			continue;

		fu_mm_device_add_port(self, ignored_ports[i].type, device_file);
	}
	mm_modem_port_info_array_free(ignored_ports, n_ignored_ports);
#endif // MM_CHECK_VERSION(1, 26, 0)

	/* add properties to fwupd device */
	if (mm_modem_get_manufacturer(modem) != NULL)
		fu_device_set_vendor(FU_DEVICE(self), mm_modem_get_manufacturer(modem));
	if (mm_modem_get_model(modem) != NULL)
		fu_device_set_name(FU_DEVICE(self), mm_modem_get_model(modem));

	/* only for modems that opt-in */
	if (fu_device_has_private_flag(FU_DEVICE(self), FU_MM_DEVICE_FLAG_USE_BRANCH))
		fu_device_set_branch(FU_DEVICE(self), mm_modem_get_carrier_configuration(modem));

	fu_device_set_version(FU_DEVICE(self), version);

	/* filter these */
	for (guint i = 0; device_ids[i] != NULL; i++)
		fu_mm_device_add_instance_id(self, device_ids[i]);

	/* fix up vendor name */
	if (g_strcmp0(fu_device_get_vendor(FU_DEVICE(self)), "QUALCOMM INCORPORATED") == 0)
		fu_device_set_vendor(FU_DEVICE(self), "Qualcomm");

	/* success */
	return TRUE;
}

typedef struct {
	const gchar *cmd;
	gsize count;
	gboolean has_response;
	GBytes *blob;
} FuMmDeviceAtCmdHelper;

static gboolean
fu_mm_device_at_cmd_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE(device);
	FuMmDeviceAtCmdHelper *helper = (FuMmDeviceAtCmdHelper *)user_data;
	const gchar *buf;
	gsize bufsz = 0;
	g_autofree gchar *at_res_safe = NULL;
	g_autofree gchar *cmd_cr = g_strdup_printf("%s\r\n", helper->cmd);
	g_autoptr(GBytes) at_req = NULL;
	g_autoptr(GBytes) at_res = NULL;

	/* command */
	g_debug("req: %s", helper->cmd);
	at_req = g_bytes_new(cmd_cr, strlen(cmd_cr));
	if (!fu_udev_device_write_bytes(FU_UDEV_DEVICE(self),
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
	at_res = fu_udev_device_read_bytes(FU_UDEV_DEVICE(self),
					   helper->count,
					   1500,
					   FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
					   error);
	if (at_res == NULL) {
		g_prefix_error(error, "failed to read response for %s: ", helper->cmd);
		return FALSE;
	}
	at_res_safe = fu_strsafe_bytes(at_res, 32);
	g_debug("res: %s", at_res_safe);

	/*
	 * the first time the modem returns may be the command itself with one \n missing.
	 * this is because the modem AT has enabled echo
	 */
	buf = g_bytes_get_data(at_res, &bufsz);
	if (g_strrstr_len(buf, bufsz, helper->cmd) != NULL && bufsz == strlen(helper->cmd) + 1) {
		g_bytes_unref(at_res);
		at_res = fu_udev_device_read_bytes(FU_UDEV_DEVICE(self),
						   helper->count,
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
	if (g_strrstr_len(buf, bufsz, "\r\nOK\r\n") == NULL &&
	    g_strrstr_len(buf, bufsz, "\r\nCONNECT\r\n") == NULL) {
		g_autofree gchar *tmp = g_strndup(buf + 2, bufsz - 4);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to read valid response for %s: %s",
			    helper->cmd,
			    tmp);
		return FALSE;
	}

	/* success */
	helper->blob = g_steal_pointer(&at_res);
	return TRUE;
}

gboolean
fu_mm_device_at_cmd(FuMmDevice *self, const gchar *cmd, gboolean has_response, GError **error)
{
	FuMmDeviceAtCmdHelper helper = {.cmd = cmd, .count = 64, .has_response = has_response};
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_mm_device_at_cmd_cb,
				  FU_MM_DEVICE_AT_RETRIES,
				  FU_MM_DEVICE_AT_DELAY,
				  &helper,
				  error))
		return FALSE;

	/* success, but remove the perhaps-unused buffer */
	if (helper.blob != NULL)
		g_bytes_unref(helper.blob);
	return TRUE;
}

static GBytes *
fu_mm_device_at_cmd_full(FuMmDevice *self, const gchar *cmd, gsize count, GError **error)
{
	FuMmDeviceAtCmdHelper helper = {.cmd = cmd, .count = count, .has_response = TRUE};
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_mm_device_at_cmd_cb,
				  FU_MM_DEVICE_AT_RETRIES,
				  FU_MM_DEVICE_AT_DELAY,
				  &helper,
				  error))
		return NULL;
	return helper.blob;
}

static gboolean
fu_mm_device_ensure_branch(FuMmDevice *self, GError **error)
{
	FuMmDevicePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GBytes) blob = NULL;
	g_auto(GStrv) parts = NULL;

	/* nothing to do if there is no AT port available or
	 * ModemManagerBranchAtCommand quirk is not set */
	if (priv->branch_at == NULL)
		return TRUE;

	/* not supported if the devices is signed */
	if (fu_device_has_flag(self, FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD))
		return TRUE;

	/* example AT+GETFWBRANCH response: "\r\nFOSS-002 \r\n\r\nOK\r\n" */
	blob = fu_mm_device_at_cmd_full(self, priv->branch_at, 64, error);
	if (blob == NULL)
		return FALSE;
	parts = fu_strsplit_bytes(blob, "\r\n", -1);
	for (guint i = 0; parts[i] != NULL; i++) {
		if (g_strcmp0(parts[i], "") != 0 && g_strcmp0(parts[i], "OK") != 0) {
			g_info("firmware branch reported as '%s'", parts[i]);
			fu_device_set_branch(FU_DEVICE(self), parts[i]);
			break;
		}
	}

	/* success */
	return TRUE;
}

static void
fu_mm_device_ensure_payload_quectel(FuMmDevice *self)
{
	const gchar *version = fu_device_get_version(FU_DEVICE(self));
	g_autofree gchar *name = NULL;
	g_autoptr(GError) error_qsec = NULL;
	g_autoptr(GError) error_qcfg = NULL;
	g_autoptr(GBytes) blob = NULL;
	const gchar *signed_versions[] = {"EM05GFAR07A07M1G_01.005.01.005",
					  "EM05CEFCR08A16M1G_LNV"};

	/* newer firmware */
	blob = fu_mm_device_at_cmd_full(self, "AT+QSECBOOT=\"status\"", 64, &error_qsec);
	if (blob == NULL) {
		g_debug("ignoring: %s", error_qsec->message);
	} else {
		/* AT+QSECBOOT="status" response: `\r\n+QSECBOOT: "STATUS",1\r\n\r\nOK\r\n` */
		g_auto(GStrv) parts = fu_strsplit_bytes(blob, "\r\n", -1);
		for (guint i = 0; parts[i] != NULL; i++) {
			if (g_strcmp0(parts[i], "+QSECBOOT: \"status\",1") == 0) {
				fu_device_add_flag(FU_DEVICE(self),
						   FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
				break;
			}
			if (g_strcmp0(parts[i], "+QSECBOOT: \"status\",0") == 0) {
				fu_device_add_flag(FU_DEVICE(self),
						   FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
				break;
			}
		}
		return;
	}

	/* older firmware */
	blob = fu_mm_device_at_cmd_full(self, "AT+QCFG=\"secbootstat\"", 64, &error_qcfg);
	if (blob == NULL) {
		g_debug("ignoring: %s", error_qcfg->message);
	} else {
		g_auto(GStrv) parts = fu_strsplit_bytes(blob, "\r\n", -1);
		for (guint i = 0; parts[i] != NULL; i++) {
			if (g_strcmp0(parts[i], "+QCFG: \"secbootstat\",1") == 0) {
				fu_device_add_flag(FU_DEVICE(self),
						   FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
				break;
			}
			if (g_strcmp0(parts[i], "+QCFG: \"secbootstat\",0") == 0) {
				fu_device_add_flag(FU_DEVICE(self),
						   FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
				break;
			}
		}
		return;
	}

	/* find model name and compare with table from Quectel */
	if (version == NULL)
		return;
	for (guint i = 0; i < G_N_ELEMENTS(signed_versions); i++) {
		if (strncmp(version, signed_versions[i], 6) == 0) {
			if (fu_version_compare(version,
					       signed_versions[i],
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
fu_mm_device_ensure_payload(FuMmDevice *self)
{
	if (fu_device_has_vendor_id(FU_DEVICE(self), "USB:0x2C7C") ||
	    fu_device_has_vendor_id(FU_DEVICE(self), "PCI:0x1EAC")) {
		fu_mm_device_ensure_payload_quectel(self);
	} else if (fu_device_has_vendor_id(FU_DEVICE(self), "USB:0x2CB7")) {
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

	if (!fu_mm_device_ensure_branch(self, &error_local))
		g_debug("failed to set firmware branch: %s", error_local->message);
	fu_mm_device_ensure_payload(self);

	/* success */
	return TRUE;
}

static gboolean
fu_mm_device_set_quirk_kv(FuDevice *device, const gchar *key, const gchar *value, GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE(device);

	if (g_strcmp0(key, "ModemManagerBranchAtCommand") == 0) {
		fu_mm_device_set_branch_at(self, value);
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
fu_mm_device_from_json(FuDevice *device, JsonObject *json_object, GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE(device);
	const gchar *tmp;

	/* FuUdevDevice->from_json */
	if (!FU_DEVICE_CLASS(fu_mm_device_parent_class)->from_json(device, json_object, error))
		return FALSE;

	/* optional properties */
	tmp = json_object_get_string_member_with_default(json_object, "Version", NULL);
	if (tmp != NULL)
		fu_device_set_version(device, tmp);
	tmp = json_object_get_string_member_with_default(json_object, "PhysicalId", NULL);
	if (tmp != NULL)
		fu_device_set_physical_id(device, tmp);
	tmp = json_object_get_string_member_with_default(json_object, "BranchAt", NULL);
	if (tmp != NULL)
		fu_mm_device_set_branch_at(self, tmp);

	/* specified by ModemManager, unusually */
	if (json_object_has_member(json_object, "DeviceIds")) {
		JsonArray *json_array = json_object_get_array_member(json_object, "DeviceIds");
		for (guint i = 0; i < json_array_get_length(json_array); i++) {
			const gchar *instance_id = json_array_get_string_element(json_array, i);
			fu_mm_device_add_instance_id(self, instance_id);
		}
	}

	/* ports */
	if (json_object_has_member(json_object, "Ports")) {
		JsonObject *json_ports = json_object_get_object_member(json_object, "Ports");
		g_autoptr(GList) keys = json_object_get_members(json_ports);
		for (GList *l = keys; l != NULL; l = l->next) {
			const gchar *port_type = l->data;
			fu_mm_device_add_port(self,
					      fu_mm_device_port_type_from_string(port_type),
					      json_object_get_string_member(json_ports, port_type));
		}
	}

	/* success */
	return TRUE;
}

static void
fu_mm_device_add_json(FuDevice *device, JsonBuilder *builder, FwupdCodecFlags flags)
{
	FuMmDevice *self = FU_MM_DEVICE(device);
	FuMmDevicePrivate *priv = GET_PRIVATE(self);
	GPtrArray *instance_ids = fu_device_get_instance_ids(device);
	GPtrArray *vendor_ids = fu_device_get_vendor_ids(device);

	/* FuUdevDevice->add_json */
	FU_DEVICE_CLASS(fu_mm_device_parent_class)->add_json(device, builder, flags);

	/* optional properties */
	fwupd_codec_json_append(builder, "GType", G_OBJECT_TYPE_NAME(self));
	if (fu_device_get_version(device) != NULL)
		fwupd_codec_json_append(builder, "Version", fu_device_get_version(device));
	if (fu_device_get_physical_id(device) != NULL)
		fwupd_codec_json_append(builder, "PhysicalId", fu_device_get_physical_id(device));
	if (priv->branch_at != NULL)
		fwupd_codec_json_append(builder, "BranchAt", priv->branch_at);

	/* specified by ModemManager, unusually */
	json_builder_set_member_name(builder, "DeviceIds");
	json_builder_begin_array(builder);
	for (guint i = 0; i < instance_ids->len; i++) {
		const gchar *instance_id = g_ptr_array_index(instance_ids, i);
		json_builder_add_string_value(builder, instance_id);
	}
	for (guint i = 0; i < vendor_ids->len; i++) {
		const gchar *vendor_id = g_ptr_array_index(vendor_ids, i);
		if (g_str_has_prefix(vendor_id, "USB:0x")) {
			g_autofree gchar *id = g_strdup_printf("USB\\VID_%s", vendor_id + 6);
			json_builder_add_string_value(builder, id);
		}
		if (g_str_has_prefix(vendor_id, "PCI:0x")) {
			g_autofree gchar *id = g_strdup_printf("PCI\\VEN_%s", vendor_id + 6);
			json_builder_add_string_value(builder, id);
		}
	}
	json_builder_end_array(builder);

	/* ports always specified */
	json_builder_set_member_name(builder, "Ports");
	json_builder_begin_object(builder);
	for (guint i = 0; i < MM_MODEM_PORT_TYPE_LAST; i++) {
		if (priv->port[i] != NULL) {
			fwupd_codec_json_append(builder,
						fu_mm_device_port_type_to_string(i),
						priv->port[i]);
		}
	}
	json_builder_end_object(builder);
}

static void
fu_mm_device_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuMmDevice *self = FU_MM_DEVICE(object);
	FuMmDevicePrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_INHIBITED:
		g_value_set_boolean(value, priv->inhibited);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_mm_device_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FuMmDevice *self = FU_MM_DEVICE(object);
	switch (prop_id) {
	case PROP_INHIBITED:
		fu_mm_device_set_inhibited(self, g_value_get_boolean(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_mm_device_init(FuMmDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_ARCHIVE_FIRMWARE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_RUNTIME_VERSION);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_MD_SET_VERFMT);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ADD_INSTANCE_ID_REV);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_summary(FU_DEVICE(self), "Mobile broadband device");
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_MODEM);
	fu_device_register_private_flag(FU_DEVICE(self), FU_MM_DEVICE_FLAG_USE_BRANCH);
	fu_device_add_possible_plugin(FU_DEVICE(self), "modem_manager");
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
}

static void
fu_mm_device_finalize(GObject *object)
{
	FuMmDevice *self = FU_MM_DEVICE(object);
	FuMmDevicePrivate *priv = GET_PRIVATE(self);

	for (guint i = 0; i < MM_MODEM_PORT_TYPE_LAST; i++)
		g_free(priv->port[i]);
	g_free(priv->branch_at);
	g_free(priv->inhibition_uid);

	G_OBJECT_CLASS(fu_mm_device_parent_class)->finalize(object);
}

static void
fu_mm_device_class_init(FuMmDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;

	object_class->finalize = fu_mm_device_finalize;
	object_class->get_property = fu_mm_device_get_property;
	object_class->set_property = fu_mm_device_set_property;
	device_class->setup = fu_mm_device_setup;
	device_class->to_string = fu_mm_device_to_string;
	device_class->set_quirk_kv = fu_mm_device_set_quirk_kv;
	device_class->from_json = fu_mm_device_from_json;
	device_class->add_json = fu_mm_device_add_json;

	pspec = g_param_spec_boolean("inhibited",
				     NULL,
				     NULL,
				     FALSE,
				     G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_INHIBITED, pspec);
}
