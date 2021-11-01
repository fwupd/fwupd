/*
 * Copyright (C) 2021 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <glib/gstdio.h>
#include <stdlib.h>

#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-plugin-private.h"

typedef struct {
	FuPlugin *plugin_uefi_capsule;
	FuPlugin *plugin_lenovo_thinklmi;
} FuTest;

static void
_plugin_device_added_cb(FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	FuDevice **dev = (FuDevice **)user_data;
	*dev = device;
}

static void
fu_test_self_init(FuTest *self)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *pluginfn_uefi = NULL;
	g_autofree gchar *pluginfn_lenovo = NULL;

	ret = fu_context_load_quirks(ctx,
				     FU_QUIRKS_LOAD_FLAG_NO_CACHE | FU_QUIRKS_LOAD_FLAG_NO_VERIFY,
				     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	self->plugin_uefi_capsule = fu_plugin_new(ctx);
	pluginfn_uefi = g_test_build_filename(G_TEST_BUILT,
					      "..",
					      "uefi-capsule",
					      "libfu_plugin_uefi_capsule." G_MODULE_SUFFIX,
					      NULL);
	ret = fu_plugin_open(self->plugin_uefi_capsule, pluginfn_uefi, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_startup(self->plugin_uefi_capsule, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	self->plugin_lenovo_thinklmi = fu_plugin_new(ctx);
	pluginfn_lenovo = g_test_build_filename(G_TEST_BUILT,
						"libfu_plugin_lenovo_thinklmi." G_MODULE_SUFFIX,
						NULL);
	ret = fu_plugin_open(self->plugin_lenovo_thinklmi, pluginfn_lenovo, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_startup(self->plugin_lenovo_thinklmi, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static FuDevice *
fu_test_probe_fake_esrt(FuTest *self)
{
	gboolean ret;
	gulong added_id;
	FuDevice *dev = NULL;
	g_autoptr(GError) error = NULL;

	added_id = g_signal_connect(self->plugin_uefi_capsule,
				    "device-added",
				    G_CALLBACK(_plugin_device_added_cb),
				    &dev);

	ret = fu_plugin_runner_coldplug(self->plugin_uefi_capsule, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_nonnull(dev);
	g_assert_true(fu_device_has_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_signal_handler_disconnect(self->plugin_uefi_capsule, added_id);
	return g_object_ref(dev);
}

static void
fu_plugin_lenovo_thinklmi_bootorder_locked(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	g_autoptr(FuDevice) dev = NULL;
	g_autofree gchar *test_dir =
	    g_test_build_filename(G_TEST_DIST, "tests", "firmware-attributes", "locked", NULL);
	g_setenv("FWUPD_SYSFSFWATTRIBDIR", test_dir, TRUE);

	dev = fu_test_probe_fake_esrt(self);
	fu_plugin_runner_device_register(self->plugin_lenovo_thinklmi, dev);
	g_assert_true(fu_device_has_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));
}

static void
fu_plugin_lenovo_thinklmi_bootorder_unlocked(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	g_autoptr(FuDevice) dev = NULL;
	g_autofree gchar *test_dir =
	    g_test_build_filename(G_TEST_DIST, "tests", "firmware-attributes", "unlocked", NULL);
	g_setenv("FWUPD_SYSFSFWATTRIBDIR", test_dir, TRUE);

	dev = fu_test_probe_fake_esrt(self);
	fu_plugin_runner_device_register(self->plugin_lenovo_thinklmi, dev);
	g_assert_true(fu_device_has_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE));
}

static void
fu_test_self_free(FuTest *self)
{
	if (self->plugin_uefi_capsule != NULL)
		g_object_unref(self->plugin_uefi_capsule);
	if (self->plugin_lenovo_thinklmi != NULL)
		g_object_unref(self->plugin_lenovo_thinklmi);
	g_free(self);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuTest, fu_test_self_free)
#pragma clang diagnostic pop

int
main(int argc, char **argv)
{
	g_autofree gchar *sysfsdir = NULL;
	g_autofree gchar *testdatadir = NULL;
	g_autofree gchar *test_dir = NULL;
	g_autoptr(FuTest) self = g_new0(FuTest, 1);

	g_test_init(&argc, &argv, NULL);

	/* starting thinklmi dir to make startup pass */
	test_dir =
	    g_test_build_filename(G_TEST_DIST, "tests", "firmware-attributes", "locked", NULL);
	g_setenv("FWUPD_SYSFSFWATTRIBDIR", test_dir, TRUE);

	/* starting ESRT path */
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	g_setenv("FWUPD_SYSFSFWDIR", testdatadir, TRUE);

	/* change behavior of UEFI plugin for test mode */
	sysfsdir = fu_common_get_path(FU_PATH_KIND_SYSFSDIR_FW);
	g_setenv("FWUPD_UEFI_ESP_PATH", sysfsdir, TRUE);
	g_setenv("FWUPD_UEFI_TEST", "1", TRUE);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	g_assert_cmpint(g_mkdir_with_parents("/tmp/fwupd-self-test/var/lib/fwupd", 0755), ==, 0);

	/* tests go here */
	fu_test_self_init(self);
	g_test_add_data_func("/fwupd/plugin{lenovo-think-lmi:bootorder-locked}",
			     self,
			     fu_plugin_lenovo_thinklmi_bootorder_locked);
	g_test_add_data_func("/fwupd/plugin{lenovo-think-lmi:bootorder-unlocked}",
			     self,
			     fu_plugin_lenovo_thinklmi_bootorder_unlocked);
	return g_test_run();
}
