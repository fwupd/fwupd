/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

#define FU_TYPE_KERNEL_SEARCH_PATH_LOCKER (fu_kernel_search_path_locker_get_type())

G_DECLARE_FINAL_TYPE(FuKernelSearchPathLocker,
		     fu_kernel_search_path_locker,
		     FU,
		     KERNEL_SEARCH_PATH_LOCKER,
		     GObject)

FuKernelSearchPathLocker *
fu_kernel_search_path_locker_new(const gchar *path, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
const gchar *
fu_kernel_search_path_locker_get_path(FuKernelSearchPathLocker *self) G_GNUC_NON_NULL(1);
