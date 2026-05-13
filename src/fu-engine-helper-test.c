/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-efivars-private.h"
#include "fu-engine-helper.h"

static void
fu_engine_error_array_func(void)
{
	g_autoptr(GPtrArray) arr1 = g_ptr_array_new_with_free_func((GDestroyNotify)g_error_free);
	g_autoptr(GPtrArray) arr2 = g_ptr_array_new_with_free_func((GDestroyNotify)g_error_free);
	g_autoptr(GPtrArray) arr3 = g_ptr_array_new_with_free_func((GDestroyNotify)g_error_free);
	g_autoptr(GPtrArray) arr4 = g_ptr_array_new_with_free_func((GDestroyNotify)g_error_free);
	g_autoptr(GError) error1 = NULL;
	g_autoptr(GError) error2 = NULL;
	g_autoptr(GError) error3 = NULL;
	g_autoptr(GError) error4 = NULL;

	/* fallback */
	error1 = fu_engine_error_array_get_best(arr1);
	g_assert_error(error1, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);

	/* get the most important single error */
	g_ptr_array_add(arr2, g_error_new_literal(FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, ""));
	error2 = fu_engine_error_array_get_best(arr2);
	g_assert_error(error2, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);

	/* version same */
	g_ptr_array_add(arr3, g_error_new_literal(FWUPD_ERROR, FWUPD_ERROR_VERSION_SAME, ""));
	g_ptr_array_add(arr3, g_error_new_literal(FWUPD_ERROR, FWUPD_ERROR_VERSION_SAME, ""));
	g_ptr_array_add(arr3, g_error_new_literal(FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, ""));
	error3 = fu_engine_error_array_get_best(arr3);
	g_assert_error(error3, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO);

	/* already have newer versions */
	g_ptr_array_add(arr4, g_error_new_literal(FWUPD_ERROR, FWUPD_ERROR_VERSION_NEWER, ""));
	g_ptr_array_add(arr4, g_error_new_literal(FWUPD_ERROR, FWUPD_ERROR_VERSION_NEWER, ""));
	error4 = fu_engine_error_array_get_best(arr4);
	g_assert_error(error4, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO);
}

static void
fu_engine_machine_hash_func(void)
{
	gsize sz = 0;
	g_autofree gchar *buf = NULL;
	g_autofree gchar *mhash1 = NULL;
	g_autofree gchar *mhash2 = NULL;
	g_autoptr(GError) error = NULL;

	if (!g_file_test("/etc/machine-id", G_FILE_TEST_EXISTS)) {
		g_test_skip("Missing /etc/machine-id");
		return;
	}
	if (!g_file_get_contents("/etc/machine-id", &buf, &sz, &error)) {
		g_test_skip("/etc/machine-id is unreadable");
		return;
	}

	if (sz == 0) {
		g_test_skip("Empty /etc/machine-id");
		return;
	}

	mhash1 = fu_engine_build_machine_id("salt1", &error);
	g_assert_no_error(error);
	g_assert_cmpstr(mhash1, !=, NULL);
	mhash2 = fu_engine_build_machine_id("salt2", &error);
	g_assert_no_error(error);
	g_assert_cmpstr(mhash2, !=, NULL);
	g_assert_cmpstr(mhash2, !=, mhash1);
}

static void
fu_engine_integrity_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS | FU_CONTEXT_FLAG_DUMMY_EFIVARS);
	g_autofree gchar *str = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GHashTable) integrity = NULL;

	/* build a plausible EFI system */
	ret = fu_efivars_set_secure_boot(fu_context_get_efivars(ctx), TRUE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	integrity = fu_engine_integrity_new(ctx, &error);
	g_assert_no_error(error);
	g_assert_nonnull(integrity);
	str = fu_engine_integrity_to_string(integrity);
	g_debug("%s", str);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/engine/error-array", fu_engine_error_array_func);
	g_test_add_func("/fwupd/engine/machine-hash", fu_engine_machine_hash_func);
	g_test_add_func("/fwupd/engine/integrity", fu_engine_integrity_func);
	return g_test_run();
}
