/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-bios-plugin.h"
#include "fu-context-private.h"
#include "fu-plugin-private.h"

#define FU_TEST_STARLABS_COREBOOT_HWID "b8cf5af6-8a46-5deb-ac01-a35b1ea5fb48"
#define FU_TEST_STARLABS_SUPPORT_URL                                                               \
	"https://support.starlabs.systems/hc/star-labs/articles/updating-your-firmware"

static void
fu_test_bios_plugin_device_added_cb(FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	FuDevice **device_added = (FuDevice **)user_data;
	*device_added = device;
}

static FuPlugin *
fu_test_bios_plugin_new(const gchar *guid, const gchar *bios_version)
{
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	FuHwids *hwids = fu_context_get_hwids(ctx);

	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_LOADED_HWINFO);
	fu_hwids_add_value(hwids, FU_HWIDS_KEY_BIOS_VENDOR, "coreboot");
	fu_hwids_add_value(hwids, FU_HWIDS_KEY_MANUFACTURER, "Star Labs");
	fu_hwids_add_value(hwids, FU_HWIDS_KEY_PRODUCT_NAME, "StarBook");
	if (bios_version != NULL)
		fu_hwids_add_value(hwids, FU_HWIDS_KEY_BIOS_VERSION, bios_version);
	if (guid != NULL)
		fu_hwids_add_guid(hwids, guid);

	return fu_plugin_new_from_gtype(fu_bios_plugin_get_type(), g_steal_pointer(&ctx));
}

static void
fu_bios_plugin_starlabs_coreboot_legacy_func(void)
{
	gboolean ret;
	gulong added_id;
	FuDevice *device_added = NULL;
	g_autoptr(FuPlugin) plugin =
	    fu_test_bios_plugin_new(FU_TEST_STARLABS_COREBOOT_HWID, "CBET4000 25.01");
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
	g_assert_cmpstr(fu_device_get_name(device_added), ==, "Coreboot Firmware");
	g_assert_cmpstr(fu_device_get_summary(device_added),
			==,
			"Manual update required before fwupd support is removed.");
	g_assert_cmpstr(fu_device_get_homepage(device_added), ==, FU_TEST_STARLABS_SUPPORT_URL);
	g_assert_cmpstr(fu_device_get_version(device_added), ==, "25.01");
	g_assert_true(
	    fu_device_has_private_flag(device_added, FU_DEVICE_PRIVATE_FLAG_HOST_FIRMWARE));
	g_assert_nonnull(fu_device_get_update_error(device_added));
	g_assert_nonnull(
	    strstr(fu_device_get_update_error(device_added), FU_TEST_STARLABS_SUPPORT_URL));
	g_signal_handler_disconnect(plugin, added_id);
}

static void
fu_bios_plugin_starlabs_coreboot_supported_func(void)
{
	gboolean ret;
	g_autoptr(FuPlugin) plugin =
	    fu_test_bios_plugin_new(FU_TEST_STARLABS_COREBOOT_HWID, "CBET4000 26.02");
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_false(ret);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/fwupd/plugin/bios/starlabs-coreboot-legacy",
			fu_bios_plugin_starlabs_coreboot_legacy_func);
	g_test_add_func("/fwupd/plugin/bios/starlabs-coreboot-supported",
			fu_bios_plugin_starlabs_coreboot_supported_func);

	return g_test_run();
}
