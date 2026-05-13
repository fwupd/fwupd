/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-cbor-struct.h"

typedef struct FuCborItem FuCborItem;

FuCborItemKind
fu_cbor_item_get_kind(FuCborItem *self) G_GNUC_NON_NULL(1);
FuCborItem *
fu_cbor_item_ref(FuCborItem *self) G_GNUC_NON_NULL(1);
FuCborItem *
fu_cbor_item_unref(FuCborItem *self) G_GNUC_NON_NULL(1);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuCborItem, fu_cbor_item_unref)

FuCborItem *
fu_cbor_item_new_integer(gint64 value) G_GNUC_WARN_UNUSED_RESULT;
FuCborItem *
fu_cbor_item_new_boolean(gboolean value) G_GNUC_WARN_UNUSED_RESULT;
FuCborItem *
fu_cbor_item_new_string(const gchar *value) G_GNUC_WARN_UNUSED_RESULT;
FuCborItem *
fu_cbor_item_new_bytes(GBytes *value) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
FuCborItem *
fu_cbor_item_new_array(void) G_GNUC_WARN_UNUSED_RESULT;
FuCborItem *
fu_cbor_item_new_map(void) G_GNUC_WARN_UNUSED_RESULT;

gboolean
fu_cbor_item_get_integer(FuCborItem *self, gint64 *value, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_cbor_item_get_boolean(FuCborItem *self, gboolean *value, GError **error) G_GNUC_NON_NULL(1);
gchar *
fu_cbor_item_get_string(FuCborItem *self, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
GBytes *
fu_cbor_item_get_bytes(FuCborItem *self, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);

gboolean
fu_cbor_item_array_append(FuCborItem *self, FuCborItem *item, GError **error) G_GNUC_NON_NULL(1, 2);
FuCborItem *
fu_cbor_item_array_index(FuCborItem *self, guint idx) G_GNUC_NON_NULL(1);
guint
fu_cbor_item_array_length(FuCborItem *self) G_GNUC_NON_NULL(1);

gboolean
fu_cbor_item_map_append(FuCborItem *self,
			FuCborItem *item_key,
			FuCborItem *item_val,
			GError **error) G_GNUC_NON_NULL(1, 2, 3);
void
fu_cbor_item_map_index(FuCborItem *self, guint idx, FuCborItem **item_key, FuCborItem **item_value)
    G_GNUC_NON_NULL(1);
guint
fu_cbor_item_map_length(FuCborItem *self) G_GNUC_NON_NULL(1);

GByteArray *
fu_cbor_item_write(FuCborItem *self, GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gchar *
fu_cbor_item_to_string(FuCborItem *self) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
