/*
 * Copyright 2026 Sean Rhodes <sean@starlabs.systems>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-plugin-private.h"
#include "fu-starlabs-coreboot-plugin.h"

static void
fu_test_bios_plugin_device_added_cb(FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	FuDevice **device_added = (FuDevice **)user_data;
	*device_added = device;
}

static void
fu_test_bios_plugin_device_register_cb(FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	FuDevice **device_registered = (FuDevice **)user_data;
	*device_registered = g_object_ref(device);
}

static FuPlugin *
fu_starlabs_coreboot_plugin_new(const gchar *manufacturer,
				const gchar *product,
				const gchar *family,
				const gchar *bios_version)
{
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	FuHwids *hwids = fu_context_get_hwids(ctx);

	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_LOADED_HWINFO);
	fu_hwids_add_value(hwids, FU_HWIDS_KEY_BIOS_VENDOR, "coreboot");
	fu_hwids_add_value(hwids, FU_HWIDS_KEY_MANUFACTURER, manufacturer);
	fu_hwids_add_value(hwids, FU_HWIDS_KEY_PRODUCT_NAME, product);
	if (family != NULL)
		fu_hwids_add_value(hwids, FU_HWIDS_KEY_FAMILY, family);
	if (bios_version != NULL)
		fu_hwids_add_value(hwids, FU_HWIDS_KEY_BIOS_VERSION, bios_version);

	plugin = fu_plugin_new_from_gtype(fu_starlabs_coreboot_plugin_get_type(), ctx);
	fu_plugin_remove_flag(plugin, FWUPD_PLUGIN_FLAG_REQUIRE_HWID);
	return g_steal_pointer(&plugin);
}

static void
fu_bios_plugin_starlabs_coreboot_legacy_func(void)
{
	gboolean ret;
	gulong added_id;
	FuDevice *device_added = NULL;
	g_autoptr(FuPlugin) plugin =
	    fu_starlabs_coreboot_plugin_new("Star Labs", "StarBook", "B6-I", "CBET4000 25.01");
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	added_id = g_signal_connect(plugin,
				    "device-added",
				    G_CALLBACK(fu_test_bios_plugin_device_added_cb),
				    &device_added);
	ret = fu_plugin_runner_coldplug(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_nonnull(device_added);
	g_assert_cmpstr(fu_device_get_name(device_added), ==, "Coreboot");
	g_assert_cmpstr(fu_device_get_summary(device_added), ==, "Manual update required");
	g_assert_nonnull(fu_device_get_details_url(device_added));
	g_assert_cmpstr(fu_device_get_version(device_added), ==, "25.01");
	g_assert_true(
	    fu_device_has_private_flag(device_added, FU_DEVICE_PRIVATE_FLAG_HOST_FIRMWARE));
	g_assert_nonnull(fu_device_get_update_error(device_added));
	g_assert_nonnull(
	    g_strstr_len(fu_device_get_update_error(device_added), -1, "updating-your-firmware"));
	g_signal_handler_disconnect(plugin, added_id);
}

static void
fu_bios_plugin_starlabs_coreboot_supported_func(void)
{
	GPtrArray *devices;
	gboolean ret;
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	plugin = fu_starlabs_coreboot_plugin_new("Star Labs", "StarBook", "B6-I", "CBET4000 26.02");
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_coldplug(plugin, progress, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
	devices = fu_plugin_get_devices(plugin);
	g_assert_cmpint(devices->len, ==, 0);
}

static void
fu_bios_plugin_starlabs_coreboot_heads_func(void)
{
	gboolean ret;
	gulong registered_id;
	FuDevice *device_registered = NULL;
	g_autofree gchar *expected_guid = NULL;
	g_autofree gchar *wrong_family_guid = NULL;
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	plugin = fu_starlabs_coreboot_plugin_new("Star Labs",
						 "starlabs_qemu",
						 "QEMU",
						 "CBET4000 Heads-v0.2.0-42-g1234567");
	registered_id = g_signal_connect(plugin,
					 "device-register",
					 G_CALLBACK(fu_test_bios_plugin_device_register_cb),
					 &device_registered);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_coldplug(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_nonnull(device_registered);
	g_assert_cmpstr(fu_device_get_name(device_registered), ==, "Heads System Firmware");
	g_assert_cmpstr(fu_device_get_version(device_registered), ==, "v0.2.0-42-g1234567");
	g_assert_cmpstr(fu_device_get_branch(device_registered), ==, "heads");
	g_assert_true(fu_device_has_vendor_id(device_registered, "DMI:Star Labs"));
	g_assert_true(fu_device_get_metadata_boolean(device_registered,
						     FU_DEVICE_METADATA_UEFI_CAPSULE_ON_DISK));
	g_assert_true(
	    fu_device_get_metadata_boolean(device_registered,
					   FU_DEVICE_METADATA_UEFI_CAPSULE_NO_RT_SET_VARIABLE));
	expected_guid =
	    fwupd_guid_hash_string("STARLABS\\PRODUCT_starlabs_qemu&FAMILY_QEMU&BRANCH_HEADS");
	wrong_family_guid =
	    fwupd_guid_hash_string("STARLABS\\PRODUCT_starlabs_qemu&FAMILY_B6-I&BRANCH_HEADS");
	g_assert_true(fwupd_device_has_guid(FWUPD_DEVICE(device_registered), expected_guid));
	g_assert_false(fwupd_device_has_guid(FWUPD_DEVICE(device_registered), wrong_family_guid));
	g_assert_true(fu_device_has_flag(device_registered, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_signal_handler_disconnect(plugin, registered_id);
	g_object_unref(device_registered);
}

static void
fu_bios_plugin_starlabs_coreboot_heads_physical_func(void)
{
	gboolean ret;
	FuDevice *device_registered = NULL;
	gulong registered_id;
	g_autoptr(FuPlugin) plugin =
	    fu_starlabs_coreboot_plugin_new("Star Labs",
					    "StarBook",
					    "B7-U",
					    "CBET4000 Heads-v0.2.0-42-g1234567");
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	registered_id = g_signal_connect(plugin,
					 "device-register",
					 G_CALLBACK(fu_test_bios_plugin_device_register_cb),
					 &device_registered);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_coldplug(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_null(device_registered);
	g_signal_handler_disconnect(plugin, registered_id);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/fwupd/plugin/bios/starlabs/coreboot-legacy",
			fu_bios_plugin_starlabs_coreboot_legacy_func);
	g_test_add_func("/fwupd/plugin/bios/starlabs/coreboot-supported",
			fu_bios_plugin_starlabs_coreboot_supported_func);
	g_test_add_func("/fwupd/plugin/bios/starlabs/coreboot-heads",
			fu_bios_plugin_starlabs_coreboot_heads_func);
	g_test_add_func("/fwupd/plugin/bios/starlabs/coreboot-heads-physical",
			fu_bios_plugin_starlabs_coreboot_heads_physical_func);
	return g_test_run();
}
