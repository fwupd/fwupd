/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

gboolean
fu_kernel_locked_down(void);
gboolean
fu_kernel_check_version(const gchar *minimum_kernel, GError **error) G_GNUC_NON_NULL(1);
GHashTable *
fu_kernel_get_config(GError **error);
GHashTable *
fu_kernel_parse_config(const gchar *buf, gsize bufsz, GError **error);
GHashTable *
fu_kernel_get_cmdline(GError **error);
GHashTable *
fu_kernel_parse_cmdline(const gchar *buf, gsize bufsz) G_GNUC_NON_NULL(1);
gboolean
fu_kernel_check_cmdline_mutable(GError **error);
gboolean
fu_kernel_add_cmdline_arg(const gchar *arg, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_kernel_remove_cmdline_arg(const gchar *arg, GError **error) G_GNUC_NON_NULL(1);
