/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"
#include "fu-plugin-private.h"
#include "fu-uefi-device-private.h"
#include "fu-uefi-kek-plugin.h"

static void
fu_uefi_kek_external_locked_func(void)
{
	gboolean ret;
	FuDevice *child;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device_kek = NULL;
	g_autoptr(FuDevice) device_kek_child = NULL;
	g_autoptr(FuDevice) device_pk = NULL;
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	plugin = fu_plugin_new_from_gtype(fu_uefi_kek_plugin_get_type(), ctx);
	fu_plugin_runner_init(plugin);

	/* create a KEK device with a child */
	device_kek = fu_device_new(ctx);
	device_kek_child = fu_device_new(ctx);
	fu_device_set_id(device_kek, "kek");
	fu_device_set_id(device_kek_child, "kek-child");
	fu_device_add_child(device_kek, device_kek_child);

	/* add KEK device to plugin */
	fu_plugin_add_device(plugin, device_kek);
	fu_plugin_runner_device_added(plugin, device_kek);

	/* register a PK device that is external */
	device_pk = FU_DEVICE(fu_uefi_device_new(ctx, FU_EFIVARS_GUID_EFI_GLOBAL, "PK"));
	fu_device_set_plugin(device_pk, "uefi_pk");
	fu_device_add_private_flag(device_pk, FU_UEFI_DEVICE_PRIVATE_FLAG_IS_EXTERNAL);
	fu_plugin_runner_device_register(plugin, device_pk);

	/* KEK child should be locked */
	child = g_ptr_array_index(fu_device_get_children(device_kek), 0);
	g_assert_true(fu_device_has_problem(child, FWUPD_DEVICE_PROBLEM_FIRMWARE_LOCKED));
}

static void
fu_uefi_kek_external_not_locked_func(void)
{
	gboolean ret;
	FuDevice *child;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device_kek = NULL;
	g_autoptr(FuDevice) device_kek_child = NULL;
	g_autoptr(FuDevice) device_pk = NULL;
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	plugin = fu_plugin_new_from_gtype(fu_uefi_kek_plugin_get_type(), ctx);
	fu_plugin_runner_init(plugin);

	/* create a KEK device with a child */
	device_kek = fu_device_new(ctx);
	device_kek_child = fu_device_new(ctx);
	fu_device_set_id(device_kek, "kek");
	fu_device_set_id(device_kek_child, "kek-child");
	fu_device_add_child(device_kek, device_kek_child);

	/* add KEK device to plugin */
	fu_plugin_add_device(plugin, device_kek);
	fu_plugin_runner_device_added(plugin, device_kek);

	/* register a PK device that is NOT external */
	device_pk = FU_DEVICE(fu_uefi_device_new(ctx, FU_EFIVARS_GUID_EFI_GLOBAL, "PK"));
	fu_device_set_plugin(device_pk, "uefi_pk");
	fu_plugin_runner_device_register(plugin, device_pk);

	/* KEK child should NOT be locked */
	child = g_ptr_array_index(fu_device_get_children(device_kek), 0);
	g_assert_false(fu_device_has_problem(child, FWUPD_DEVICE_PROBLEM_FIRMWARE_LOCKED));
}

static void
fu_uefi_kek_external_locked_device_added_func(void)
{
	gboolean ret;
	FuDevice *child;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device_kek = NULL;
	g_autoptr(FuDevice) device_kek_child = NULL;
	g_autoptr(FuDevice) device_pk = NULL;
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	plugin = fu_plugin_new_from_gtype(fu_uefi_kek_plugin_get_type(), ctx);
	fu_plugin_runner_init(plugin);

	/* register PK device as external first */
	device_pk = FU_DEVICE(fu_uefi_device_new(ctx, FU_EFIVARS_GUID_EFI_GLOBAL, "PK"));
	fu_device_set_plugin(device_pk, "uefi_pk");
	fu_device_add_private_flag(device_pk, FU_UEFI_DEVICE_PRIVATE_FLAG_IS_EXTERNAL);
	fu_plugin_runner_device_register(plugin, device_pk);

	/* then add a KEK device with a child */
	device_kek = fu_device_new(ctx);
	device_kek_child = fu_device_new(ctx);
	fu_device_set_id(device_kek, "kek");
	fu_device_set_id(device_kek_child, "kek-child");
	fu_device_add_child(device_kek, device_kek_child);

	/* add KEK device to plugin -- child should be locked via device_added */
	fu_plugin_add_device(plugin, device_kek);
	fu_plugin_runner_device_added(plugin, device_kek);

	child = g_ptr_array_index(fu_device_get_children(device_kek), 0);
	g_assert_true(fu_device_has_problem(child, FWUPD_DEVICE_PROBLEM_FIRMWARE_LOCKED));
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/uefi-kek/external-locked", fu_uefi_kek_external_locked_func);
	g_test_add_func("/uefi-kek/external-not-locked", fu_uefi_kek_external_not_locked_func);
	g_test_add_func("/uefi-kek/external-locked-device-added",
			fu_uefi_kek_external_locked_device_added_func);
	return g_test_run();
}
