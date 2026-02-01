/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"
#include "fu-intel-me-smbios-plugin.h"
#include "fu-plugin-private.h"
#include "fu-smbios-private.h"

static void
fu_intel_me_smbios_plugin_subtype18_func(void)
{
	gboolean ret;
	FuDevice *device_tmp;
	GPtrArray *devices;
	g_autofree gchar *str = NULL;
	g_autofree gchar *fn = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin =
	    g_object_new(FU_TYPE_INTEL_ME_SMBIOS_PLUGIN, "context", ctx, NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuSmbios) smbios = NULL;
	g_autoptr(GError) error = NULL;

	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fn = g_test_build_filename(G_TEST_DIST, "tests", "smbios-subtype-0x18.builder.xml", NULL);
	smbios = FU_SMBIOS(fu_firmware_new_from_filename(fn, &error));
	g_assert_no_error(error);
	g_assert_nonnull(smbios);
	fu_context_set_smbios(ctx, smbios);

	ret = fu_plugin_runner_coldplug(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	devices = fu_plugin_get_devices(plugin);
	g_assert_cmpint(devices->len, ==, 1);
	device_tmp = g_ptr_array_index(devices, 0);
	g_assert_cmpstr(fu_device_get_version(device_tmp), ==, "16.1.32.2473");
	g_assert_true(FU_IS_INTEL_ME_DEVICE(device_tmp));
	g_assert_cmpint(fu_intel_me_device_get_family(FU_INTEL_ME_DEVICE(device_tmp)),
			==,
			FU_INTEL_ME_FAMILY_CSME16);

	str = fu_device_to_string(device_tmp);
	g_debug("%s", str);
}

static void
fu_intel_me_smbios_plugin_subtype30_func(void)
{
	gboolean ret;
	FuDevice *device_tmp;
	GPtrArray *devices;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin =
	    g_object_new(FU_TYPE_INTEL_ME_SMBIOS_PLUGIN, "context", ctx, NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuSmbios) smbios = NULL;
	g_autoptr(GError) error = NULL;

	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fn = g_test_build_filename(G_TEST_DIST, "tests", "smbios-subtype-0x30.builder.xml", NULL);
	smbios = FU_SMBIOS(fu_firmware_new_from_filename(fn, &error));
	g_assert_no_error(error);
	g_assert_nonnull(smbios);
	fu_context_set_smbios(ctx, smbios);

	ret = fu_plugin_runner_coldplug(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	devices = fu_plugin_get_devices(plugin);
	g_assert_cmpint(devices->len, ==, 1);
	device_tmp = g_ptr_array_index(devices, 0);
	g_assert_cmpstr(fu_device_get_version(device_tmp), ==, "255.255.255.65535");
	g_assert_true(FU_IS_INTEL_ME_DEVICE(device_tmp));

	str = fu_device_to_string(device_tmp);
	g_debug("%s", str);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);

	/* tests go here */
	g_test_add_func("/intel-me-smbios/plugin{subtype-0x18}",
			fu_intel_me_smbios_plugin_subtype18_func);
	g_test_add_func("/intel-me-smbios/plugin{subtype-0x30}",
			fu_intel_me_smbios_plugin_subtype30_func);
	return g_test_run();
}
