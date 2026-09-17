/*
 * Copyright 2026 Sean Rhodes <sean@starlabs.systems>
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-starlabs-coreboot-device.h"
#include "fu-starlabs-coreboot-plugin.h"

struct _FuStarlabsCorebootPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuStarlabsCorebootPlugin, fu_starlabs_coreboot_plugin, FU_TYPE_PLUGIN)

static const gchar *
fu_starlabs_coreboot_plugin_get_heads_version(FuContext *ctx)
{
	const gchar *version = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_BIOS_VERSION);

	if (version == NULL)
		return NULL;
	if (strlen(version) > 9 && g_str_has_prefix(version, "CBET"))
		version += 9;
	if (!g_str_has_prefix(version, "Heads-"))
		return NULL;
	return version + strlen("Heads-");
}

static gboolean
fu_starlabs_coreboot_plugin_register_heads(FuPlugin *plugin, const gchar *version, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	const gchar *product = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_PRODUCT_NAME);
	const gchar *family = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_FAMILY);
	g_autofree gchar *guid = NULL;
	g_autofree gchar *instance_id = NULL;
	g_autoptr(FuDevice) device = fu_device_new(ctx);

	if (product == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "Heads firmware has no SMBIOS product name");
		return FALSE;
	}
	if (family == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "Heads firmware has no SMBIOS family");
		return FALSE;
	}

	fu_device_set_id(device, "star-labs-heads-system-firmware");
	fu_device_set_vendor(device, "Star Labs");
	fu_device_build_vendor_id(device, "DMI", "Star Labs");
	fu_device_set_name(device, "Heads System Firmware");
	fu_device_set_summary(device, "Coreboot and Heads system firmware");
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_version(device, version);
	fu_device_set_branch(device, "heads");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_AFFECTS_FDE);
	fu_device_add_icon(device, FU_DEVICE_ICON_COMPUTER);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_HOST_FIRMWARE);
	instance_id =
	    g_strdup_printf("STARLABS\\PRODUCT_%s&FAMILY_%s&BRANCH_HEADS", product, family);
	guid = fwupd_guid_hash_string(instance_id);
	fwupd_device_add_instance_id(FWUPD_DEVICE(device), instance_id);
	fwupd_device_add_guid(FWUPD_DEVICE(device), guid);
	fu_device_set_metadata(device, FU_DEVICE_METADATA_UEFI_DEVICE_KIND, "system-firmware");
	fu_device_set_metadata_boolean(device, FU_DEVICE_METADATA_UEFI_CAPSULE_ON_DISK, TRUE);
	fu_device_set_metadata_boolean(device,
				       FU_DEVICE_METADATA_UEFI_CAPSULE_NO_RT_SET_VARIABLE,
				       TRUE);
	fu_plugin_device_register(plugin, device);
	return TRUE;
}

static void
fu_starlabs_coreboot_plugin_init(FuStarlabsCorebootPlugin *self)
{
	fu_plugin_add_flag(FU_PLUGIN(self), FWUPD_PLUGIN_FLAG_REQUIRE_HWID);
}

static void
fu_starlabs_coreboot_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_STARLABS_COREBOOT_DEVICE);
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_RUN_AFTER, "uefi_capsule");

	/* chain up to parent */
	G_OBJECT_CLASS(fu_starlabs_coreboot_plugin_parent_class)->constructed(obj);
}

static gboolean
fu_starlabs_coreboot_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	const gchar *heads_version = fu_starlabs_coreboot_plugin_get_heads_version(ctx);
	const gchar *product = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_PRODUCT_NAME);
	const gchar *family = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_FAMILY);
	g_autoptr(FuDevice) device = NULL;

	/* the consumer is only enabled for the QEMU fixture at present */
	if (heads_version != NULL && g_strcmp0(product, "starlabs_qemu") == 0 &&
	    g_strcmp0(family, "QEMU") == 0)
		return fu_starlabs_coreboot_plugin_register_heads(plugin, heads_version, error);
	if (heads_version != NULL)
		return TRUE;

	device = g_object_new(FU_TYPE_STARLABS_COREBOOT_DEVICE, "context", ctx, NULL);
	if (fu_version_compare(fu_device_get_version(device),
			       FU_STARLABS_COREBOOT_VERSION_MIN,
			       FWUPD_VERSION_FORMAT_PAIR) >= 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "device is new enough to support mtd");
		return FALSE;
	}
	fu_plugin_add_device(plugin, device);
	return TRUE;
}

static void
fu_starlabs_coreboot_plugin_class_init(FuStarlabsCorebootPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_starlabs_coreboot_plugin_constructed;
	plugin_class->coldplug = fu_starlabs_coreboot_plugin_coldplug;
}
