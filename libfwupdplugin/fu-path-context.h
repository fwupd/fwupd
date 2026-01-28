/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-path-struct.h"

#define FU_TYPE_PATH_CONTEXT (fu_path_context_get_type())

G_DECLARE_FINAL_TYPE(FuPathContext, fu_path_context, FU, PATH_CONTEXT, GObject)

const gchar *
fu_path_context_get_dir(FuPathContext *self, FuPathKind kind) G_GNUC_NON_NULL(1);
gchar *
fu_path_context_build_filename(FuPathContext *self,
			       FuPathKind path_kind,
			       ...) G_GNUC_NULL_TERMINATED;

void
fu_path_context_set_dir(FuPathContext *self, FuPathKind kind, const gchar *dirname)
    G_GNUC_NON_NULL(1);
void
fu_path_context_add_prefix(FuPathContext *self, FuPathKind kind, const gchar *prefix)
    G_GNUC_NON_NULL(1, 3);
void
fu_path_context_add_dir(FuPathContext *self, FuPathKind kind, ...) G_GNUC_NULL_TERMINATED;

void
fu_path_context_load_defaults(FuPathContext *self) G_GNUC_NON_NULL(1);
void
fu_path_context_load_from_env(FuPathContext *self) G_GNUC_NON_NULL(1);

FuPathContext *
fu_path_context_new(void) G_GNUC_WARN_UNUSED_RESULT;
