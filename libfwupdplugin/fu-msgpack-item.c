/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuMsgpack"

#include "config.h"

#include <fwupd.h>

#include "fu-byte-array.h"
#include "fu-input-stream.h"
#include "fu-mem-private.h"
#include "fu-msgpack-item-private.h"

struct _FuMsgpackItem {
	GObject parent_instance;
	FuMsgpackItemKind kind;
	GInputStream *stream;
	union {
		gint64 i64;
		gdouble f64;
		GByteArray *buf;
		GString *str;
	} value;
};

G_DEFINE_TYPE(FuMsgpackItem, fu_msgpack_item, G_TYPE_OBJECT)

/**
 * fu_msgpack_item_get_kind:
 * @self: a #FuMsgpackItem
 *
 * Gets the item kind.
 *
 * Returns: a #FuMsgpackItemKind, e.g. %FU_MSGPACK_ITEM_KIND_BOOLEAN
 *
 * Since: 2.0.0
 **/
FuMsgpackItemKind
fu_msgpack_item_get_kind(FuMsgpackItem *self)
{
	g_return_val_if_fail(FU_IS_MSGPACK_ITEM(self), FU_MSGPACK_ITEM_KIND_UNKNOWN);
	return self->kind;
}

/**
 * fu_msgpack_item_get_boolean:
 * @self: a #FuMsgpackItem
 *
 * Reads a value from the item.
 *
 * Returns: an integer, or %G_MAXINT64 if invalid or not found
 *
 * Since: 2.0.0
 **/
gboolean
fu_msgpack_item_get_boolean(FuMsgpackItem *self)
{
	g_return_val_if_fail(FU_IS_MSGPACK_ITEM(self), FALSE);
	g_return_val_if_fail(self->kind == FU_MSGPACK_ITEM_KIND_BOOLEAN, FALSE);
	return self->value.i64 > 0;
}

/**
 * fu_msgpack_item_get_integer:
 * @self: a #FuMsgpackItem
 *
 * Reads a value from the item.
 *
 * Returns: an integer, or %G_MAXINT64 if invalid or not found
 *
 * Since: 2.0.0
 **/
gint64
fu_msgpack_item_get_integer(FuMsgpackItem *self)
{
	g_return_val_if_fail(FU_IS_MSGPACK_ITEM(self), G_MAXINT64);
	g_return_val_if_fail(self->kind == FU_MSGPACK_ITEM_KIND_INTEGER, G_MAXINT64);
	return self->value.i64;
}

/**
 * fu_msgpack_item_get_float:
 * @self: a #FuMsgpackItem
 *
 * Reads a value from the item.
 *
 * Returns: an integer, or %G_MINDOUBLE if invalid or not found
 *
 * Since: 2.0.0
 **/
gdouble
fu_msgpack_item_get_float(FuMsgpackItem *self)
{
	g_return_val_if_fail(FU_IS_MSGPACK_ITEM(self), G_MINDOUBLE);
	g_return_val_if_fail(self->kind == FU_MSGPACK_ITEM_KIND_FLOAT, G_MINDOUBLE);
	return self->value.f64;
}

/**
 * fu_msgpack_item_get_binary:
 * @self: a #FuMsgpackItem
 *
 * Reads a value from the item.
 *
 * Returns: (transfer none): a #GByteArray
 *
 * Since: 2.0.0
 **/
GByteArray *
fu_msgpack_item_get_binary(FuMsgpackItem *self)
{
	g_return_val_if_fail(FU_IS_MSGPACK_ITEM(self), NULL);
	g_return_val_if_fail(self->kind == FU_MSGPACK_ITEM_KIND_BINARY, NULL);
	g_return_val_if_fail(self->stream == NULL, NULL);
	return self->value.buf;
}

/**
 * fu_msgpack_item_get_map:
 * @self: a #FuMsgpackItem
 *
 * Reads the number of items in the map.
 *
 * Returns: an integer, or %G_MAXINT64 if invalid or not found
 *
 * Since: 2.0.0
 **/
guint64
fu_msgpack_item_get_map(FuMsgpackItem *self)
{
	g_return_val_if_fail(FU_IS_MSGPACK_ITEM(self), G_MAXINT64);
	g_return_val_if_fail(self->kind == FU_MSGPACK_ITEM_KIND_MAP, G_MAXINT64);
	return self->value.i64;
}

/**
 * fu_msgpack_item_get_array:
 * @self: a #FuMsgpackItem
 *
 * Reads the number of items in the array.
 *
 * Returns: an integer, or %G_MAXINT64 if invalid or not found
 *
 * Since: 2.0.0
 **/
guint64
fu_msgpack_item_get_array(FuMsgpackItem *self)
{
	g_return_val_if_fail(FU_IS_MSGPACK_ITEM(self), G_MAXINT64);
	g_return_val_if_fail(self->kind == FU_MSGPACK_ITEM_KIND_ARRAY, G_MAXINT64);
	return self->value.i64;
}

/**
 * fu_msgpack_item_get_string:
 * @self: a #FuMsgpackItem
 *
 * Reads a value from the item.
 *
 * Returns: (transfer none): a #GString
 *
 * Since: 2.0.0
 **/
GString *
fu_msgpack_item_get_string(FuMsgpackItem *self)
{
	g_return_val_if_fail(FU_IS_MSGPACK_ITEM(self), NULL);
	g_return_val_if_fail(self->kind == FU_MSGPACK_ITEM_KIND_STRING, NULL);
	return self->value.str;
}

/**
 * fu_msgpack_item_new_nil:
 *
 * Creates a new msgpack item.
 *
 * Returns: (transfer full): a #FuMsgpackItem
 *
 * Since: 2.0.0
 **/
FuMsgpackItem *
fu_msgpack_item_new_nil(void)
{
	g_autoptr(FuMsgpackItem) self = g_object_new(FU_TYPE_MSGPACK_ITEM, NULL);
	self->kind = FU_MSGPACK_ITEM_KIND_NIL;
	return g_steal_pointer(&self);
}

/**
 * fu_msgpack_item_new_boolean:
 * @value: integer value
 *
 * Creates a new msgpack item.
 *
 * Returns: (transfer full): a #FuMsgpackItem
 *
 * Since: 2.0.0
 **/
FuMsgpackItem *
fu_msgpack_item_new_boolean(gboolean value)
{
	g_autoptr(FuMsgpackItem) self = g_object_new(FU_TYPE_MSGPACK_ITEM, NULL);
	self->kind = FU_MSGPACK_ITEM_KIND_BOOLEAN;
	self->value.i64 = value ? 1 : 0;
	return g_steal_pointer(&self);
}

/**
 * fu_msgpack_item_new_integer:
 * @value: integer value
 *
 * Creates a new msgpack item.
 *
 * Returns: (transfer full): a #FuMsgpackItem
 *
 * Since: 2.0.0
 **/
FuMsgpackItem *
fu_msgpack_item_new_integer(gint64 value)
{
	g_autoptr(FuMsgpackItem) self = g_object_new(FU_TYPE_MSGPACK_ITEM, NULL);
	g_return_val_if_fail(value < G_MAXINT64, NULL);
	self->kind = FU_MSGPACK_ITEM_KIND_INTEGER;
	self->value.i64 = value;
	return g_steal_pointer(&self);
}

/**
 * fu_msgpack_item_new_float:
 * @value: floating point value
 *
 * Creates a new msgpack item.
 *
 * Returns: (transfer full): a #FuMsgpackItem
 *
 * Since: 2.0.0
 **/
FuMsgpackItem *
fu_msgpack_item_new_float(gdouble value)
{
	g_autoptr(FuMsgpackItem) self = g_object_new(FU_TYPE_MSGPACK_ITEM, NULL);
	g_return_val_if_fail(value < G_MAXDOUBLE, NULL);
	self->kind = FU_MSGPACK_ITEM_KIND_FLOAT;
	self->value.f64 = value;
	return g_steal_pointer(&self);
}

/**
 * fu_msgpack_item_new_binary:
 * @buf: (not nullable): a #GByteArray
 *
 * Creates a new msgpack item.
 *
 * Returns: (transfer full): a #FuMsgpackItem
 *
 * Since: 2.0.0
 **/
FuMsgpackItem *
fu_msgpack_item_new_binary(GByteArray *buf)
{
	g_autoptr(FuMsgpackItem) self = g_object_new(FU_TYPE_MSGPACK_ITEM, NULL);
	g_return_val_if_fail(buf != NULL, NULL);
	self->kind = FU_MSGPACK_ITEM_KIND_BINARY;
	self->value.buf = g_byte_array_ref(buf);
	return g_steal_pointer(&self);
}

/**
 * fu_msgpack_item_new_binary_stream:
 * @stream: (not nullable): a #GInputStream
 *
 * Creates a new msgpack item.
 *
 * Returns: (transfer full): a #FuMsgpackItem
 *
 * Since: 2.0.0
 **/
FuMsgpackItem *
fu_msgpack_item_new_binary_stream(GInputStream *stream)
{
	g_autoptr(FuMsgpackItem) self = g_object_new(FU_TYPE_MSGPACK_ITEM, NULL);
	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), NULL);
	self->kind = FU_MSGPACK_ITEM_KIND_BINARY;
	self->stream = g_object_ref(stream);
	return g_steal_pointer(&self);
}

/**
 * fu_msgpack_item_new_string:
 * @str: (not nullable): string value
 *
 * Creates a new msgpack item.
 *
 * Returns: (transfer full): a #FuMsgpackItem
 *
 * Since: 2.0.0
 **/
FuMsgpackItem *
fu_msgpack_item_new_string(const gchar *str)
{
	g_autoptr(FuMsgpackItem) self = g_object_new(FU_TYPE_MSGPACK_ITEM, NULL);
	g_return_val_if_fail(str != NULL, NULL);
	self->kind = FU_MSGPACK_ITEM_KIND_STRING;
	self->value.str = g_string_new(str);
	return g_steal_pointer(&self);
}

/**
 * fu_msgpack_item_new_map:
 * @items: number of items
 *
 * Creates a new msgpack item.
 *
 * Returns: (transfer full): a #FuMsgpackItem
 *
 * Since: 2.0.0
 **/
FuMsgpackItem *
fu_msgpack_item_new_map(guint64 items)
{
	g_autoptr(FuMsgpackItem) self = g_object_new(FU_TYPE_MSGPACK_ITEM, NULL);
	self->kind = FU_MSGPACK_ITEM_KIND_MAP;
	self->value.i64 = items;
	return g_steal_pointer(&self);
}

/**
 * fu_msgpack_item_new_array:
 * @items: number of items
 *
 * Creates a new msgpack item.
 *
 * Returns: (transfer full): a #FuMsgpackItem
 *
 * Since: 2.0.0
 **/
FuMsgpackItem *
fu_msgpack_item_new_array(guint64 items)
{
	g_autoptr(FuMsgpackItem) self = g_object_new(FU_TYPE_MSGPACK_ITEM, NULL);
	self->kind = FU_MSGPACK_ITEM_KIND_ARRAY;
	self->value.i64 = items;
	return g_steal_pointer(&self);
}

static gboolean
fu_msgpack_item_append_integer(GByteArray *buf, gint64 val, GError **error)
{
	if (val >= 0) {
		if (val <= FU_MSGPACK_CMD_POSITIVE_FIXINT_END) {
			fu_byte_array_append_uint8(buf, val);
			return TRUE;
		}
		if (val <= G_MAXUINT8) {
			fu_byte_array_append_uint8(buf, FU_MSGPACK_CMD_UINT8);
			fu_byte_array_append_uint8(buf, val);
			return TRUE;
		}
		if (val <= G_MAXUINT16) {
			fu_byte_array_append_uint8(buf, FU_MSGPACK_CMD_UINT16);
			fu_byte_array_append_uint16(buf, val, G_BIG_ENDIAN);
			return TRUE;
		}
		if (val <= G_MAXUINT32) {
			fu_byte_array_append_uint8(buf, FU_MSGPACK_CMD_UINT32);
			fu_byte_array_append_uint32(buf, val, G_BIG_ENDIAN);
			return TRUE;
		}
		fu_byte_array_append_uint8(buf, FU_MSGPACK_CMD_UINT64);
		fu_byte_array_append_uint64(buf, val, G_BIG_ENDIAN);
		return TRUE;
	}
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "negint not supported");
	return FALSE;
}

static gboolean
fu_msgpack_item_append_double(GByteArray *buf, gdouble val, GError **error)
{
	guint64 int_val = 0;
	fu_byte_array_append_uint8(buf, FU_MSGPACK_CMD_FLOAT64);
	if (!fu_memcpy_safe((guint8 *)&int_val,
			    sizeof(int_val),
			    0,
			    (guint8 *)&val,
			    sizeof(val),
			    0,
			    sizeof(val),
			    error))
		return FALSE;
	fu_byte_array_append_uint64(buf, int_val, G_BIG_ENDIAN);
	return TRUE;
}

static gboolean
fu_msgpack_item_append_array(GByteArray *buf, gint64 val, GError **error)
{
	if (val <= 15) {
		fu_byte_array_append_uint8(buf, FU_MSGPACK_CMD_FIXARRAY | val);
		return TRUE;
	}
	if (val <= G_MAXUINT16) {
		fu_byte_array_append_uint8(buf, FU_MSGPACK_CMD_ARRAY16);
		fu_byte_array_append_uint16(buf, val, G_BIG_ENDIAN);
		return TRUE;
	}
	if (val <= G_MAXUINT32) {
		fu_byte_array_append_uint8(buf, FU_MSGPACK_CMD_ARRAY32);
		fu_byte_array_append_uint32(buf, val, G_BIG_ENDIAN);
		return TRUE;
	}
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "array too large");
	return FALSE;
}

static gboolean
fu_msgpack_item_append_map(GByteArray *buf, gint64 val, GError **error)
{
	if (val <= 15) {
		fu_byte_array_append_uint8(buf, FU_MSGPACK_CMD_FIXMAP | val);
		return TRUE;
	}
	if (val <= G_MAXUINT16) {
		fu_byte_array_append_uint8(buf, FU_MSGPACK_CMD_MAP16);
		fu_byte_array_append_uint16(buf, val, G_BIG_ENDIAN);
		return TRUE;
	}
	if (val <= G_MAXUINT32) {
		fu_byte_array_append_uint8(buf, FU_MSGPACK_CMD_MAP32);
		fu_byte_array_append_uint32(buf, val, G_BIG_ENDIAN);
		return TRUE;
	}
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "map too large");
	return FALSE;
}

static gboolean
fu_msgpack_item_append_string(GByteArray *buf, GString *str, GError **error)
{
	if (str->len <= 31) {
		fu_byte_array_append_uint8(buf, FU_MSGPACK_CMD_FIXSTR | str->len);
		g_byte_array_append(buf, (const guint8 *)str->str, str->len);
		return TRUE;
	}
	if (str->len <= G_MAXUINT8) {
		fu_byte_array_append_uint8(buf, FU_MSGPACK_CMD_STR8);
		fu_byte_array_append_uint8(buf, str->len);
		g_byte_array_append(buf, (const guint8 *)str->str, str->len);
		return TRUE;
	}
	if (str->len <= G_MAXUINT16) {
		fu_byte_array_append_uint8(buf, FU_MSGPACK_CMD_STR16);
		fu_byte_array_append_uint16(buf, str->len, G_BIG_ENDIAN);
		g_byte_array_append(buf, (const guint8 *)str->str, str->len);
		return TRUE;
	}
	if (str->len <= G_MAXUINT32) {
		fu_byte_array_append_uint8(buf, FU_MSGPACK_CMD_STR32);
		fu_byte_array_append_uint32(buf, str->len, G_BIG_ENDIAN);
		g_byte_array_append(buf, (const guint8 *)str->str, str->len);
		return TRUE;
	}
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "string too long");
	return FALSE;
}

static gboolean
fu_msgpack_item_append_binary_stream_chunk_cb(const guint8 *buf,
					      gsize bufsz,
					      gpointer user_data,
					      GError **error)
{
	GByteArray *data = (GByteArray *)user_data;
	g_byte_array_append(data, buf, bufsz);
	return TRUE;
}

static gboolean
fu_msgpack_item_append_binary_stream(GByteArray *buf, GInputStream *stream, GError **error)
{
	gsize streamsz = 0;
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (streamsz <= G_MAXUINT8) {
		fu_byte_array_append_uint8(buf, FU_MSGPACK_CMD_BIN8);
		fu_byte_array_append_uint8(buf, streamsz);
		return fu_input_stream_chunkify(stream,
						fu_msgpack_item_append_binary_stream_chunk_cb,
						buf,
						error);
	}
	if (streamsz <= G_MAXUINT16) {
		fu_byte_array_append_uint8(buf, FU_MSGPACK_CMD_BIN16);
		fu_byte_array_append_uint16(buf, streamsz, G_BIG_ENDIAN);
		return fu_input_stream_chunkify(stream,
						fu_msgpack_item_append_binary_stream_chunk_cb,
						buf,
						error);
	}
	if (streamsz <= G_MAXUINT32) {
		fu_byte_array_append_uint8(buf, FU_MSGPACK_CMD_BIN32);
		fu_byte_array_append_uint32(buf, streamsz, G_BIG_ENDIAN);
		return fu_input_stream_chunkify(stream,
						fu_msgpack_item_append_binary_stream_chunk_cb,
						buf,
						error);
	}
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "binary too large");
	return FALSE;
}

static gboolean
fu_msgpack_item_append_binary(GByteArray *buf, GByteArray *donor, GError **error)
{
	if (donor->len <= G_MAXUINT8) {
		fu_byte_array_append_uint8(buf, FU_MSGPACK_CMD_BIN8);
		fu_byte_array_append_uint8(buf, donor->len);
		g_byte_array_append(buf, donor->data, donor->len);
		return TRUE;
	}
	if (donor->len <= G_MAXUINT16) {
		fu_byte_array_append_uint8(buf, FU_MSGPACK_CMD_BIN16);
		fu_byte_array_append_uint16(buf, donor->len, G_BIG_ENDIAN);
		g_byte_array_append(buf, donor->data, donor->len);
		return TRUE;
	}
	if (donor->len <= G_MAXUINT32) {
		fu_byte_array_append_uint8(buf, FU_MSGPACK_CMD_BIN32);
		fu_byte_array_append_uint32(buf, donor->len, G_BIG_ENDIAN);
		g_byte_array_append(buf, donor->data, donor->len);
		return TRUE;
	}
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "binary too large");
	return FALSE;
}

/* private */
gboolean
fu_msgpack_item_append(FuMsgpackItem *self, GByteArray *buf, GError **error)
{
	g_return_val_if_fail(FU_IS_MSGPACK_ITEM(self), FALSE);

	if (self->kind == FU_MSGPACK_ITEM_KIND_NIL) {
		fu_byte_array_append_uint8(buf, FU_MSGPACK_CMD_NIL);
		return TRUE;
	}
	if (self->kind == FU_MSGPACK_ITEM_KIND_BOOLEAN) {
		fu_byte_array_append_uint8(buf,
					   self->value.i64 ? FU_MSGPACK_CMD_TRUE
							   : FU_MSGPACK_CMD_FALSE);
		return TRUE;
	}
	if (self->kind == FU_MSGPACK_ITEM_KIND_FLOAT)
		return fu_msgpack_item_append_double(buf, self->value.f64, error);
	if (self->kind == FU_MSGPACK_ITEM_KIND_INTEGER)
		return fu_msgpack_item_append_integer(buf, self->value.i64, error);
	if (self->kind == FU_MSGPACK_ITEM_KIND_STRING)
		return fu_msgpack_item_append_string(buf, self->value.str, error);
	if (self->kind == FU_MSGPACK_ITEM_KIND_BINARY) {
		if (self->stream != NULL)
			return fu_msgpack_item_append_binary_stream(buf, self->stream, error);
		return fu_msgpack_item_append_binary(buf, self->value.buf, error);
	}
	if (self->kind == FU_MSGPACK_ITEM_KIND_ARRAY)
		return fu_msgpack_item_append_array(buf, self->value.i64, error);
	if (self->kind == FU_MSGPACK_ITEM_KIND_MAP)
		return fu_msgpack_item_append_map(buf, self->value.i64, error);

	/* failure */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "msgpack item %s not supported",
		    fu_msgpack_item_kind_to_string(self->kind));
	return FALSE;
}

static GByteArray *
fu_msgpack_item_read_binary(GByteArray *buf, gsize offset, gsize n, GError **error)
{
	g_autoptr(GByteArray) tmp = g_byte_array_new();
	if (!fu_memchk_read(buf->len, offset, n, error))
		return NULL;
	g_byte_array_append(tmp, buf->data + offset, n);
	return g_steal_pointer(&tmp);
}

static gchar *
fu_msgpack_item_read_string(GByteArray *buf, gsize offset, gsize n, GError **error)
{
	g_autofree gchar *tmp = NULL;

	if (!fu_memchk_read(buf->len, offset, n, error))
		return NULL;
	tmp = g_strndup((const gchar *)buf->data + offset, n);
	if (!g_utf8_validate_len(tmp, n, NULL)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid UTF-8 string");
		return NULL;
	}
	return g_steal_pointer(&tmp);
}

FuMsgpackItem *
fu_msgpack_item_parse(GByteArray *buf, gsize *offset, GError **error)
{
	guint8 cmd = 0;
	g_autofree gchar *tmp_string = NULL;
	g_autoptr(GByteArray) tmp_binary = NULL;

	g_return_val_if_fail(buf != NULL, NULL);
	g_return_val_if_fail(offset != NULL, NULL);

	/* first byte, normally a command */
	if (!fu_memread_uint8_safe(buf->data, buf->len, *offset, &cmd, error))
		return NULL;
	*offset += 1;

	/* nil */
	if (cmd == FU_MSGPACK_CMD_NIL)
		return fu_msgpack_item_new_nil();

	/* boolean */
	if (cmd == FU_MSGPACK_CMD_FALSE)
		return fu_msgpack_item_new_boolean(FALSE);
	if (cmd == FU_MSGPACK_CMD_TRUE)
		return fu_msgpack_item_new_boolean(TRUE);

	/* integer */
	if (cmd <= FU_MSGPACK_CMD_POSITIVE_FIXINT_END)
		return fu_msgpack_item_new_integer(cmd);
	if (cmd == FU_MSGPACK_CMD_UINT8) {
		guint8 v = 0;
		if (!fu_memread_uint8_safe(buf->data, buf->len, *offset, &v, error))
			return NULL;
		*offset += sizeof(v);
		return fu_msgpack_item_new_integer(v);
	}
	if (cmd == FU_MSGPACK_CMD_UINT16) {
		guint16 v = 0;
		if (!fu_memread_uint16_safe(buf->data, buf->len, *offset, &v, G_BIG_ENDIAN, error))
			return NULL;
		*offset += sizeof(v);
		return fu_msgpack_item_new_integer(v);
	}
	if (cmd == FU_MSGPACK_CMD_UINT32) {
		guint32 v = 0;
		if (!fu_memread_uint32_safe(buf->data, buf->len, *offset, &v, G_BIG_ENDIAN, error))
			return NULL;
		*offset += sizeof(v);
		return fu_msgpack_item_new_integer(v);
	}
	if (cmd == FU_MSGPACK_CMD_UINT64) {
		guint64 v = 0;
		if (!fu_memread_uint64_safe(buf->data, buf->len, *offset, &v, G_BIG_ENDIAN, error))
			return NULL;
		*offset += sizeof(v);
		return fu_msgpack_item_new_integer(v);
	}

	/* float */
	if (cmd == FU_MSGPACK_CMD_FLOAT64) {
		gdouble v = 0.;
		if (!fu_memread_uint64_safe(buf->data,
					    buf->len,
					    *offset,
					    (guint64 *)&v,
					    G_BIG_ENDIAN,
					    error))
			return NULL;
		*offset += sizeof(v);
		return fu_msgpack_item_new_float(v);
	}

	/* string */
	if (cmd >= FU_MSGPACK_CMD_FIXSTR && cmd <= FU_MSGPACK_CMD_FIXSTR_END) {
		gsize n = cmd & 0b00011111;
		tmp_string = fu_msgpack_item_read_string(buf, *offset, n, error);
		if (tmp_string == NULL)
			return NULL;
		*offset += n;
		return fu_msgpack_item_new_string(tmp_string);
	}
	if (cmd == FU_MSGPACK_CMD_STR8) {
		guint8 n = 0;
		if (!fu_memread_uint8_safe(buf->data, buf->len, *offset, &n, error))
			return NULL;
		tmp_string = fu_msgpack_item_read_string(buf, (*offset) + sizeof(n), n, error);
		if (tmp_string == NULL)
			return NULL;
		*offset += sizeof(n) + n;
		return fu_msgpack_item_new_string(tmp_string);
	}
	if (cmd == FU_MSGPACK_CMD_STR16) {
		guint16 n = 0;
		if (!fu_memread_uint16_safe(buf->data, buf->len, *offset, &n, G_BIG_ENDIAN, error))
			return NULL;
		tmp_string = fu_msgpack_item_read_string(buf, (*offset) + sizeof(n), n, error);
		if (tmp_string == NULL)
			return NULL;
		*offset += sizeof(n) + n;
		return fu_msgpack_item_new_string(tmp_string);
	}
	if (cmd == FU_MSGPACK_CMD_STR32) {
		guint32 n = 0;
		if (!fu_memread_uint32_safe(buf->data, buf->len, *offset, &n, G_BIG_ENDIAN, error))
			return NULL;
		tmp_string = fu_msgpack_item_read_string(buf, (*offset) + sizeof(n), n, error);
		if (tmp_string == NULL)
			return NULL;
		*offset += sizeof(n) + n;
		return fu_msgpack_item_new_string(tmp_string);
	}

	/* binary */
	if (cmd == FU_MSGPACK_CMD_BIN8) {
		guint8 n = 0;
		if (!fu_memread_uint8_safe(buf->data, buf->len, *offset, &n, error))
			return NULL;
		tmp_binary = fu_msgpack_item_read_binary(buf, (*offset) + sizeof(n), n, error);
		if (tmp_binary == NULL)
			return NULL;
		*offset += sizeof(n) + n;
		return fu_msgpack_item_new_binary(tmp_binary);
	}
	if (cmd == FU_MSGPACK_CMD_BIN16) {
		guint16 n = 0;
		if (!fu_memread_uint16_safe(buf->data, buf->len, *offset, &n, G_BIG_ENDIAN, error))
			return NULL;
		tmp_binary = fu_msgpack_item_read_binary(buf, (*offset) + sizeof(n), n, error);
		if (tmp_binary == NULL)
			return NULL;
		*offset += sizeof(n) + n;
		return fu_msgpack_item_new_binary(tmp_binary);
	}
	if (cmd == FU_MSGPACK_CMD_BIN32) {
		guint32 n = 0;
		if (!fu_memread_uint32_safe(buf->data, buf->len, *offset, &n, G_BIG_ENDIAN, error))
			return NULL;
		tmp_binary = fu_msgpack_item_read_binary(buf, (*offset) + sizeof(n), n, error);
		if (tmp_binary == NULL)
			return NULL;
		*offset += sizeof(n) + n;
		return fu_msgpack_item_new_binary(tmp_binary);
	}

	/* array */
	if (cmd >= FU_MSGPACK_CMD_FIXARRAY && cmd <= FU_MSGPACK_CMD_FIXARRAY_END)
		return fu_msgpack_item_new_array(cmd & 0b00001111);
	if (cmd == FU_MSGPACK_CMD_ARRAY16) {
		guint16 n = 0;
		if (!fu_memread_uint16_safe(buf->data, buf->len, *offset, &n, G_BIG_ENDIAN, error))
			return NULL;
		*offset += sizeof(n);
		return fu_msgpack_item_new_array(n);
	}
	if (cmd == FU_MSGPACK_CMD_ARRAY32) {
		guint32 n = 0;
		if (!fu_memread_uint32_safe(buf->data, buf->len, *offset, &n, G_BIG_ENDIAN, error))
			return NULL;
		*offset += sizeof(n);
		return fu_msgpack_item_new_array(n);
	}

	/* map */
	if (cmd >= FU_MSGPACK_CMD_FIXMAP && cmd <= FU_MSGPACK_CMD_FIXMAP_END)
		return fu_msgpack_item_new_map(cmd & 0b00001111);
	if (cmd == FU_MSGPACK_CMD_MAP16) {
		guint16 n = 0;
		if (!fu_memread_uint16_safe(buf->data, buf->len, *offset, &n, G_BIG_ENDIAN, error))
			return NULL;
		*offset += sizeof(n);
		return fu_msgpack_item_new_map(n);
	}
	if (cmd == FU_MSGPACK_CMD_MAP32) {
		guint32 n = 0;
		if (!fu_memread_uint32_safe(buf->data, buf->len, *offset, &n, G_BIG_ENDIAN, error))
			return NULL;
		*offset += sizeof(n);
		return fu_msgpack_item_new_map(n);
	}

	/* failure */
	g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "failed to parse 0x%02X", cmd);
	return NULL;
}

static void
fu_msgpack_item_init(FuMsgpackItem *self)
{
}

static void
fu_msgpack_item_finalize(GObject *object)
{
	FuMsgpackItem *self = FU_MSGPACK_ITEM(object);

	if (self->stream != NULL) {
		g_object_unref(self->stream);
	} else {
		if (self->kind == FU_MSGPACK_ITEM_KIND_BINARY)
			g_byte_array_unref(self->value.buf);
		if (self->kind == FU_MSGPACK_ITEM_KIND_STRING)
			g_string_free(self->value.str, TRUE);
	}

	G_OBJECT_CLASS(fu_msgpack_item_parent_class)->finalize(object);
}

static void
fu_msgpack_item_class_init(FuMsgpackItemClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_msgpack_item_finalize;
}
