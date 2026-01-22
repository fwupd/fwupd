/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fwupd-json-common.h"

G_BEGIN_DECLS

FwupdJsonObject *
fwupd_json_object_new(void) G_GNUC_WARN_UNUSED_RESULT;
FwupdJsonObject *
fwupd_json_object_ref(FwupdJsonObject *self) G_GNUC_NON_NULL(1);
FwupdJsonObject *
fwupd_json_object_unref(FwupdJsonObject *self) G_GNUC_NON_NULL(1);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FwupdJsonObject, fwupd_json_object_unref);
void
fwupd_json_object_clear(FwupdJsonObject *self) G_GNUC_NON_NULL(1);
guint
fwupd_json_object_get_size(FwupdJsonObject *self) G_GNUC_NON_NULL(1) G_GNUC_WARN_UNUSED_RESULT;
GRefString *
fwupd_json_object_get_key_for_index(FwupdJsonObject *self, guint idx, GError **error)
    G_GNUC_NON_NULL(1);
GPtrArray *
fwupd_json_object_get_keys(FwupdJsonObject *self) G_GNUC_NON_NULL(1) G_GNUC_WARN_UNUSED_RESULT;
FwupdJsonNode *
fwupd_json_object_get_node_for_index(FwupdJsonObject *self, guint idx, GError **error)
    G_GNUC_NON_NULL(1);
GPtrArray *
fwupd_json_object_get_nodes(FwupdJsonObject *self) G_GNUC_NON_NULL(1) G_GNUC_WARN_UNUSED_RESULT;
GRefString *
fwupd_json_object_get_string(FwupdJsonObject *self, const gchar *key, GError **error)
    G_GNUC_NON_NULL(1, 2);
const gchar *
fwupd_json_object_get_string_with_default(FwupdJsonObject *self,
					  const gchar *key,
					  const gchar *value_default,
					  GError **error) G_GNUC_NON_NULL(1, 2);
gboolean
fwupd_json_object_get_integer(FwupdJsonObject *self,
			      const gchar *key,
			      gint64 *value,
			      GError **error) G_GNUC_NON_NULL(1, 2);
gboolean
fwupd_json_object_get_integer_with_default(FwupdJsonObject *self,
					   const gchar *key,
					   gint64 *value,
					   gint64 value_default,
					   GError **error) G_GNUC_NON_NULL(1, 2);
gboolean
fwupd_json_object_get_boolean(FwupdJsonObject *self,
			      const gchar *key,
			      gboolean *value,
			      GError **error) G_GNUC_NON_NULL(1, 2);
gboolean
fwupd_json_object_get_boolean_with_default(FwupdJsonObject *self,
					   const gchar *key,
					   gboolean *value,
					   gboolean value_default,
					   GError **error) G_GNUC_NON_NULL(1, 2);
gboolean
fwupd_json_object_has_node(FwupdJsonObject *self, const gchar *key)
    G_GNUC_NON_NULL(1, 2) G_GNUC_WARN_UNUSED_RESULT;
FwupdJsonNode *
fwupd_json_object_get_node(FwupdJsonObject *self, const gchar *key, GError **error)
    G_GNUC_NON_NULL(1, 2) G_GNUC_WARN_UNUSED_RESULT;
FwupdJsonObject *
fwupd_json_object_get_object(FwupdJsonObject *self, const gchar *key, GError **error)
    G_GNUC_NON_NULL(1, 2) G_GNUC_WARN_UNUSED_RESULT;
FwupdJsonArray *
fwupd_json_object_get_array(FwupdJsonObject *self, const gchar *key, GError **error)
    G_GNUC_NON_NULL(1, 2) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_json_object_add_node(FwupdJsonObject *self, const gchar *key, FwupdJsonNode *json_node)
    G_GNUC_NON_NULL(1, 2, 3);
void
fwupd_json_object_add_raw(FwupdJsonObject *self, const gchar *key, const gchar *value)
    G_GNUC_NON_NULL(1, 2, 3);
void
fwupd_json_object_add_string(FwupdJsonObject *self, const gchar *key, const gchar *value)
    G_GNUC_NON_NULL(1, 2);
void
fwupd_json_object_add_integer(FwupdJsonObject *self, const gchar *key, gint64 value)
    G_GNUC_NON_NULL(1, 2);
void
fwupd_json_object_add_boolean(FwupdJsonObject *self, const gchar *key, gboolean value)
    G_GNUC_NON_NULL(1, 2);
void
fwupd_json_object_add_object(FwupdJsonObject *self, const gchar *key, FwupdJsonObject *json_obj)
    G_GNUC_NON_NULL(1, 2, 3);
void
fwupd_json_object_add_array(FwupdJsonObject *self, const gchar *key, FwupdJsonArray *json_arr)
    G_GNUC_NON_NULL(1, 2, 3);

GString *
fwupd_json_object_to_string(FwupdJsonObject *self, FwupdJsonExportFlags flags) G_GNUC_NON_NULL(1);
GBytes *
fwupd_json_object_to_bytes(FwupdJsonObject *self, FwupdJsonExportFlags flags) G_GNUC_NON_NULL(1);

G_END_DECLS
