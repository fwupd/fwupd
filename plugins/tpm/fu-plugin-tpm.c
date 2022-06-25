/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-tpm-eventlog-parser.h"
#include "fu-tpm-v1-device.h"
#include "fu-tpm-v2-device.h"

struct FuPluginData {
	FuTpmDevice *tpm_device;
	FuDevice *bios_device;
	GPtrArray *ev_items; /* of FuTpmEventlogItem */
};

static void
fu_plugin_tpm_init(FuPlugin *plugin)
{
	fu_plugin_alloc_data(plugin, sizeof(FuPluginData));
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_CONFLICTS, "tpm_eventlog"); /* old name */
	fu_plugin_add_udev_subsystem(plugin, "tpm");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_TPM_V2_DEVICE);
}

static void
fu_plugin_tpm_destroy(FuPlugin *plugin)
{
	FuPluginData *priv = fu_plugin_get_data(plugin);
	if (priv->tpm_device != NULL)
		g_object_unref(priv->tpm_device);
	if (priv->bios_device != NULL)
		g_object_unref(priv->bios_device);
	if (priv->ev_items != NULL)
		g_ptr_array_unref(priv->ev_items);
}

static void
fu_plugin_tpm_set_bios_pcr0s(FuPlugin *plugin)
{
	FuPluginData *priv = fu_plugin_get_data(plugin);
	g_autoptr(GPtrArray) pcr0s = NULL;

	if (priv->tpm_device == NULL)
		return;
	if (priv->bios_device == NULL)
		return;

	/* add all the PCR0s */
	pcr0s = fu_tpm_device_get_checksums(priv->tpm_device, 0);
	if (pcr0s->len == 0)
		return;
	for (guint i = 0; i < pcr0s->len; i++) {
		const gchar *checksum = g_ptr_array_index(pcr0s, i);
		fu_device_add_checksum(priv->bios_device, checksum);
	}
	fu_device_add_flag(priv->bios_device, FWUPD_DEVICE_FLAG_CAN_VERIFY);
}

/* set the PCR0 as the device checksum */
static void
fu_plugin_tpm_device_registered(FuPlugin *plugin, FuDevice *device)
{
	FuPluginData *priv = fu_plugin_get_data(plugin);
	if (fu_device_has_instance_id(device, "main-system-firmware")) {
		g_set_object(&priv->bios_device, device);
		fu_plugin_tpm_set_bios_pcr0s(plugin);
	}
}

static void
fu_plugin_tpm_device_added(FuPlugin *plugin, FuDevice *dev)
{
	FuPluginData *priv = fu_plugin_get_data(plugin);
	g_autoptr(GPtrArray) pcr0s = NULL;

	g_set_object(&priv->tpm_device, FU_TPM_DEVICE(dev));
	fu_plugin_add_report_metadata(plugin,
				      "TpmFamily",
				      fu_tpm_device_get_family(FU_TPM_DEVICE(dev)));

	/* ensure */
	fu_plugin_tpm_set_bios_pcr0s(plugin);

	/* add extra plugin metadata */
	pcr0s = fu_tpm_device_get_checksums(priv->tpm_device, 0);
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
	}
}

static void
fu_plugin_tpm_add_security_attr_version(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_TPM_VERSION_20);
	fwupd_security_attr_set_plugin(attr, fu_plugin_get_name(plugin));
	fwupd_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fu_security_attrs_append(attrs, attr);

	/* check exists, and in v2.0 mode */
	if (priv->tpm_device == NULL) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}
	if (g_strcmp0(fu_tpm_device_get_family(priv->tpm_device), "2.0") != 0) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}

	/* success */
	fwupd_security_attr_add_guids(attr, fu_device_get_guids(FU_DEVICE(priv->tpm_device)));
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_FOUND);
}

static void
fu_plugin_tpm_add_security_attr_eventlog(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data(plugin);
	gboolean reconstructed = TRUE;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) pcr0s_calc = NULL;
	g_autoptr(GPtrArray) pcr0s_real = NULL;

	/* no TPM device */
	if (priv->tpm_device == NULL)
		return;

	/* create attr */
	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_TPM_RECONSTRUCTION_PCR0);
	fwupd_security_attr_set_plugin(attr, fu_plugin_get_name(plugin));
	fwupd_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT);
	fwupd_security_attr_add_guids(attr, fu_device_get_guids(priv->tpm_device));
	fu_security_attrs_append(attrs, attr);

	/* check reconstructed to PCR0 */
	if (priv->ev_items == NULL || priv->bios_device == NULL) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}

	/* calculate from the eventlog */
	pcr0s_calc = fu_tpm_eventlog_calc_checksums(priv->ev_items, 0, &error);
	if (pcr0s_calc == NULL) {
		g_warning("failed to get eventlog reconstruction: %s", error->message);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}

	/* compare against the real PCR0s */
	pcr0s_real = fu_tpm_device_get_checksums(priv->tpm_device, 0);
	for (guint i = 0; i < pcr0s_real->len; i++) {
		const gchar *checksum = g_ptr_array_index(pcr0s_real, i);
		reconstructed = FALSE;
		for (guint j = 0; j < pcr0s_calc->len; j++) {
			const gchar *checksum_tmp = g_ptr_array_index(pcr0s_calc, j);
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
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
}

static void
fu_plugin_tpm_add_security_attr_empty(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* no TPM device */
	if (priv->tpm_device == NULL)
		return;

	/* add attributes */
	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_TPM_EMPTY_PCR);
	fwupd_security_attr_set_plugin(attr, fu_plugin_get_name(plugin));
	fwupd_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fwupd_security_attr_add_guids(attr, fu_device_get_guids(priv->tpm_device));
	fu_security_attrs_append(attrs, attr);

	/* check PCRs 0 through 7 for empty checksums */
	for (guint pcr = 0; pcr <= 7; pcr++) {
		g_autoptr(GPtrArray) checksums = fu_tpm_device_get_checksums(priv->tpm_device, pcr);
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
fu_plugin_tpm_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	if (fu_plugin_has_flag(plugin, FWUPD_PLUGIN_FLAG_DISABLED))
		return;
	fu_plugin_tpm_add_security_attr_version(plugin, attrs);
	fu_plugin_tpm_add_security_attr_eventlog(plugin, attrs);
	fu_plugin_tpm_add_security_attr_empty(plugin, attrs);
}

static gchar *
fu_plugin_tpm_eventlog_report_metadata(FuPlugin *plugin)
{
	FuPluginData *priv = fu_plugin_get_data(plugin);
	GString *str = g_string_new("");
	g_autoptr(GPtrArray) pcrs = NULL;

	for (guint i = 0; i < priv->ev_items->len; i++) {
		FuTpmEventlogItem *item = g_ptr_array_index(priv->ev_items, i);
		g_autofree gchar *blobstr = NULL;
		g_autofree gchar *checksum = NULL;

		if (item->blob == NULL)
			continue;
		if (item->checksum_sha1 != NULL)
			checksum = fu_tpm_eventlog_strhex(item->checksum_sha1);
		else if (item->checksum_sha256 != NULL)
			checksum = fu_tpm_eventlog_strhex(item->checksum_sha256);
		else
			continue;
		g_string_append_printf(str, "0x%08x %s", item->kind, checksum);
		blobstr = fu_tpm_eventlog_blobstr(item->blob);
		if (blobstr != NULL)
			g_string_append_printf(str, " [%s]", blobstr);
		g_string_append(str, "\n");
	}
	pcrs = fu_tpm_eventlog_calc_checksums(priv->ev_items, 0, NULL);
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
fu_plugin_tpm_coldplug_eventlog(FuPlugin *plugin, GError **error)
{
	FuPluginData *priv = fu_plugin_get_data(plugin);
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
	priv->ev_items =
	    fu_tpm_eventlog_parser_new(buf, bufsz, FU_TPM_EVENTLOG_PARSER_FLAG_NONE, error);
	if (priv->ev_items == NULL)
		return FALSE;

	/* add optional report metadata */
	str = fu_plugin_tpm_eventlog_report_metadata(plugin);
	fu_plugin_add_report_metadata(plugin, "TpmEventLog", str);
	return TRUE;
}

static gboolean
fu_plugin_tpm_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	g_autoptr(GError) error_local = NULL;

	/* best effort */
	if (!fu_plugin_tpm_coldplug_eventlog(plugin, &error_local))
		g_warning("failed to load eventlog: %s", error_local->message);

	/* success */
	return TRUE;
}

static gboolean
fu_plugin_tpm_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuPluginData *priv = fu_plugin_get_data(plugin);
	g_autofree gchar *sysfstpmdir = NULL;
	g_autofree gchar *fn_pcrs = NULL;

	/* look for TPM v1.2 */
	sysfstpmdir = fu_path_from_kind(FU_PATH_KIND_SYSFSDIR_TPM);
	fn_pcrs = g_build_filename(sysfstpmdir, "tpm0", "pcrs", NULL);
	if (g_file_test(fn_pcrs, G_FILE_TEST_EXISTS) && g_getenv("FWUPD_FORCE_TPM2") == NULL) {
		priv->tpm_device = fu_tpm_v1_device_new(fu_plugin_get_context(plugin));
		g_object_set(priv->tpm_device, "device-file", fn_pcrs, NULL);
		fu_device_set_physical_id(FU_DEVICE(priv->tpm_device), "tpm");
		if (!fu_device_probe(FU_DEVICE(priv->tpm_device), error))
			return FALSE;
		fu_plugin_device_add(plugin, FU_DEVICE(priv->tpm_device));
	}

	/* success */
	return TRUE;
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_tpm_init;
	vfuncs->destroy = fu_plugin_tpm_destroy;
	vfuncs->startup = fu_plugin_tpm_startup;
	vfuncs->coldplug = fu_plugin_tpm_coldplug;
	vfuncs->device_added = fu_plugin_tpm_device_added;
	vfuncs->device_registered = fu_plugin_tpm_device_registered;
	vfuncs->add_security_attrs = fu_plugin_tpm_add_security_attrs;
}
