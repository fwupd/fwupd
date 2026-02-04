/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

#include "fu-path-struct.h"

GPtrArray *
fu_path_glob(const gchar *directory, const gchar *pattern, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_path_rmtree(const gchar *directory, GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
GPtrArray *
fu_path_get_files(const gchar *path, GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_path_mkdir(const gchar *dirname, GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_path_mkdir_parent(const gchar *filename, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
gchar *
fu_path_find_program(const gchar *basename, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
gchar *
fu_path_make_absolute(const gchar *filename, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
gchar *
fu_path_get_symlink_target(const gchar *filename, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
