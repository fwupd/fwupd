/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-msgpack-struct.h"

#define FU_TYPE_MSGPACK_ITEM (fu_msgpack_item_get_type())

G_DECLARE_FINAL_TYPE(FuMsgpackItem, fu_msgpack_item, FU, MSGPACK_ITEM, GObject)

FuMsgpackItem *
fu_msgpack_item_new_nil(void);
FuMsgpackItem *
fu_msgpack_item_new_boolean(gboolean value);
FuMsgpackItem *
fu_msgpack_item_new_integer(gint64 value);
FuMsgpackItem *
fu_msgpack_item_new_float(gdouble value);
FuMsgpackItem *
fu_msgpack_item_new_binary(GByteArray *buf) G_GNUC_NON_NULL(1);
FuMsgpackItem *
fu_msgpack_item_new_binary_stream(GInputStream *stream) G_GNUC_NON_NULL(1);
FuMsgpackItem *
fu_msgpack_item_new_string(const gchar *str) G_GNUC_NON_NULL(1);
FuMsgpackItem *
fu_msgpack_item_new_map(guint64 items);
FuMsgpackItem *
fu_msgpack_item_new_array(guint64 items);

FuMsgpackItemKind
fu_msgpack_item_get_kind(FuMsgpackItem *self) G_GNUC_NON_NULL(1);
gboolean
fu_msgpack_item_get_boolean(FuMsgpackItem *self) G_GNUC_NON_NULL(1);
gint64
fu_msgpack_item_get_integer(FuMsgpackItem *self) G_GNUC_NON_NULL(1);
gdouble
fu_msgpack_item_get_float(FuMsgpackItem *self) G_GNUC_NON_NULL(1);
GByteArray *
fu_msgpack_item_get_binary(FuMsgpackItem *self) G_GNUC_NON_NULL(1);
GString *
fu_msgpack_item_get_string(FuMsgpackItem *self) G_GNUC_NON_NULL(1);
guint64
fu_msgpack_item_get_map(FuMsgpackItem *self) G_GNUC_NON_NULL(1);
guint64
fu_msgpack_item_get_array(FuMsgpackItem *self) G_GNUC_NON_NULL(1);
