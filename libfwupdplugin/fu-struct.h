/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#include "fwupd-common.h"

#define FU_TYPE_STRUCT (fu_struct_get_type())

G_DECLARE_FINAL_TYPE(FuStruct, fu_struct, FU, STRUCT, GObject)

typedef enum {
	FU_STRUCT_FLAG_NONE,
	FU_STRUCT_FLAG_ONLY_CONSTANTS,
} FuStructFlags;

FuStruct *
fu_struct_new(const gchar *fmt, GError **error);
gchar *
fu_struct_to_string(FuStruct *self);
gsize
fu_struct_size(FuStruct *self);
const gchar *
fu_struct_get_name(FuStruct *self);
GByteArray *
fu_struct_pack(FuStruct *self);
void
fu_struct_pack_into(FuStruct *self, GByteArray *buf);
GBytes *
fu_struct_pack_bytes(FuStruct *self);
gboolean
fu_struct_pack_full(FuStruct *self,
		    guint8 *buf,
		    gsize bufsz,
		    gsize offset,
		    FuStructFlags flags,
		    GError **error);
gboolean
fu_struct_unpack(FuStruct *self, GByteArray *buf, GError **error);
gboolean
fu_struct_unpack_full(FuStruct *self,
		      const guint8 *buf,
		      gsize bufsz,
		      gsize offset,
		      FuStructFlags flags,
		      GError **error);

gsize
fu_struct_get_id_offset(FuStruct *self, const gchar *id);
gsize
fu_struct_get_id_size(FuStruct *self, const gchar *id);

guint8
fu_struct_get_u8(FuStruct *self, const gchar *id);
guint16
fu_struct_get_u16(FuStruct *self, const gchar *id);
guint32
fu_struct_get_u24(FuStruct *self, const gchar *id);
guint32
fu_struct_get_u32(FuStruct *self, const gchar *id);
guint64
fu_struct_get_u64(FuStruct *self, const gchar *id);
const guint8 *
fu_struct_get_u8ptr(FuStruct *self, const gchar *id, gsize *bufsz);
const fwupd_guid_t *
fu_struct_get_guid(FuStruct *self, const gchar *id);
gchar *
fu_struct_get_string(FuStruct *self, const gchar *id);

void
fu_struct_set_u8(FuStruct *self, const gchar *id, guint8 val);
void
fu_struct_set_u16(FuStruct *self, const gchar *id, guint16 val);
void
fu_struct_set_u24(FuStruct *self, const gchar *id, guint32 val);
void
fu_struct_set_u32(FuStruct *self, const gchar *id, guint32 val);
void
fu_struct_set_u64(FuStruct *self, const gchar *id, guint64 val);
void
fu_struct_set_u8ptr(FuStruct *self, const gchar *id, const guint8 *buf, gsize bufsz);
void
fu_struct_set_guid(FuStruct *self, const gchar *id, const fwupd_guid_t *guid);
gboolean
fu_struct_set_string(FuStruct *self, const gchar *id, const gchar *val, GError **error);
void
fu_struct_set_string_literal(FuStruct *self, const gchar *id, const gchar *val);

void
fu_struct_register(gpointer obj, const gchar *fmt);
FuStruct *
fu_struct_lookup(gpointer obj, const gchar *name);
