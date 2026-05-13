/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-path-struct.h"
#include "fu-temporary-directory.h"

#define FU_TYPE_PATH_STORE (fu_path_store_get_type())

G_DECLARE_FINAL_TYPE(FuPathStore, fu_path_store, FU, PATH_STORE, GObject)

const gchar *
fu_path_store_get_path(FuPathStore *self, FuPathKind kind, GError **error) G_GNUC_NON_NULL(1);
gchar *
fu_path_store_build_filename(FuPathStore *self, GError **error, FuPathKind kind, ...)
    G_GNUC_NON_NULL(1) G_GNUC_NULL_TERMINATED;

void
fu_path_store_set_path(FuPathStore *self, FuPathKind kind, const gchar *path) G_GNUC_NON_NULL(1);
void
fu_path_store_set_tmpdir(FuPathStore *self, FuPathKind kind, FuTemporaryDirectory *tmpdir)
    G_GNUC_NON_NULL(1, 3);
void
fu_path_store_add_prefix(FuPathStore *self, FuPathKind kind, const gchar *prefix)
    G_GNUC_NON_NULL(1, 3);

void
fu_path_store_load_defaults(FuPathStore *self) G_GNUC_NON_NULL(1);
void
fu_path_store_load_from_env(FuPathStore *self) G_GNUC_NON_NULL(1);

FuPathStore *
fu_path_store_new(void) G_GNUC_WARN_UNUSED_RESULT;
