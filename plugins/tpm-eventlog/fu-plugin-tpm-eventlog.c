/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-tpm-eventlog-device.h"

struct FuPluginData {
	GPtrArray		*pcr0s;
	gboolean		 has_tpm_device;
	gboolean		 has_uefi_device;
	gboolean		 reconstructed;
};

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_RUN_BEFORE, "uefi_capsule");
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_RUN_AFTER, "tpm");
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	if (data->pcr0s != NULL)
		g_ptr_array_unref (data->pcr0s);
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	gsize bufsz = 0;
	const gchar *fn = "/sys/kernel/security/tpm0/binary_bios_measurements";
	g_autofree gchar *str = NULL;
	g_autofree guint8 *buf = NULL;
	g_autoptr(FuTpmEventlogDevice) dev = NULL;

	if (!g_file_get_contents (fn, (gchar **) &buf, &bufsz, error))
		return FALSE;
	if (bufsz == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "failed to read data from %s", fn);
		return FALSE;
	}
	dev = fu_tpm_eventlog_device_new (buf, bufsz, error);
	if (dev == NULL)
		return FALSE;
	if (!fu_device_setup (FU_DEVICE (dev), error))
		return FALSE;

	/* save this so we can compare against system-firmware */
	data->pcr0s = fu_tpm_eventlog_device_get_checksums (dev, 0, error);
	if (data->pcr0s == NULL)
		return FALSE;
	for (guint i = 0; i < data->pcr0s->len; i++) {
		const gchar *csum = g_ptr_array_index (data->pcr0s, i);
		fu_device_add_checksum (FU_DEVICE (dev), csum);
	}
	for (guint i = 0; i < data->pcr0s->len; i++) {
		const gchar *csum = g_ptr_array_index (data->pcr0s, i);
		GChecksumType csum_type = fwupd_checksum_guess_kind (csum);
		if (csum_type == G_CHECKSUM_SHA1) {
			fu_plugin_add_report_metadata (plugin, "Pcr0_SHA1", csum);
			continue;
		}
		if (csum_type == G_CHECKSUM_SHA256) {
			fu_plugin_add_report_metadata (plugin, "Pcr0_SHA256", csum);
			continue;
		}
	}

	/* add optional report metadata */
	str = fu_tpm_eventlog_device_report_metadata (dev);
	fu_plugin_add_report_metadata (plugin, "TpmEventLog", str);
	fu_plugin_device_add (plugin, FU_DEVICE (dev));
	return TRUE;
}

static void
fu_plugin_device_registered_tpm (FuPlugin *plugin, FuDevice *device)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	data->has_tpm_device = TRUE;
}

static void
fu_plugin_device_registered_uefi (FuPlugin *plugin, FuDevice *device)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	GPtrArray *checksums;

	/* only the system-firmware device gets checksums */
	checksums = fu_device_get_checksums (device);
	if (checksums->len == 0)
		return;
	data->has_uefi_device = TRUE;

	for (guint i = 0; i < checksums->len; i++) {
		const gchar *checksum = g_ptr_array_index (checksums, i);
		data->reconstructed = FALSE;
		for (guint j = 0; j < data->pcr0s->len; j++) {
			const gchar *checksum_tmp = g_ptr_array_index (data->pcr0s, j);
			/* skip unless same algorithm */
			if (strlen (checksum) != strlen (checksum_tmp))
				continue;
			if (g_strcmp0 (checksum, checksum_tmp) == 0) {
				data->reconstructed = TRUE;
				break;
			}
		}
		/* check at least one reconstruction for this algorithm */
		if (!data->reconstructed)
			return;
	}
}

void
fu_plugin_device_registered (FuPlugin *plugin, FuDevice *device)
{
	/* only care about UEFI devices from ESRT */
	if (g_strcmp0 (fu_device_get_plugin (device), "uefi_capsule") == 0) {
		fu_plugin_device_registered_uefi (plugin, device);
		return;
	}

	/* detect the system TPM device */
	if (g_strcmp0 (fu_device_get_plugin (device), "tpm") == 0) {
		fu_plugin_device_registered_tpm (plugin, device);
		return;
	}
}

void
fu_plugin_add_security_attrs (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* no TPM device */
	if (!data->has_tpm_device)
		return;

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_TPM_RECONSTRUCTION_PCR0);
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT);
	fu_security_attrs_append (attrs, attr);

	/* check reconstructed to PCR0 */
	if (fu_plugin_has_flag (plugin, FWUPD_PLUGIN_FLAG_DISABLED) || !data->has_uefi_device) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}
	if (!data->reconstructed) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
}
