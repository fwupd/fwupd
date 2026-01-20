/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"
#include "fu-intel-me-pci-plugin.h"
#include "fu-plugin-private.h"
#include "fu-smbios-private.h"

static void
fu_intel_me_pci_plugin_func(void)
{
	FuDevice *pci_device;
	GPtrArray *devices;
	gboolean ret;
	g_autofree gchar *json = NULL;
	g_autofree gchar *path = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuBackend) backend =
	    g_object_new(FU_TYPE_BACKEND, "context", ctx, "device-gtype", FU_TYPE_PCI_DEVICE, NULL);
	g_autoptr(FuPlugin) plugin =
	    g_object_new(FU_TYPE_INTEL_ME_PCI_PLUGIN, "context", ctx, NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	path = g_test_build_filename(G_TEST_DIST, "tests", "intel-me-setup.json", NULL);
	ret = g_file_get_contents(path, &json, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fwupd_codec_from_json_string(FWUPD_CODEC(backend), json, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	pci_device = fu_backend_lookup_by_id(backend, "/sys/devices/pci0000:00/0000:00:16.0");
	g_assert_nonnull(pci_device);

	ret = fu_plugin_runner_backend_device_added(plugin, pci_device, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	devices = fu_plugin_get_devices(plugin);
	g_assert_cmpint(devices->len, ==, 1);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);

	/* tests go here */
	g_test_add_func("/intel-me-pci/plugin", fu_intel_me_pci_plugin_func);
	return g_test_run();
}
