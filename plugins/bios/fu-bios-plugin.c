/*
 * Copyright 2020 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-bios-plugin.h"

#define FU_BIOS_PLUGIN_STARLABS_COREBOOT_HWID	     "b8cf5af6-8a46-5deb-ac01-a35b1ea5fb48"
#define FU_BIOS_PLUGIN_STARLABS_COREBOOT_VERSION_MIN "26.02"
#define FU_BIOS_PLUGIN_STARLABS_SUPPORT_URL                                                        \
	"https://support.starlabs.systems/hc/star-labs/articles/updating-your-firmware"

struct _FuBiosPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuBiosPlugin, fu_bios_plugin, FU_TYPE_PLUGIN)

static gchar *
fu_bios_plugin_get_coreboot_version(FuPlugin *plugin)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	const gchar *version = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_BIOS_VERSION);
	const gchar *version_major =
	    fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_BIOS_MAJOR_RELEASE);
	const gchar *version_minor =
	    fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_BIOS_MINOR_RELEASE);

	if (version != NULL) {
		g_autoptr(FuDevice) device = fu_device_new(ctx);

		/* star labs uses a dmi version with a cbetxxxx prefix */
		if (strlen(version) > 9 && g_str_has_prefix(version, "CBET"))
			version += 9;

		fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_ENSURE_SEMVER);
		fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_PAIR);
		fu_device_set_version(device, version);
		if (fu_device_get_version(device) != NULL)
			return g_strdup(fu_device_get_version(device));
	}

	if (version_major != NULL && version_minor != NULL)
		return g_strdup_printf("%s.%s", version_major, version_minor);

	return NULL;
}

static gboolean
fu_bios_plugin_is_starlabs_coreboot_legacy(FuPlugin *plugin, gchar **version_out)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	g_autofree gchar *version = NULL;

	if (g_strcmp0(fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_BIOS_VENDOR), "coreboot") != 0)
		return FALSE;
	if (!fu_context_has_hwid_guid(ctx, FU_BIOS_PLUGIN_STARLABS_COREBOOT_HWID))
		return FALSE;

	version = fu_bios_plugin_get_coreboot_version(plugin);
	if (version == NULL)
		return FALSE;
	if (fu_version_compare(version,
			       FU_BIOS_PLUGIN_STARLABS_COREBOOT_VERSION_MIN,
			       FWUPD_VERSION_FORMAT_PAIR) >= 0)
		return FALSE;

	if (version_out != NULL)
		*version_out = g_steal_pointer(&version);
	return TRUE;
}

static gboolean
fu_bios_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	const gchar *vendor;

	vendor = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_BIOS_VENDOR);
	if (g_strcmp0(vendor, "coreboot") == 0) {
		if (fu_bios_plugin_is_starlabs_coreboot_legacy(plugin, NULL))
			return TRUE;
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "system uses coreboot");
		return FALSE;
	}

	/* check if UEFI is supported by the hardware */
	if (!fu_context_has_flag(ctx, FU_CONTEXT_FLAG_SMBIOS_UEFI_ENABLED)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "system does not support UEFI");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_bios_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	FuEfivars *efivars = fu_context_get_efivars(ctx);
	g_autofree gchar *coreboot_version = NULL;
	g_autofree gchar *esrt_path = NULL;
	g_autoptr(GError) error_local = NULL;

	if (fu_bios_plugin_is_starlabs_coreboot_legacy(plugin, &coreboot_version)) {
		g_autoptr(FuDevice) device = fu_device_new(ctx);
		g_autoptr(GString) msg = g_string_new(NULL);
		const gchar *manufacturer =
		    fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_MANUFACTURER);
		GPtrArray *hwids = fu_context_get_hwid_guids(ctx);

		fu_device_set_id(device, "star-labs-coreboot-manual-update");
		fu_device_set_name(device, "Star Labs Coreboot Firmware");
		fu_device_set_summary(device,
				      "Manual update required before fwupd support is removed.");
		fu_device_set_homepage(device, FU_BIOS_PLUGIN_STARLABS_SUPPORT_URL);
		fu_device_set_vendor(device, manufacturer != NULL ? manufacturer : "Star Labs");
		fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_PAIR);
		fu_device_set_version(device, coreboot_version);
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_INTERNAL);
		fu_device_add_icon(device, FU_DEVICE_ICON_COMPUTER);
		fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_HOST_FIRMWARE);
		for (guint i = 0; i < hwids->len; i++) {
			const gchar *hwid = g_ptr_array_index(hwids, i);
			fu_device_add_instance_id(device, hwid);
		}

		g_string_append(msg,
				"This Star Labs coreboot firmware must be updated manually before "
				"fwupd support is removed. Follow: ");
		g_string_append(msg, FU_BIOS_PLUGIN_STARLABS_SUPPORT_URL);
		fu_device_set_update_error(device, msg->str);
		fu_device_set_update_message(device, msg->str);

		fu_plugin_add_device(plugin, device);
		return TRUE;
	}

	/* are the EFI dirs set up so we can update each device */
	if (!fu_efivars_supported(efivars, &error_local)) {
		fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_LEGACY_BIOS);
		fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_USER_WARNING);
		return TRUE;
	}

	/* get the directory of ESRT entries */
	esrt_path =
	    fu_context_build_filename(ctx, error, FU_PATH_KIND_SYSFSDIR_FW, "efi", "esrt", NULL);
	if (esrt_path == NULL)
		return FALSE;
	if (!g_file_test(esrt_path, G_FILE_TEST_IS_DIR)) {
		/* don't show the warning in a hypervisor as capsule updates are not expected */
		if (!fu_context_has_flag(ctx, FU_CONTEXT_FLAG_IS_HYPERVISOR)) {
			fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_CAPSULES_UNSUPPORTED);
			fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_USER_WARNING);
		}
		return TRUE;
	}

	/* we appear to have UEFI capsule updates */
	fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_DISABLED);
	return TRUE;
}

static void
fu_bios_plugin_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	if (!fu_plugin_has_flag(plugin, FWUPD_PLUGIN_FLAG_LEGACY_BIOS))
		return;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_UEFI_SECUREBOOT);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	fu_security_attrs_append(attrs, attr);

	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
}

static void
fu_bios_plugin_init(FuBiosPlugin *self)
{
}

static void
fu_bios_plugin_class_init(FuBiosPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->startup = fu_bios_plugin_startup;
	plugin_class->coldplug = fu_bios_plugin_coldplug;
	plugin_class->add_security_attrs = fu_bios_plugin_add_security_attrs;
}
