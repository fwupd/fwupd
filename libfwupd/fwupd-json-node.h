/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fwupd-json-common.h"

G_BEGIN_DECLS

FwupdJsonNodeKind
fwupd_json_node_get_kind(FwupdJsonNode *self) G_GNUC_NON_NULL(1);
FwupdJsonNode *
fwupd_json_node_ref(FwupdJsonNode *self) G_GNUC_NON_NULL(1);
FwupdJsonNode *
fwupd_json_node_unref(FwupdJsonNode *self) G_GNUC_NON_NULL(1);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FwupdJsonNode, fwupd_json_node_unref)

FwupdJsonNode *
fwupd_json_node_new_raw(const gchar *value) G_GNUC_NON_NULL(1) G_GNUC_WARN_UNUSED_RESULT;
FwupdJsonNode *
fwupd_json_node_new_string(const gchar *value) G_GNUC_NON_NULL(1) G_GNUC_WARN_UNUSED_RESULT;
FwupdJsonNode *
fwupd_json_node_new_object(FwupdJsonObject *json_obj) G_GNUC_NON_NULL(1) G_GNUC_WARN_UNUSED_RESULT;
FwupdJsonNode *
fwupd_json_node_new_array(FwupdJsonArray *json_arr) G_GNUC_NON_NULL(1) G_GNUC_WARN_UNUSED_RESULT;

FwupdJsonObject *
fwupd_json_node_get_object(FwupdJsonNode *self, GError **error) G_GNUC_NON_NULL(1);
FwupdJsonArray *
fwupd_json_node_get_array(FwupdJsonNode *self, GError **error) G_GNUC_NON_NULL(1);
GRefString *
fwupd_json_node_get_raw(FwupdJsonNode *self, GError **error) G_GNUC_NON_NULL(1);
GRefString *
fwupd_json_node_get_string(FwupdJsonNode *self, GError **error) G_GNUC_NON_NULL(1);

GString *
fwupd_json_node_to_string(FwupdJsonNode *self, FwupdJsonExportFlags flags) G_GNUC_NON_NULL(1);

G_END_DECLS
