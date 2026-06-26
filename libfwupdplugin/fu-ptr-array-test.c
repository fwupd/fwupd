/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-test.h"

static void
fu_ptr_array_copy_func(void)
{
	g_autoptr(GPtrArray) array = g_ptr_array_new_with_free_func(g_free);
	g_autoptr(GPtrArray) array_copy = NULL;

	g_ptr_array_add(array, g_strdup("hello"));
	g_ptr_array_add(array, g_strdup("world"));
	g_assert_cmpint(array->len, ==, 2);

	array_copy = fu_ptr_array_copy(array, (GCopyFunc)g_strdup, g_free);
	g_assert_nonnull(array_copy);
	g_assert_cmpint(array_copy->len, ==, 2);
	g_assert_cmpstr(g_ptr_array_index(array_copy, 0), ==, "hello");
	g_assert_cmpstr(g_ptr_array_index(array_copy, 1), ==, "world");

	/* check it is a deep copy */
	g_assert_true(g_ptr_array_index(array, 0) != g_ptr_array_index(array_copy, 0));
	g_assert_true(g_ptr_array_index(array, 1) != g_ptr_array_index(array_copy, 1));
}

static void
fu_ptr_array_copy_empty_func(void)
{
	g_autoptr(GPtrArray) array = g_ptr_array_new_with_free_func(g_free);
	g_autoptr(GPtrArray) array_copy = NULL;

	array_copy = fu_ptr_array_copy(array, (GCopyFunc)g_strdup, g_free);
	g_assert_nonnull(array_copy);
	g_assert_cmpint(array_copy->len, ==, 0);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/ptr-array/copy", fu_ptr_array_copy_func);
	g_test_add_func("/fwupd/ptr-array/copy-empty", fu_ptr_array_copy_empty_func);
	return g_test_run();
}
