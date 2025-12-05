/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fwupd-json-common.h"

G_BEGIN_DECLS

FwupdJsonArray *
fwupd_json_array_new(void) G_GNUC_WARN_UNUSED_RESULT;
FwupdJsonArray *
fwupd_json_array_ref(FwupdJsonArray *self) G_GNUC_NON_NULL(1);
FwupdJsonArray *
fwupd_json_array_unref(FwupdJsonArray *self) G_GNUC_NON_NULL(1);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FwupdJsonArray, fwupd_json_array_unref);

guint
fwupd_json_array_get_size(FwupdJsonArray *self) G_GNUC_NON_NULL(1) G_GNUC_WARN_UNUSED_RESULT;
FwupdJsonNode *
fwupd_json_array_get_node(FwupdJsonArray *self, guint idx, GError **error)
    G_GNUC_NON_NULL(1) G_GNUC_WARN_UNUSED_RESULT;
GRefString *
fwupd_json_array_get_string(FwupdJsonArray *self, guint idx, GError **error)
    G_GNUC_NON_NULL(1) G_GNUC_WARN_UNUSED_RESULT;
GRefString *
fwupd_json_array_get_raw(FwupdJsonArray *self, guint idx, GError **error)
    G_GNUC_NON_NULL(1) G_GNUC_WARN_UNUSED_RESULT;
FwupdJsonObject *
fwupd_json_array_get_object(FwupdJsonArray *self, guint idx, GError **error)
    G_GNUC_NON_NULL(1) G_GNUC_WARN_UNUSED_RESULT;
FwupdJsonArray *
fwupd_json_array_get_array(FwupdJsonArray *self, guint idx, GError **error)
    G_GNUC_NON_NULL(1) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_json_array_add_node(FwupdJsonArray *self, FwupdJsonNode *json_node) G_GNUC_NON_NULL(1, 2);
void
fwupd_json_array_add_string(FwupdJsonArray *self, const gchar *value) G_GNUC_NON_NULL(1, 2);
void
fwupd_json_array_add_raw(FwupdJsonArray *self, const gchar *value) G_GNUC_NON_NULL(1, 2);
void
fwupd_json_array_add_object(FwupdJsonArray *self, FwupdJsonObject *json_obj) G_GNUC_NON_NULL(1, 2);
void
fwupd_json_array_add_array(FwupdJsonArray *self, FwupdJsonArray *json_arr) G_GNUC_NON_NULL(1, 2);
GString *
fwupd_json_array_to_string(FwupdJsonArray *self, FwupdJsonExportFlags flags)
    G_GNUC_NON_NULL(1) G_GNUC_WARN_UNUSED_RESULT;

G_END_DECLS
