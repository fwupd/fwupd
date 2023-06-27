/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-tpm-eventlog-parser.h"
#include "fu-tpm-plugin.h"
#include "fu-tpm-v1-device.h"
#include "fu-tpm-v2-device.h"

struct _FuTpmPlugin {
	FuPlugin parent_instance;
	FuTpmDevice *tpm_device;
	FuDevice *bios_device;
	GPtrArray *ev_items; /* of FuTpmEventlogItem */
};

G_DEFINE_TYPE(FuTpmPlugin, fu_tpm_plugin, FU_TYPE_PLUGIN)

static void
fu_tpm_plugin_to_string(FuPlugin *plugin, guint idt, GString *str)
{
	FuTpmPlugin *self = FU_TPM_PLUGIN(plugin);
	if (self->tpm_device != NULL)
		fu_string_append(str, idt, "TpmDevice", fu_device_get_id(self->tpm_device));
	if (self->bios_device != NULL)
		fu_string_append(str, idt, "BiosDevice", fu_device_get_id(self->bios_device));
}

static void
fu_tpm_plugin_set_bios_pcr0s(FuPlugin *plugin)
{
	FuTpmPlugin *self = FU_TPM_PLUGIN(plugin);
	g_autoptr(GPtrArray) pcr0s = NULL;

	if (self->tpm_device == NULL)
		return;
	if (self->bios_device == NULL)
		return;

	/* add all the PCR0s */
	pcr0s = fu_tpm_device_get_checksums(self->tpm_device, 0);
	if (pcr0s->len == 0)
		return;
	for (guint i = 0; i < pcr0s->len; i++) {
		const gchar *checksum = g_ptr_array_index(pcr0s, i);
		fu_device_add_checksum(self->bios_device, checksum);
	}
	fu_device_add_flag(self->bios_device, FWUPD_DEVICE_FLAG_CAN_VERIFY);
}

/* set the PCR0 as the device checksum */
static void
fu_tpm_plugin_device_registered(FuPlugin *plugin, FuDevice *device)
{
	FuTpmPlugin *self = FU_TPM_PLUGIN(plugin);
	if (fu_device_has_instance_id(device, "main-system-firmware")) {
		g_set_object(&self->bios_device, device);
		fu_tpm_plugin_set_bios_pcr0s(plugin);
	}
}

static void
fu_tpm_plugin_device_added(FuPlugin *plugin, FuDevice *dev)
{
	FuTpmPlugin *self = FU_TPM_PLUGIN(plugin);
	g_autoptr(GPtrArray) pcr0s = NULL;

	g_set_object(&self->tpm_device, FU_TPM_DEVICE(dev));
	fu_plugin_add_report_metadata(plugin,
				      "TpmFamily",
				      fu_tpm_device_get_family(FU_TPM_DEVICE(dev)));

	/* ensure */
	fu_tpm_plugin_set_bios_pcr0s(plugin);

	/* add extra plugin metadata */
	pcr0s = fu_tpm_device_get_checksums(self->tpm_device, 0);
	for (guint i = 0; i < pcr0s->len; i++) {
		const gchar *csum = g_ptr_array_index(pcr0s, i);
		GChecksumType csum_type = fwupd_checksum_guess_kind(csum);
		if (csum_type == G_CHECKSUM_SHA1) {
			fu_plugin_add_report_metadata(plugin, "Pcr0_SHA1", csum);
			continue;
		}
		if (csum_type == G_CHECKSUM_SHA256) {
			fu_plugin_add_report_metadata(plugin, "Pcr0_SHA256", csum);
			continue;
		}
		if (csum_type == G_CHECKSUM_SHA384) {
			fu_plugin_add_report_metadata(plugin, "Pcr0_SHA384", csum);
			continue;
		}
	}
}

static void
fu_tpm_plugin_add_security_attr_version(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuTpmPlugin *self = FU_TPM_PLUGIN(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_TPM_VERSION_20);
	fu_security_attrs_append(attrs, attr);

	/* check exists, and in v2.0 mode */
	if (self->tpm_device == NULL) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}
	if (g_strcmp0(fu_tpm_device_get_family(self->tpm_device), "2.0") != 0) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW);
		return;
	}

	/* success */
	fwupd_security_attr_add_guids(attr, fu_device_get_guids(FU_DEVICE(self->tpm_device)));
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_FOUND);
}

static void
fu_tpm_plugin_add_security_attr_eventlog(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuTpmPlugin *self = FU_TPM_PLUGIN(plugin);
	gboolean reconstructed = TRUE;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) pcr0s_calc = NULL;
	g_autoptr(GPtrArray) pcr0s_real = NULL;

	/* no TPM device */
	if (self->tpm_device == NULL)
		return;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_TPM_RECONSTRUCTION_PCR0);
	fwupd_security_attr_add_guids(attr, fu_device_get_guids(self->tpm_device));
	fu_security_attrs_append(attrs, attr);

	/* check reconstructed to PCR0 */
	if (self->ev_items == NULL) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}

	/* calculate from the eventlog */
	pcr0s_calc = fu_tpm_eventlog_calc_checksums(self->ev_items, 0, &error);
	if (pcr0s_calc == NULL) {
		g_warning("failed to get eventlog reconstruction: %s", error->message);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* compare against the real PCR0s */
	pcr0s_real = fu_tpm_device_get_checksums(self->tpm_device, 0);
	for (guint i = 0; i < pcr0s_calc->len; i++) {
		const gchar *checksum = g_ptr_array_index(pcr0s_calc, i);
		reconstructed = FALSE;
		for (guint j = 0; j < pcr0s_real->len; j++) {
			const gchar *checksum_tmp = g_ptr_array_index(pcr0s_real, j);
			/* skip unless same algorithm */
			if (strlen(checksum) != strlen(checksum_tmp))
				continue;
			g_debug("comparing TPM %s and EVT %s", checksum, checksum_tmp);
			if (g_strcmp0(checksum, checksum_tmp) == 0) {
				reconstructed = TRUE;
				break;
			}
		}
		/* all algorithms must match */
		if (!reconstructed)
			break;
	}
	if (!reconstructed) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
}

static void
fu_tpm_plugin_add_security_attr_empty(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuTpmPlugin *self = FU_TPM_PLUGIN(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* no TPM device */
	if (self->tpm_device == NULL)
		return;

	/* add attributes */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_TPM_EMPTY_PCR);
	fwupd_security_attr_add_guids(attr, fu_device_get_guids(self->tpm_device));
	fu_security_attrs_append(attrs, attr);

	/* check PCRs 0 through 7 for empty checksums */
	for (guint pcr = 0; pcr <= 7; pcr++) {
		g_autoptr(GPtrArray) checksums = fu_tpm_device_get_checksums(self->tpm_device, pcr);
		for (guint i = 0; i < checksums->len; i++) {
			const gchar *checksum = g_ptr_array_index(checksums, i);
			gboolean empty = TRUE;

			/* empty checksum is zero, so made entirely of zeroes */
			for (guint j = 0; checksum[j] != '\0'; j++) {
				if (checksum[j] != '0') {
					empty = FALSE;
					break;
				}
			}
			if (empty) {
				fwupd_security_attr_set_result(
				    attr,
				    FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
				return;
			}
		}
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
}

static void
fu_tpm_plugin_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	if (fu_plugin_has_flag(plugin, FWUPD_PLUGIN_FLAG_DISABLED))
		return;
	fu_tpm_plugin_add_security_attr_version(plugin, attrs);
	fu_tpm_plugin_add_security_attr_eventlog(plugin, attrs);
	fu_tpm_plugin_add_security_attr_empty(plugin, attrs);
}

static gchar *
fu_tpm_plugin_eventlog_report_metadata(FuPlugin *plugin)
{
	FuTpmPlugin *self = FU_TPM_PLUGIN(plugin);
	GString *str = g_string_new("");
	g_autoptr(GPtrArray) pcrs = NULL;

	for (guint i = 0; i < self->ev_items->len; i++) {
		FuTpmEventlogItem *item = g_ptr_array_index(self->ev_items, i);
		g_autofree gchar *blobstr = NULL;
		g_autofree gchar *checksum = NULL;

		if (item->blob == NULL)
			continue;
		if (item->checksum_sha1 != NULL)
			checksum = fu_tpm_eventlog_strhex(item->checksum_sha1);
		else if (item->checksum_sha256 != NULL)
			checksum = fu_tpm_eventlog_strhex(item->checksum_sha256);
		else if (item->checksum_sha384 != NULL)
			checksum = fu_tpm_eventlog_strhex(item->checksum_sha384);
		else
			continue;
		g_string_append_printf(str, "0x%08x %s", item->kind, checksum);
		blobstr = fu_tpm_eventlog_blobstr(item->blob);
		if (blobstr != NULL)
			g_string_append_printf(str, " [%s]", blobstr);
		g_string_append(str, "\n");
	}
	pcrs = fu_tpm_eventlog_calc_checksums(self->ev_items, 0, NULL);
	if (pcrs != NULL) {
		for (guint j = 0; j < pcrs->len; j++) {
			const gchar *csum = g_ptr_array_index(pcrs, j);
			g_string_append_printf(str, "PCR0: %s\n", csum);
		}
	}
	if (str->len > 0)
		g_string_truncate(str, str->len - 1);
	return g_string_free(str, FALSE);
}

static gboolean
fu_tpm_plugin_coldplug_eventlog(FuPlugin *plugin, GError **error)
{
	FuTpmPlugin *self = FU_TPM_PLUGIN(plugin);
	gsize bufsz = 0;
	const gchar *fn = "/sys/kernel/security/tpm0/binary_bios_measurements";
	g_autofree gchar *str = NULL;
	g_autofree guint8 *buf = NULL;

	/* do not show a warning if no TPM exists, or the kernel is too old */
	if (!g_file_test(fn, G_FILE_TEST_EXISTS)) {
		g_debug("no %s, so skipping", fn);
		return TRUE;
	}
	if (!g_file_get_contents(fn, (gchar **)&buf, &bufsz, error))
		return FALSE;
	if (bufsz == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "failed to read data from %s",
			    fn);
		return FALSE;
	}
	self->ev_items =
	    fu_tpm_eventlog_parser_new(buf, bufsz, FU_TPM_EVENTLOG_PARSER_FLAG_NONE, error);
	if (self->ev_items == NULL)
		return FALSE;

	/* add optional report metadata */
	str = fu_tpm_plugin_eventlog_report_metadata(plugin);
	fu_plugin_add_report_metadata(plugin, "TpmEventLog", str);
	return TRUE;
}

static gboolean
fu_tpm_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	g_autoptr(GError) error_local = NULL;

	/* best effort */
	if (!fu_tpm_plugin_coldplug_eventlog(plugin, &error_local))
		g_warning("failed to load eventlog: %s", error_local->message);

	/* success */
	return TRUE;
}

static gboolean
fu_tpm_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuTpmPlugin *self = FU_TPM_PLUGIN(plugin);
	g_autofree gchar *sysfstpmdir = NULL;
	g_autofree gchar *fn_pcrs = NULL;

	/* look for TPM v1.2 */
	sysfstpmdir = fu_path_from_kind(FU_PATH_KIND_SYSFSDIR_TPM);
	fn_pcrs = g_build_filename(sysfstpmdir, "tpm0", "pcrs", NULL);
	if (g_file_test(fn_pcrs, G_FILE_TEST_EXISTS) && g_getenv("FWUPD_FORCE_TPM2") == NULL) {
		self->tpm_device = fu_tpm_v1_device_new(fu_plugin_get_context(plugin));
		g_object_set(self->tpm_device, "device-file", fn_pcrs, NULL);
		fu_device_set_physical_id(FU_DEVICE(self->tpm_device), "tpm");
		if (!fu_device_probe(FU_DEVICE(self->tpm_device), error))
			return FALSE;
		fu_plugin_device_add(plugin, FU_DEVICE(self->tpm_device));
	}

	/* success */
	return TRUE;
}

static void
fu_tpm_plugin_init(FuTpmPlugin *self)
{
}

static void
fu_tpm_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);

	/* old name */
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_CONFLICTS, "tpm_eventlog");
	fu_plugin_add_device_udev_subsystem(plugin, "tpm");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_TPM_V2_DEVICE);
}

static void
fu_tpm_finalize(GObject *obj)
{
	FuTpmPlugin *self = FU_TPM_PLUGIN(obj);
	if (self->tpm_device != NULL)
		g_object_unref(self->tpm_device);
	if (self->bios_device != NULL)
		g_object_unref(self->bios_device);
	if (self->ev_items != NULL)
		g_ptr_array_unref(self->ev_items);
	G_OBJECT_CLASS(fu_tpm_plugin_parent_class)->finalize(obj);
}

static void
fu_tpm_plugin_class_init(FuTpmPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_tpm_finalize;
	plugin_class->constructed = fu_tpm_plugin_constructed;
	plugin_class->to_string = fu_tpm_plugin_to_string;
	plugin_class->startup = fu_tpm_plugin_startup;
	plugin_class->coldplug = fu_tpm_plugin_coldplug;
	plugin_class->device_added = fu_tpm_plugin_device_added;
	plugin_class->device_registered = fu_tpm_plugin_device_registered;
	plugin_class->add_security_attrs = fu_tpm_plugin_add_security_attrs;
}
