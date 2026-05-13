/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"
#include "fu-uefi-device-private.h"

static void
fu_uefi_device_auth_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_DUMMY_EFIVARS | FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuUefiDevice) uefi_device = NULL;
	g_autoptr(GError) error = NULL;
	const guint8 buf[] = {0xFF};

	ret = fu_efivars_set_data(fu_context_get_efivars(ctx),
				  FU_EFIVARS_GUID_EFI_GLOBAL,
				  "db",
				  buf,
				  sizeof(buf),
				  FU_EFI_VARIABLE_ATTR_RUNTIME_ACCESS |
				      FU_EFI_VARIABLE_ATTR_BOOTSERVICE_ACCESS |
				      FU_EFI_VARIABLE_ATTR_NON_VOLATILE |
				      FU_EFI_VARIABLE_ATTR_TIME_BASED_AUTHENTICATED_WRITE_ACCESS,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	uefi_device = fu_uefi_device_new(ctx, FU_EFIVARS_GUID_EFI_GLOBAL, "db");
	ret = fu_device_probe(FU_DEVICE(uefi_device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_false(
	    fu_device_has_problem(FU_DEVICE(uefi_device), FWUPD_DEVICE_PROBLEM_INSECURE_PLATFORM));
}

static void
fu_uefi_device_no_auth_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_DUMMY_EFIVARS | FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuUefiDevice) uefi_device = NULL;
	g_autoptr(GError) error = NULL;
	const guint8 buf[] = {0xFF};

	ret = fu_efivars_set_data(fu_context_get_efivars(ctx),
				  FU_EFIVARS_GUID_EFI_GLOBAL,
				  "db",
				  buf,
				  sizeof(buf),
				  FU_EFI_VARIABLE_ATTR_RUNTIME_ACCESS |
				      FU_EFI_VARIABLE_ATTR_BOOTSERVICE_ACCESS |
				      FU_EFI_VARIABLE_ATTR_NON_VOLATILE,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	uefi_device = fu_uefi_device_new(ctx, FU_EFIVARS_GUID_EFI_GLOBAL, "db");
	ret = fu_device_probe(FU_DEVICE(uefi_device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(
	    fu_device_has_problem(FU_DEVICE(uefi_device), FWUPD_DEVICE_PROBLEM_INSECURE_PLATFORM));
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/uefi-device/auth", fu_uefi_device_auth_func);
	g_test_add_func("/fwupd/uefi-device/no-auth", fu_uefi_device_no_auth_func);
	return g_test_run();
}
