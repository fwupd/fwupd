/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-kernel-search-path.h"

G_BEGIN_DECLS

gchar *
fu_kernel_search_path_get_current(FuPathStore *pstore, GError **error);

G_END_DECLS
