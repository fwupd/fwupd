/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-kernel-search-path.h"

G_BEGIN_DECLS

FuKernelSearchPathLocker *
fu_kernel_search_path_locker_new(FuPathStore *pstore,
				 const gchar *path,
				 FuKernelSearchPathLockerFlags flags,
				 GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
gchar *
fu_kernel_search_path_get_current(FuPathStore *pstore, GError **error);

G_END_DECLS
