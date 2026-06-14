/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuCommon"

#include "config.h"

#include "fu-ptr-array.h"

/**
 * fu_ptr_array_copy:
 * @array: (element-type void): a #GPtrArray
 * @func: (scope call): a copy function
 * @free_func: (nullable): a free function for the new array
 *
 * Deep copies a #GPtrArray using @func to copy each element. The new array uses @free_func
 * as the element free function.
 *
 * Returns: (transfer full) (element-type void): a new #GPtrArray
 *
 * Since: 2.0.8
 **/
GPtrArray *
fu_ptr_array_copy(GPtrArray *array, GCopyFunc func, GDestroyNotify free_func)
{
	GPtrArray *array_new;

	g_return_val_if_fail(array != NULL, NULL);
	g_return_val_if_fail(func != NULL, NULL);

	array_new = g_ptr_array_new_with_free_func(free_func);
	for (guint i = 0; i < array->len; i++)
		g_ptr_array_add(array_new, func(g_ptr_array_index(array, i), NULL));
	return array_new;
}
