/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#ifdef HAVE_CBOR
#include <cbor.h>
#include <gio/gio.h>

#include "fu-coswid-struct.h"

G_DEFINE_AUTOPTR_CLEANUP_FUNC(cbor_item_t, cbor_intermediate_decref)

gchar *
fu_coswid_read_string(cbor_item_t *item, GError **error) G_GNUC_NON_NULL(1);
GByteArray *
fu_coswid_read_byte_array(cbor_item_t *item, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_coswid_read_tag(cbor_item_t *item, FuCoswidTag *value, GError **error) G_GNUC_NON_NULL(1, 2);
gboolean
fu_coswid_read_version_scheme(cbor_item_t *item, FuCoswidVersionScheme *value, GError **error)
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_coswid_read_u8(cbor_item_t *item, guint8 *value, GError **error) G_GNUC_NON_NULL(1, 2);
gboolean
fu_coswid_read_s8(cbor_item_t *item, gint8 *value, GError **error) G_GNUC_NON_NULL(1, 2);
gboolean
fu_coswid_read_u64(cbor_item_t *item, guint64 *value, GError **error) G_GNUC_NON_NULL(1, 2);

void
fu_coswid_write_tag_string(cbor_item_t *item, FuCoswidTag tag, const gchar *value)
    G_GNUC_NON_NULL(1);
void
fu_coswid_write_tag_bytestring(cbor_item_t *item, FuCoswidTag tag, const guint8 *buf, gsize bufsz)
    G_GNUC_NON_NULL(1, 3);
void
fu_coswid_write_tag_bool(cbor_item_t *item, FuCoswidTag tag, gboolean value) G_GNUC_NON_NULL(1);
void
fu_coswid_write_tag_u16(cbor_item_t *item, FuCoswidTag tag, guint16 value) G_GNUC_NON_NULL(1);
void
fu_coswid_write_tag_u64(cbor_item_t *item, FuCoswidTag tag, guint64 value) G_GNUC_NON_NULL(1);
void
fu_coswid_write_tag_s8(cbor_item_t *item, FuCoswidTag tag, gint8 value) G_GNUC_NON_NULL(1);
void
fu_coswid_write_tag_item(cbor_item_t *item, FuCoswidTag tag, cbor_item_t *value)
    G_GNUC_NON_NULL(1, 3);

typedef gboolean (*FuCoswidItemFunc)(cbor_item_t *item,
				     gpointer user_data,
				     GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_coswid_parse_one_or_many(cbor_item_t *item,
			    FuCoswidItemFunc func,
			    gpointer user_data,
			    GError **error) G_GNUC_NON_NULL(1, 2);

#endif
