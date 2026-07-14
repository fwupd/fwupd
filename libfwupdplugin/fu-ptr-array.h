/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

GPtrArray *
fu_ptr_array_copy(GPtrArray *array, GCopyFunc func, GDestroyNotify free_func) G_GNUC_NON_NULL(1, 2);
