/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

gboolean
fu_kernel_locked_down(void);
gboolean
fu_kernel_check_version(const gchar *minimum_kernel, GError **error);
gchar *
fu_kernel_get_firmware_search_path(GError **error);
gboolean
fu_kernel_set_firmware_search_path(const gchar *path, GError **error);
gboolean
fu_kernel_reset_firmware_search_path(GError **error);
