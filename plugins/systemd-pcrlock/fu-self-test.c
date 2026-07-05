/*
 * Copyright 2026 Luca Boccassi <luca.boccassi@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-plugin-private.h"
#include "fu-systemd-pcrlock-plugin.h"

static FuDevice *
fu_systemd_pcrlock_test_device_new(FuContext *ctx, const gchar *plugin_name, gboolean affects_fde)
{
	FuDevice *device = fu_device_new(ctx);
	fu_device_set_plugin(device, plugin_name);
	if (affects_fde)
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_AFFECTS_FDE);
	return device;
}

/* systemd-pcrlock is not protecting this system, so the plugin disables itself
 * by failing startup */
static void
fu_systemd_pcrlock_plugin_startup_not_used_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(FU_TYPE_SYSTEMD_PCRLOCK_PLUGIN, ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	/* FU_CONTEXT_FLAG_FDE_SYSTEMD_PCRLOCK is deliberately not set */
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
}

/* pcrlock is protecting the system but the systemd-pcrlock service cannot be
 * reached, so the plugin disables itself by failing startup */
static void
fu_systemd_pcrlock_plugin_startup_no_service_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(FU_TYPE_SYSTEMD_PCRLOCK_PLUGIN, ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_FDE_SYSTEMD_PCRLOCK);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
}

/* pcrlock is protecting the system, but nothing being updated changes a measured
 * boot state, so the sealed policy must be left untouched */
static void
fu_systemd_pcrlock_plugin_no_affects_fde_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(FU_TYPE_SYSTEMD_PCRLOCK_PLUGIN, ctx);
	g_autoptr(FuDevice) device = fu_systemd_pcrlock_test_device_new(ctx, "test", FALSE);
	g_autoptr(GPtrArray) devices =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	g_autoptr(GError) error = NULL;

	g_ptr_array_add(devices, g_steal_pointer(&device));

	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_FDE_SYSTEMD_PCRLOCK);
	ret = fu_plugin_runner_composite_prepare(plugin, devices, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

/* pcrlock is protecting the system and a SecureBoot database update is about to
 * be applied, so the plugin must try to loosen the policy before the write, and
 * when the systemd-pcrlock service cannot be reached the whole update has to be
 * aborted (rather than silently continuing and leaving the disk unlockable) */
static void
fu_systemd_pcrlock_plugin_abort_on_error_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(FU_TYPE_SYSTEMD_PCRLOCK_PLUGIN, ctx);
	g_autoptr(FuDevice) device = fu_systemd_pcrlock_test_device_new(ctx, "uefi_dbx", TRUE);
	g_autoptr(GPtrArray) devices =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	g_autoptr(GError) error = NULL;

	g_ptr_array_add(devices, g_steal_pointer(&device));

	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_FDE_SYSTEMD_PCRLOCK);
	ret = fu_plugin_runner_composite_prepare(plugin, devices, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_nonnull(g_strstr_len(error->message, -1, "systemd-pcrlock policy"));
	g_assert_false(ret);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	(void)g_setenv("FWUPD_SYSTEMD_PCRLOCK_VARLINK_ADDRESS",
		       "/nonexistent/fwupd-systemd-pcrlock-self-test/io.systemd.PCRLock",
		       TRUE);

	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/systemd-pcrlock/startup{not-pcrlock}",
			fu_systemd_pcrlock_plugin_startup_not_used_func);
	g_test_add_func("/systemd-pcrlock/startup{no-service}",
			fu_systemd_pcrlock_plugin_startup_no_service_func);
	g_test_add_func("/systemd-pcrlock/composite-prepare{no-affects-fde}",
			fu_systemd_pcrlock_plugin_no_affects_fde_func);
	g_test_add_func("/systemd-pcrlock/composite-prepare{abort-on-error}",
			fu_systemd_pcrlock_plugin_abort_on_error_func);
	return g_test_run();
}
