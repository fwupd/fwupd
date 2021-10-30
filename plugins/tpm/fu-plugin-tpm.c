/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-tpm-v1-device.h"
#include "fu-tpm-v2-device.h"

struct FuPluginData {
	FuTpmDevice *tpm_device;
	FuDevice *bios_device;
	gboolean has_tpm_v20;
};

void
fu_plugin_init(FuPlugin *plugin)
{
	fu_plugin_alloc_data(plugin, sizeof(FuPluginData));
	fu_plugin_set_build_hash(plugin, FU_BUILD_HASH);
	fu_plugin_add_udev_subsystem(plugin, "tpm");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_TPM_V2_DEVICE);
}

void
fu_plugin_destroy(FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data(plugin);
	if (data->tpm_device != NULL)
		g_object_unref(data->tpm_device);
	if (data->bios_device != NULL)
		g_object_unref(data->bios_device);
}

static void
fu_plugin_tpm_set_bios_pcr0s(FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data(plugin);
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) pcr0s = NULL;

	if (data->tpm_device == NULL)
		return;
	if (data->bios_device == NULL)
		return;

	/* get all the PCR0s */
	pcr0s = fu_tpm_device_get_checksums(data->tpm_device, 0);
	if (pcr0s->len == 0)
		return;
	for (guint i = 0; i < pcr0s->len; i++) {
		const gchar *checksum = g_ptr_array_index(pcr0s, i);
		fu_device_add_checksum(data->bios_device, checksum);
	}
	fu_device_add_flag(data->bios_device, FWUPD_DEVICE_FLAG_CAN_VERIFY);
}

/* set the PCR0 as the device checksum */
void
fu_plugin_device_registered(FuPlugin *plugin, FuDevice *device)
{
	FuPluginData *data = fu_plugin_get_data(plugin);
	if (fu_device_has_instance_id(device, "main-system-firmware")) {
		g_set_object(&data->bios_device, device);
		fu_plugin_tpm_set_bios_pcr0s(plugin);
	}
}

void
fu_plugin_device_added(FuPlugin *plugin, FuDevice *dev)
{
	FuPluginData *data = fu_plugin_get_data(plugin);
	const gchar *family = fu_tpm_device_get_family(FU_TPM_DEVICE(dev));

	g_set_object(&data->tpm_device, FU_TPM_DEVICE(dev));
	if (g_strcmp0(family, "2.0") == 0)
		data->has_tpm_v20 = TRUE;
	fu_plugin_add_report_metadata(plugin, "TpmFamily", family);

	/* ensure */
	fu_plugin_tpm_set_bios_pcr0s(plugin);
}

void
fu_plugin_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *data = fu_plugin_get_data(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_TPM_VERSION_20);
	fwupd_security_attr_set_plugin(attr, fu_plugin_get_name(plugin));
	fwupd_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fu_security_attrs_append(attrs, attr);

	/* check exists, and in v2.0 mode */
	if (data->tpm_device == NULL) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}
	if (!data->has_tpm_v20) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}

	/* success */
	fwupd_security_attr_add_guids(attr, fu_device_get_guids(FU_DEVICE(data->tpm_device)));
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_FOUND);
}

gboolean
fu_plugin_startup(FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data(plugin);
	g_autofree gchar *sysfstpmdir = NULL;
	g_autofree gchar *fn_pcrs = NULL;

	/* look for TPM v1.2 */
	sysfstpmdir = fu_common_get_path(FU_PATH_KIND_SYSFSDIR_TPM);
	fn_pcrs = g_build_filename(sysfstpmdir, "tmp0", "pcrs", NULL);
	if (g_file_test(fn_pcrs, G_FILE_TEST_EXISTS) && g_getenv("FWUPD_FORCE_TPM2") == NULL) {
		data->tpm_device = fu_tpm_v1_device_new(fu_plugin_get_context(plugin));
		g_object_set(data->tpm_device, "device-file", fn_pcrs, NULL);
		if (!fu_device_probe(FU_DEVICE(data->tpm_device), error))
			return FALSE;
	}

	/* success */
	return TRUE;
}
