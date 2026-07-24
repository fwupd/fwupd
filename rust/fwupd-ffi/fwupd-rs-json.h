/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct FwupdRsJsonParser FwupdRsJsonParser;
typedef struct FwupdRsJsonNode FwupdRsJsonNode;
typedef struct FwupdRsJsonObject FwupdRsJsonObject;
typedef struct FwupdRsJsonArray FwupdRsJsonArray;

/* -- Parser -- */
FwupdRsJsonParser *
fwupd_rs_json_parser_new(void);
void
fwupd_rs_json_parser_free(FwupdRsJsonParser *ptr);
void
fwupd_rs_json_parser_set_max_depth(FwupdRsJsonParser *ptr, guint max_depth);
void
fwupd_rs_json_parser_set_max_items(FwupdRsJsonParser *ptr, guint max_items);
void
fwupd_rs_json_parser_set_max_quoted(FwupdRsJsonParser *ptr, guint max_quoted);
FwupdRsJsonNode *
fwupd_rs_json_parser_load_from_data(const FwupdRsJsonParser *ptr,
				    const gchar *text,
				    guint flags,
				    GError **error);
FwupdRsJsonNode *
fwupd_rs_json_parser_load_from_bytes(const FwupdRsJsonParser *ptr,
				     const guint8 *data,
				     gsize data_len,
				     guint flags,
				     GError **error);

/* -- Node -- */
FwupdRsJsonNode *
fwupd_rs_json_node_new_raw(const gchar *value);
FwupdRsJsonNode *
fwupd_rs_json_node_new_string(const gchar *value);
FwupdRsJsonNode *
fwupd_rs_json_node_new_object(FwupdRsJsonObject *obj);
FwupdRsJsonNode *
fwupd_rs_json_node_new_array(FwupdRsJsonArray *arr);
void
fwupd_rs_json_node_free(FwupdRsJsonNode *ptr);
guint
fwupd_rs_json_node_get_kind(const FwupdRsJsonNode *ptr);
const gchar *
fwupd_rs_json_node_get_raw(FwupdRsJsonNode *ptr, GError **error);
const gchar *
fwupd_rs_json_node_get_string(FwupdRsJsonNode *ptr, GError **error);
FwupdRsJsonObject *
fwupd_rs_json_node_get_object(const FwupdRsJsonNode *ptr, GError **error);
FwupdRsJsonArray *
fwupd_rs_json_node_get_array(const FwupdRsJsonNode *ptr, GError **error);
GString *
fwupd_rs_json_node_to_string(const FwupdRsJsonNode *ptr, guint flags);

/* -- Object -- */
FwupdRsJsonObject *
fwupd_rs_json_object_new(void);
void
fwupd_rs_json_object_free(FwupdRsJsonObject *ptr);
guint
fwupd_rs_json_object_get_size(const FwupdRsJsonObject *ptr);
void
fwupd_rs_json_object_clear(FwupdRsJsonObject *ptr);
const gchar *
fwupd_rs_json_object_get_key_for_index(FwupdRsJsonObject *ptr, guint idx, GError **error);
FwupdRsJsonNode *
fwupd_rs_json_object_get_node_for_index(const FwupdRsJsonObject *ptr, guint idx, GError **error);
gint
fwupd_rs_json_object_has_node(const FwupdRsJsonObject *ptr, const gchar *key);
const gchar *
fwupd_rs_json_object_get_string(FwupdRsJsonObject *ptr, const gchar *key, GError **error);
gint
fwupd_rs_json_object_get_integer(const FwupdRsJsonObject *ptr,
				 const gchar *key,
				 gint64 *value,
				 GError **error);
gint
fwupd_rs_json_object_get_boolean(const FwupdRsJsonObject *ptr,
				 const gchar *key,
				 gint *value,
				 GError **error);
FwupdRsJsonNode *
fwupd_rs_json_object_get_node(const FwupdRsJsonObject *ptr, const gchar *key, GError **error);
FwupdRsJsonObject *
fwupd_rs_json_object_get_object(const FwupdRsJsonObject *ptr, const gchar *key, GError **error);
FwupdRsJsonArray *
fwupd_rs_json_object_get_array(const FwupdRsJsonObject *ptr, const gchar *key, GError **error);
void
fwupd_rs_json_object_add_string(FwupdRsJsonObject *ptr, const gchar *key, const gchar *value);
void
fwupd_rs_json_object_add_raw(FwupdRsJsonObject *ptr, const gchar *key, const gchar *value);
void
fwupd_rs_json_object_add_integer(FwupdRsJsonObject *ptr, const gchar *key, gint64 value);
void
fwupd_rs_json_object_add_boolean(FwupdRsJsonObject *ptr, const gchar *key, gint value);
void
fwupd_rs_json_object_add_object(FwupdRsJsonObject *ptr, const gchar *key, FwupdRsJsonObject *obj);
void
fwupd_rs_json_object_add_array(FwupdRsJsonObject *ptr, const gchar *key, FwupdRsJsonArray *arr);
void
fwupd_rs_json_object_add_node(FwupdRsJsonObject *ptr,
			      const gchar *key,
			      const FwupdRsJsonNode *node);
GString *
fwupd_rs_json_object_to_string(const FwupdRsJsonObject *ptr, guint flags);

/* -- Array -- */
FwupdRsJsonArray *
fwupd_rs_json_array_new(void);
void
fwupd_rs_json_array_free(FwupdRsJsonArray *ptr);
guint
fwupd_rs_json_array_get_size(const FwupdRsJsonArray *ptr);
FwupdRsJsonNode *
fwupd_rs_json_array_get_node(const FwupdRsJsonArray *ptr, guint idx, GError **error);
const gchar *
fwupd_rs_json_array_get_string(FwupdRsJsonArray *ptr, guint idx, GError **error);
const gchar *
fwupd_rs_json_array_get_raw(FwupdRsJsonArray *ptr, guint idx, GError **error);
FwupdRsJsonObject *
fwupd_rs_json_array_get_object(const FwupdRsJsonArray *ptr, guint idx, GError **error);
FwupdRsJsonArray *
fwupd_rs_json_array_get_array(const FwupdRsJsonArray *ptr, guint idx, GError **error);
void
fwupd_rs_json_array_add_string(FwupdRsJsonArray *ptr, const gchar *value);
void
fwupd_rs_json_array_add_raw(FwupdRsJsonArray *ptr, const gchar *value);
void
fwupd_rs_json_array_add_object(FwupdRsJsonArray *ptr, const FwupdRsJsonObject *obj);
void
fwupd_rs_json_array_add_array(FwupdRsJsonArray *ptr, const FwupdRsJsonArray *arr);
void
fwupd_rs_json_array_add_node(FwupdRsJsonArray *ptr, const FwupdRsJsonNode *node);
GString *
fwupd_rs_json_array_to_string(const FwupdRsJsonArray *ptr, guint flags);

G_END_DECLS
