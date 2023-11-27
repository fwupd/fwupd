/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

#ifdef HAVE_GIO_UNIX
#include <gio/gunixinputstream.h>
#endif

#include <json-glib/json-glib.h>

#include "fwupd-common.h"

G_BEGIN_DECLS

GVariant *
fwupd_hash_kv_to_variant(GHashTable *hash) G_GNUC_NON_NULL(1);
GHashTable *
fwupd_variant_to_hash_kv(GVariant *dict) G_GNUC_NON_NULL(1);
gchar *
fwupd_build_user_agent_system(void);

void
fwupd_input_stream_read_bytes_async(GInputStream *stream,
				    GCancellable *cancellable,
				    GAsyncReadyCallback callback,
				    gpointer callback_data) G_GNUC_NON_NULL(1);
GBytes *
fwupd_input_stream_read_bytes_finish(GInputStream *stream,
				     GAsyncResult *res,
				     GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1, 2);

void
fwupd_common_json_add_string(JsonBuilder *builder, const gchar *key, const gchar *value)
    G_GNUC_NON_NULL(1, 2);
void
fwupd_common_json_add_stringv(JsonBuilder *builder, const gchar *key, gchar **value)
    G_GNUC_NON_NULL(1, 2);
void
fwupd_common_json_add_int(JsonBuilder *builder, const gchar *key, guint64 value)
    G_GNUC_NON_NULL(1, 2);
void
fwupd_common_json_add_boolean(JsonBuilder *builder, const gchar *key, gboolean value)
    G_GNUC_NON_NULL(1, 2);

#ifdef HAVE_GIO_UNIX
GUnixInputStream *
fwupd_unix_input_stream_from_bytes(GBytes *bytes, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
GUnixInputStream *
fwupd_unix_input_stream_from_fn(const gchar *fn, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
#endif

void
fwupd_pad_kv_unx(GString *str, const gchar *key, guint64 value) G_GNUC_NON_NULL(1);
void
fwupd_pad_kv_str(GString *str, const gchar *key, const gchar *value) G_GNUC_NON_NULL(1);
void
fwupd_pad_kv_int(GString *str, const gchar *key, guint32 value) G_GNUC_NON_NULL(1);

G_END_DECLS
