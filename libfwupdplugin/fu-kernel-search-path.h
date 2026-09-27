/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-path-store.h"

G_BEGIN_DECLS

#define FU_TYPE_KERNEL_SEARCH_PATH_LOCKER (fu_kernel_search_path_locker_get_type())

G_DECLARE_FINAL_TYPE(FuKernelSearchPathLocker,
		     fu_kernel_search_path_locker,
		     FU,
		     KERNEL_SEARCH_PATH_LOCKER,
		     GObject)

typedef enum {
	FU_KERNEL_SEARCH_PATH_LOCKER_FLAG_NONE = 0,
	FU_KERNEL_SEARCH_PATH_LOCKER_FLAG_EMULATED = 1 << 0,
} FuKernelSearchPathLockerFlags;

const gchar *
fu_kernel_search_path_locker_get_path(FuKernelSearchPathLocker *self)
    G_GNUC_NON_NULL(1) G_GNUC_PURE;

G_END_DECLS
