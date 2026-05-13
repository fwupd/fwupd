/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-cbor-item.h"
#include "fu-coswid-struct.h"

gchar *
fu_coswid_read_string(FuCborItem *item, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_coswid_read_tag(FuCborItem *item, FuCoswidTag *value, GError **error) G_GNUC_NON_NULL(1, 2);
gboolean
fu_coswid_read_version_scheme(FuCborItem *item, FuCoswidVersionScheme *value, GError **error)
    G_GNUC_NON_NULL(1, 2);

void
fu_coswid_write_tag_string(FuCborItem *item, FuCoswidTag tag, const gchar *value)
    G_GNUC_NON_NULL(1);
void
fu_coswid_write_tag_bytestring(FuCborItem *item, FuCoswidTag tag, const guint8 *buf, gsize bufsz)
    G_GNUC_NON_NULL(1, 3);
void
fu_coswid_write_tag_bool(FuCborItem *item, FuCoswidTag tag, gboolean value) G_GNUC_NON_NULL(1);
void
fu_coswid_write_tag_integer(FuCborItem *item, FuCoswidTag tag, gint64 value) G_GNUC_NON_NULL(1);
void
fu_coswid_write_tag_item(FuCborItem *item, FuCoswidTag tag, FuCborItem *value)
    G_GNUC_NON_NULL(1, 3);

typedef gboolean (*FuCoswidItemFunc)(FuCborItem *item,
				     gpointer user_data,
				     GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_coswid_parse_one_or_many(FuCborItem *item,
			    FuCoswidItemFunc func,
			    gpointer user_data,
			    GError **error) G_GNUC_NON_NULL(1, 2);
