/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-uefi-backend.h"
#include "fu-uefi-device-private.h"

static void
fu_uefi_backend_create_device_func(void)
{
	const gchar *backend_id;
	g_autoptr(FuBackend) backend = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuDevice) dev_src = NULL;
	g_autoptr(GError) error = NULL;

	backend = fu_uefi_backend_new(ctx);

	/* a device built by the backend round-trips through its backend-id */
	dev_src = FU_DEVICE(fu_uefi_device_new(ctx, "8be4df61-93ca-11d2-aa0d-00e098032b8c", "PK"));
	backend_id = fu_device_get_backend_id(dev_src);
	g_assert_cmpstr(backend_id, ==, "8be4df61-93ca-11d2-aa0d-00e098032b8c-PK");

	dev = fu_backend_create_device(backend, backend_id, &error);
	g_assert_no_error(error);
	g_assert_nonnull(dev);
	g_assert_true(FU_IS_UEFI_DEVICE(dev));
	g_assert_cmpstr(fu_uefi_device_get_guid(FU_UEFI_DEVICE(dev)),
			==,
			"8be4df61-93ca-11d2-aa0d-00e098032b8c");
	g_assert_cmpstr(fu_uefi_device_get_name(FU_UEFI_DEVICE(dev)), ==, "PK");
	g_assert_cmpstr(fu_device_get_backend_id(dev), ==, backend_id);
}

static void
fu_uefi_backend_create_device_invalid_func(void)
{
	g_autoptr(FuBackend) backend = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) dev1 = NULL;
	g_autoptr(FuDevice) dev2 = NULL;
	g_autoptr(GError) error1 = NULL;
	g_autoptr(GError) error2 = NULL;

	backend = fu_uefi_backend_new(ctx);

	/* too short / no GUID separator */
	dev1 = fu_backend_create_device(backend, "nonsense", &error1);
	g_assert_error(error1, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(dev1);

	/* correct length and separator, but the GUID prefix is not valid hex */
	dev2 =
	    fu_backend_create_device(backend, "zzzzzzzz-93ca-11d2-aa0d-00e098032b8c-PK", &error2);
	g_assert_error(error2, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(dev2);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/uefi-backend/create-device", fu_uefi_backend_create_device_func);
	g_test_add_func("/fwupd/uefi-backend/create-device/invalid",
			fu_uefi_backend_create_device_invalid_func);
	return g_test_run();
}
