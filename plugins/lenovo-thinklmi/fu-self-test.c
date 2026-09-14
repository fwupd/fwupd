/*
 * Copyright 2021 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <glib/gstdio.h>
#include <stdlib.h>

#include "../linux-fwattr/fu-linux-fwattr-plugin.h"
#include "../uefi-capsule/fu-uefi-capsule-plugin.h"
#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-lenovo-thinklmi-plugin.h"
#include "fu-plugin-private.h"

static void
fu_test_plugin_device_added_cb(FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	FuDevice **dev = (FuDevice **)user_data;
	*dev = device;
}

static FuContext *
fu_lenovo_thinklmi_context_new(void)
{
	g_autofree gchar *testdir = NULL;
	g_autofree gchar *testdir_conf = NULL;
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS | FU_CONTEXT_FLAG_NO_CACHE);

	/* loading EFI */
	testdir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR_FW, testdir);
	fu_context_set_path(ctx, FU_PATH_KIND_UEFI_ESP, testdir);

	testdir_conf = g_test_build_filename(G_TEST_DIST, "tests", "etc", "fwupd", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSCONFDIR_PKG, testdir_conf);
	return g_steal_pointer(&ctx);
}

static FuPlugin *
fu_lenovo_thinklmi_plugin_new(FuContext *ctx)
{
	return fu_plugin_new_from_gtype(fu_lenovo_thinklmi_plugin_get_type(), ctx);
}

static FuPlugin *
fu_lenovo_thinklmi_uefi_capsule_plugin_new(FuContext *ctx)
{
	return fu_plugin_new_from_gtype(fu_uefi_capsule_plugin_get_type(), ctx);
}

static FuPlugin *
fu_lenovo_thinklmi_linux_fwattr_plugin_new(FuContext *ctx)
{
	return fu_plugin_new_from_gtype(fu_linux_fwattr_plugin_get_type(), ctx);
}

static FuDevice *
fu_test_probe_fake_esrt(FuPlugin *plugin_uefi_capsule)
{
	gboolean ret;
	gulong added_id;
	FuDevice *dev = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	added_id = g_signal_connect(FU_PLUGIN(plugin_uefi_capsule),
				    "device-added",
				    G_CALLBACK(fu_test_plugin_device_added_cb),
				    &dev);

	ret = fu_plugin_runner_coldplug(plugin_uefi_capsule, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_nonnull(dev);
	g_assert_true(fu_device_has_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_signal_handler_disconnect(plugin_uefi_capsule, added_id);
	return g_object_ref(dev);
}

static void
fu_lenovo_thinklmi_bootorder_locked_func(void)
{
	gboolean ret;
	g_autofree gchar *testdir_fw_attrib = NULL;
	g_autoptr(FuContext) ctx = fu_lenovo_thinklmi_context_new();
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuPlugin) plugin_linux_fwattr = fu_lenovo_thinklmi_linux_fwattr_plugin_new(ctx);
	g_autoptr(FuPlugin) plugin_lenovo_thinklmi = fu_lenovo_thinklmi_plugin_new(ctx);
	g_autoptr(FuPlugin) plugin_uefi_capsule = fu_lenovo_thinklmi_uefi_capsule_plugin_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	testdir_fw_attrib =
	    g_test_build_filename(G_TEST_DIST, "tests", "firmware-attributes", "locked", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR_FW_ATTRIB, testdir_fw_attrib);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_HWID_CONFIG, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_SMBIOS_UEFI_ENABLED);

	ret = fu_plugin_runner_startup(plugin_linux_fwattr, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_startup(plugin_uefi_capsule, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_startup(plugin_lenovo_thinklmi, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	dev = fu_test_probe_fake_esrt(plugin_uefi_capsule);
	fu_plugin_runner_device_register(plugin_lenovo_thinklmi, dev);
	g_assert_true(fu_device_has_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));
}

static void
fu_lenovo_thinklmi_bootorder_unlocked_func(void)
{
	gboolean ret;
	g_autofree gchar *testdir_fw_attrib = NULL;
	g_autoptr(FuContext) ctx = fu_lenovo_thinklmi_context_new();
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuPlugin) plugin_lenovo_thinklmi = fu_lenovo_thinklmi_plugin_new(ctx);
	g_autoptr(FuPlugin) plugin_uefi_capsule = fu_lenovo_thinklmi_uefi_capsule_plugin_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	testdir_fw_attrib =
	    g_test_build_filename(G_TEST_DIST, "tests", "firmware-attributes", "unlocked", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR_FW_ATTRIB, testdir_fw_attrib);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_HWID_CONFIG, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_SMBIOS_UEFI_ENABLED);

	ret = fu_plugin_runner_startup(plugin_uefi_capsule, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_startup(plugin_lenovo_thinklmi, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	dev = fu_test_probe_fake_esrt(plugin_uefi_capsule);
	fu_plugin_runner_device_register(plugin_lenovo_thinklmi, dev);
	g_assert_true(fu_device_has_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE));
}

static void
fu_lenovo_thinklmi_reboot_pending_func(void)
{
	gboolean ret;
	g_autofree gchar *testdir_fw_attrib = NULL;
	g_autoptr(FuContext) ctx = fu_lenovo_thinklmi_context_new();
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuPlugin) plugin_lenovo_thinklmi = fu_lenovo_thinklmi_plugin_new(ctx);
	g_autoptr(FuPlugin) plugin_linux_fwattr = fu_lenovo_thinklmi_linux_fwattr_plugin_new(ctx);
	g_autoptr(FuPlugin) plugin_uefi_capsule = fu_lenovo_thinklmi_uefi_capsule_plugin_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	testdir_fw_attrib = g_test_build_filename(G_TEST_DIST,
						  "tests",
						  "firmware-attributes",
						  "reboot-pending",
						  NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR_FW_ATTRIB, testdir_fw_attrib);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_HWID_CONFIG, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_SMBIOS_UEFI_ENABLED);

	ret = fu_plugin_runner_startup(plugin_linux_fwattr, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_startup(plugin_uefi_capsule, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_startup(plugin_lenovo_thinklmi, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	dev = fu_test_probe_fake_esrt(plugin_uefi_capsule);
	fu_plugin_runner_device_register(plugin_lenovo_thinklmi, dev);
	g_assert_true(fu_device_has_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	(void)g_setenv("FWUPD_UEFI_TEST", "1", TRUE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/lenovo-think-lmi/bootorder/locked",
			fu_lenovo_thinklmi_bootorder_locked_func);
	g_test_add_func("/fwupd/lenovo-think-lmi/bootorder/unlocked",
			fu_lenovo_thinklmi_bootorder_unlocked_func);
	g_test_add_func("/fwupd/lenovo-think-lmi/reboot-pending",
			fu_lenovo_thinklmi_reboot_pending_func);
	return g_test_run();
}
