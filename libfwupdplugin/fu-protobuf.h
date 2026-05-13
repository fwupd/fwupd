/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

#define FU_TYPE_PROTOBUF (fu_protobuf_get_type())

G_DECLARE_FINAL_TYPE(FuProtobuf, fu_protobuf, FU, PROTOBUF, GObject)

FuProtobuf *
fu_protobuf_new(void);
FuProtobuf *
fu_protobuf_new_from_data(const guint8 *buf, gsize bufsz) G_GNUC_NON_NULL(1);

gchar *
fu_protobuf_to_string(FuProtobuf *self) G_GNUC_NON_NULL(1);
GByteArray *
fu_protobuf_write(FuProtobuf *self) G_GNUC_NON_NULL(1);

void
fu_protobuf_add_empty(FuProtobuf *self, guint8 fnum) G_GNUC_NON_NULL(1);
void
fu_protobuf_add_boolean(FuProtobuf *self, guint8 fnum, gboolean value) G_GNUC_NON_NULL(1);
void
fu_protobuf_add_uint64(FuProtobuf *self, guint8 fnum, guint64 value) G_GNUC_NON_NULL(1);
void
fu_protobuf_add_string(FuProtobuf *self, guint8 fnum, const gchar *value) G_GNUC_NON_NULL(1, 3);
void
fu_protobuf_add_embedded(FuProtobuf *self, guint8 fnum, FuProtobuf *pbuf) G_GNUC_NON_NULL(1, 3);

gboolean
fu_protobuf_get_boolean(FuProtobuf *self, guint8 fnum, gboolean *value, GError **error)
    G_GNUC_NON_NULL(1, 3);
gboolean
fu_protobuf_get_uint64(FuProtobuf *self, guint8 fnum, guint64 *value, GError **error)
    G_GNUC_NON_NULL(1, 3);
gchar *
fu_protobuf_get_string(FuProtobuf *self, guint8 fnum, GError **error) G_GNUC_NON_NULL(1);
FuProtobuf *
fu_protobuf_get_embedded(FuProtobuf *self, guint8 fnum, GError **error) G_GNUC_NON_NULL(1);
