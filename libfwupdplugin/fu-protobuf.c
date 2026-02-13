/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuProtobuf"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-fuzzer.h"
#include "fu-mem-private.h"
#include "fu-protobuf-struct.h"
#include "fu-protobuf.h"

struct _FuProtobuf {
	GObject parent_instance;
	GByteArray *buf;
};

static void
fu_protobuf_fuzzer_iface_init(FuFuzzerInterface *iface);

G_DEFINE_TYPE_EXTENDED(FuProtobuf,
		       fu_protobuf,
		       G_TYPE_OBJECT,
		       0,
		       G_IMPLEMENT_INTERFACE(FU_TYPE_FUZZER, fu_protobuf_fuzzer_iface_init));

/**
 * fu_protobuf_write
 * @self: a #FuProtobuf
 *
 * Converts the protobuf to a bytes representation.
 *
 * Returns: (transfer full): a #GByteArray
 *
 * Since: 2.1.1
 **/
GByteArray *
fu_protobuf_write(FuProtobuf *self)
{
	g_return_val_if_fail(FU_IS_PROTOBUF(self), NULL);
	return g_byte_array_ref(self->buf);
}

/**
 * fu_protobuf_to_string
 * @self: a #FuProtobuf
 *
 * Converts the protobuf to a string representation.
 *
 * Returns: (transfer full): string
 *
 * Since: 2.1.1
 **/
gchar *
fu_protobuf_to_string(FuProtobuf *self)
{
	g_return_val_if_fail(FU_IS_PROTOBUF(self), NULL);
	return fu_byte_array_to_string(self->buf);
}

static void
fu_protobuf_add_varint_raw(FuProtobuf *self, guint64 value)
{
	guint8 tmp = 0;
	while (1) {
		tmp = value & 0b01111111;
		value >>= 7;
		if (value == 0)
			break;
		fu_byte_array_append_uint8(self->buf, 0b10000000 | tmp);
	}
	fu_byte_array_append_uint8(self->buf, tmp);
}

static void
fu_protobuf_add_tag_raw(FuProtobuf *self, FuProtobufWireType wire_type, guint8 fnum)
{
	fu_byte_array_append_uint8(self->buf, fnum << 3 | wire_type);
}

/**
 * fu_protobuf_add_uint64
 * @self: a #FuProtobuf
 * @fnum: a field number
 * @value: data
 *
 * Adds a `guint64` tag to the protobuf using a `varint` wire type.
 *
 * NOTE: You cannot add a value of zero; just don't set anything if required.
 *
 * Since: 2.1.1
 **/
void
fu_protobuf_add_uint64(FuProtobuf *self, guint8 fnum, guint64 value)
{
	g_return_if_fail(FU_IS_PROTOBUF(self));
	g_return_if_fail(fnum <= 0b11111);
	g_return_if_fail(value != 0);
	fu_protobuf_add_tag_raw(self, FU_PROTOBUF_WIRE_TYPE_VARINT, fnum);
	fu_protobuf_add_varint_raw(self, value);
}

/**
 * fu_protobuf_add_boolean
 * @self: a #FuProtobuf
 * @fnum: a field number
 * @value: data
 *
 * Adds a `gboolean` tag to the protobuf using a `varint` wire type.
 *
 * Since: 2.1.1
 **/
void
fu_protobuf_add_boolean(FuProtobuf *self, guint8 fnum, gboolean value)
{
	g_return_if_fail(FU_IS_PROTOBUF(self));
	g_return_if_fail(fnum <= 0b11111);
	fu_protobuf_add_tag_raw(self, FU_PROTOBUF_WIRE_TYPE_VARINT, fnum);
	fu_protobuf_add_varint_raw(self, value);
}

/**
 * fu_protobuf_add_embedded
 * @self: a #FuProtobuf
 * @fnum: a field number
 * @value: data
 *
 * Adds an embedded tag to the protobuf using a `len` wire type.
 *
 * NOTE: The @pbuf must be non-zero in size.
 *
 * Since: 2.1.1
 **/
void
fu_protobuf_add_embedded(FuProtobuf *self, guint8 fnum, FuProtobuf *pbuf)
{
	g_return_if_fail(FU_IS_PROTOBUF(self));
	g_return_if_fail(fnum <= 0b11111);
	g_return_if_fail(FU_IS_PROTOBUF(pbuf));
	g_return_if_fail(pbuf->buf->len > 0);

	fu_protobuf_add_tag_raw(self, FU_PROTOBUF_WIRE_TYPE_LEN, fnum);
	fu_protobuf_add_varint_raw(self, pbuf->buf->len);
	g_byte_array_append(self->buf, pbuf->buf->data, pbuf->buf->len);
}

/**
 * fu_protobuf_add_string
 * @self: a #FuProtobuf
 * @fnum: a field number
 * @value: (not nullable): data
 *
 * Adds a string tag to the protobuf.
 *
 * NOTE: The string cannot be zero length. Use `fu_protobuf_add_empty()` if required.
 *
 * Since: 2.1.1
 **/
void
fu_protobuf_add_string(FuProtobuf *self, guint8 fnum, const gchar *value)
{
	gsize valuesz;

	g_return_if_fail(FU_IS_PROTOBUF(self));
	g_return_if_fail(fnum <= 0b11111);
	g_return_if_fail(value != NULL);
	g_return_if_fail(value[0] != '\0');

	valuesz = strlen(value);
	fu_protobuf_add_tag_raw(self, FU_PROTOBUF_WIRE_TYPE_LEN, fnum);
	fu_protobuf_add_varint_raw(self, valuesz);
	g_byte_array_append(self->buf, (const guint8 *)value, valuesz);
}

/**
 * fu_protobuf_add_empty
 * @self: a #FuProtobuf
 * @fnum: a field number
 *
 * Adds an empty len tag to the protobuf.
 *
 * Since: 2.1.1
 **/
void
fu_protobuf_add_empty(FuProtobuf *self, guint8 fnum)
{
	g_return_if_fail(FU_IS_PROTOBUF(self));
	g_return_if_fail(fnum <= 0b11111);
	fu_protobuf_add_tag_raw(self, FU_PROTOBUF_WIRE_TYPE_LEN, fnum);
	fu_protobuf_add_varint_raw(self, 0);
}

static gboolean
fu_protobuf_get_varint_raw(FuProtobuf *self, guint64 *value, gsize *offset, GError **error)
{
	guint64 value_tmp = 0;

	/* a maximum of 10 bytes */
	for (guint i = 0; i < 10; i++) {
		guint8 tmp = 0;
		if (!fu_memread_uint8_safe(self->buf->data, self->buf->len, *offset, &tmp, error))
			return FALSE;
		*offset += 1;
		value_tmp += (guint64)(tmp & 0b01111111) << i * 7;
		if ((tmp & 0b10000000) == 0)
			break;
	}

	/* success */
	if (value != NULL)
		*value = value_tmp;
	return TRUE;
}

static gboolean
fu_protobuf_get_tag(FuProtobuf *self,
		    FuProtobufWireType *wire_type,
		    guint8 *fnum,
		    gsize *offset,
		    GError **error)
{
	guint8 tag = 0;

	if (!fu_memread_uint8_safe(self->buf->data, self->buf->len, *offset, &tag, error))
		return FALSE;
	*wire_type = tag & 0b111;
	*fnum = tag >> 3;
	*offset += 1;
	return TRUE;
}

typedef struct {
	gboolean (*varint)(FuProtobuf *self, gpointer data, gsize *offset, GError **error);
	gboolean (*int64)(FuProtobuf *self, gpointer data, gsize *offset, GError **error);
	gboolean (*int32)(FuProtobuf *self, gpointer data, gsize *offset, GError **error);
	gboolean (*len)(FuProtobuf *self, gpointer data, gsize *offset, GError **error);
} FuProtobufCallbacks;

static gboolean
fu_protobuf_process(FuProtobuf *self,
		    guint8 fnum,
		    FuProtobufCallbacks *callbacks,
		    gpointer data,
		    GError **error)
{
	guint8 field_number_tmp = 0;
	gsize offset = 0;
	g_autoptr(GString) fnums_msg = g_string_new(NULL);

	/* field numbers may be declared in any order in a .proto file */
	offset = 0;
	while (offset < self->buf->len) {
		FuProtobufWireType wire_type = 0;
		if (!fu_protobuf_get_tag(self, &wire_type, &field_number_tmp, &offset, error))
			return FALSE;
		g_debug("@0x%x, got wire type %s for field %u",
			(guint)offset,
			fu_protobuf_wire_type_to_string(wire_type),
			field_number_tmp);
		if (fnums_msg->len > 0)
			g_string_append_c(fnums_msg, ',');
		g_string_append_printf(fnums_msg, "%u", field_number_tmp);
		if (wire_type == FU_PROTOBUF_WIRE_TYPE_VARINT) {
			if (fnum == field_number_tmp) {
				if (callbacks->varint == NULL) {
					g_set_error(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_DATA,
						    "field %u matched for varint, but wrong type",
						    fnum);
					return FALSE;
				}
				return callbacks->varint(self, data, &offset, error);
			}
			if (!fu_protobuf_get_varint_raw(self, NULL, &offset, error))
				return FALSE;
		} else if (wire_type == FU_PROTOBUF_WIRE_TYPE_INT64) {
			if (fnum == field_number_tmp) {
				if (callbacks->int64 == NULL) {
					g_set_error(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_DATA,
						    "field %u matched for int64, but wrong type",
						    fnum);
					return FALSE;
				}
				return callbacks->int64(self, data, &offset, error);
			}
			offset += sizeof(guint64);
		} else if (wire_type == FU_PROTOBUF_WIRE_TYPE_LEN) {
			guint64 lensz = 0;
			if (fnum == field_number_tmp) {
				if (callbacks->len == NULL) {
					g_set_error(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_DATA,
						    "field %u matched for len, but wrong type",
						    fnum);
					return FALSE;
				}
				return callbacks->len(self, data, &offset, error);
			}
			if (!fu_protobuf_get_varint_raw(self, &lensz, &offset, error))
				return FALSE;
			if (!g_size_checked_add(&offset, offset, lensz)) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_DATA,
						    "lensz would overflow for wire type len");
				return FALSE;
			}
		} else if (wire_type == FU_PROTOBUF_WIRE_TYPE_INT32) {
			if (fnum == field_number_tmp) {
				if (callbacks->int32 == NULL) {
					g_set_error(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_DATA,
						    "field number %u matched for int32, but not a "
						    "recognized type",
						    fnum);
					return FALSE;
				}
				return callbacks->int32(self, data, &offset, error);
			}
			offset += sizeof(guint32);
		} else {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "unknown wire type %s",
				    fu_protobuf_wire_type_to_string(wire_type));
			return FALSE;
		}
	}

	/* not found */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_FOUND,
		    "no field number %u found, got %s",
		    fnum,
		    fnums_msg->str);
	return FALSE;
}

static gboolean
fu_protobuf_get_uint64_varint_cb(FuProtobuf *self, gpointer data, gsize *offset, GError **error)
{
	guint64 *value = (guint64 *)data;
	return fu_protobuf_get_varint_raw(self, value, offset, error);
}

static gboolean
fu_protobuf_get_uint64_int64_cb(FuProtobuf *self, gpointer data, gsize *offset, GError **error)
{
	guint64 *value = (guint64 *)data;
	if (!fu_memread_uint64_safe(self->buf->data,
				    self->buf->len,
				    *offset,
				    value,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	*offset += sizeof(guint64);
	return TRUE;
}

static gboolean
fu_protobuf_get_uint64_int32_cb(FuProtobuf *self, gpointer data, gsize *offset, GError **error)
{
	guint64 *value = (guint64 *)data;
	guint32 value_tmp = 0;
	if (!fu_memread_uint32_safe(self->buf->data,
				    self->buf->len,
				    *offset,
				    &value_tmp,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	*value = value_tmp;
	*offset += sizeof(guint32);
	return TRUE;
}

/**
 * fu_protobuf_get_uint64
 * @self: a #FuProtobuf
 * @fnum: a field number
 * @value: (not nullable) (out): data
 * @error: (nullable): optional return location for an error
 *
 * Gets a guint64 value from a protobuf, either from a varint or int64 type.
 *
 * Returns: (transfer full): %TRUE on success
 *
 * Since: 2.1.1
 **/
gboolean
fu_protobuf_get_uint64(FuProtobuf *self, guint8 fnum, guint64 *value, GError **error)
{
	FuProtobufCallbacks callbacks = {
	    .varint = fu_protobuf_get_uint64_varint_cb,
	    .int64 = fu_protobuf_get_uint64_int64_cb,
	    .int32 = fu_protobuf_get_uint64_int32_cb,
	};
	g_return_val_if_fail(FU_IS_PROTOBUF(self), FALSE);
	g_return_val_if_fail(fnum <= 0b11111, FALSE);
	g_return_val_if_fail(value != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return fu_protobuf_process(self, fnum, &callbacks, value, error);
}

/**
 * fu_protobuf_get_boolean
 * @self: a #FuProtobuf
 * @fnum: a field number
 * @value: (nullable) (out): data
 * @error: (nullable): optional return location for an error
 *
 * Gets a boolean value from a protobuf, either from a varint or int64 type.
 *
 * Returns: (transfer full): %TRUE on success
 *
 * Since: 2.1.1
 **/
gboolean
fu_protobuf_get_boolean(FuProtobuf *self, guint8 fnum, gboolean *value, GError **error)
{
	FuProtobufCallbacks callbacks = {
	    .varint = fu_protobuf_get_uint64_varint_cb,
	};
	guint64 value_tmp = 0;

	g_return_val_if_fail(FU_IS_PROTOBUF(self), FALSE);
	g_return_val_if_fail(fnum <= 0b11111, FALSE);
	g_return_val_if_fail(value != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	if (!fu_protobuf_process(self, fnum, &callbacks, &value_tmp, error))
		return FALSE;
	if (value_tmp > 1) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "boolean requested, but integer value was 0x%x",
			    (guint)value_tmp);
		return FALSE;
	}
	*value = value_tmp > 0;
	return TRUE;
}

static gboolean
fu_protobuf_get_string_len_cb(FuProtobuf *self, gpointer data, gsize *offset, GError **error)
{
	gchar **value = (gchar **)data;
	guint64 lensz = 0;

	if (!fu_protobuf_get_varint_raw(self, &lensz, offset, error))
		return FALSE;
	*value = fu_memstrsafe(self->buf->data, self->buf->len, *offset, lensz, error);
	if (*value == NULL)
		return FALSE;
	*offset += lensz;
	return TRUE;
}

/**
 * fu_protobuf_get_string
 * @self: a #FuProtobuf
 * @fnum: a field number
 * @error: (nullable): optional return location for an error
 *
 * Gets a string from a protobuf.
 *
 * Returns: (transfer full): the string, or %NULL for error
 *
 * Since: 2.1.1
 **/
gchar *
fu_protobuf_get_string(FuProtobuf *self, guint8 fnum, GError **error)
{
	g_autofree gchar *value = NULL;
	FuProtobufCallbacks callbacks = {
	    .len = fu_protobuf_get_string_len_cb,
	};
	g_return_val_if_fail(FU_IS_PROTOBUF(self), NULL);
	g_return_val_if_fail(fnum <= 0b11111, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	if (!fu_protobuf_process(self, fnum, &callbacks, &value, error))
		return NULL;
	return g_steal_pointer(&value);
}

static gboolean
fu_protobuf_get_embedded_len_cb(FuProtobuf *self, gpointer data, gsize *offset, GError **error)
{
	FuProtobuf *pbuf = FU_PROTOBUF(data);
	guint64 lensz = 0;

	if (!fu_protobuf_get_varint_raw(self, &lensz, offset, error))
		return FALSE;
	if (!fu_memchk_read(self->buf->len, *offset, lensz, error))
		return FALSE;
	g_byte_array_append(pbuf->buf, self->buf->data + *offset, lensz);
	*offset += lensz;
	return TRUE;
}

/**
 * fu_protobuf_get_embedded
 * @self: a #FuProtobuf
 * @fnum: a field number
 * @error: (nullable): optional return location for an error
 *
 * Gets an embedded child protobuf from a protobuf.
 *
 * Returns: (transfer full): a #FuProtobuf, or %NULL for error
 *
 * Since: 2.1.1
 **/
FuProtobuf *
fu_protobuf_get_embedded(FuProtobuf *self, guint8 fnum, GError **error)
{
	FuProtobufCallbacks callbacks = {
	    .len = fu_protobuf_get_embedded_len_cb,
	};
	g_autoptr(FuProtobuf) pbuf = fu_protobuf_new();
	g_return_val_if_fail(FU_IS_PROTOBUF(self), NULL);
	g_return_val_if_fail(fnum <= 0b11111, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	if (!fu_protobuf_process(self, fnum, &callbacks, pbuf, error))
		return NULL;
	return g_steal_pointer(&pbuf);
}

static gboolean
fu_protobuf_fuzzer_test_input(FuFuzzer *fuzzer, GBytes *blob, GError **error)
{
	FuProtobuf *self = FU_PROTOBUF(fuzzer);
	guint64 value = 0;
	g_autofree gchar *str = NULL;
	g_autoptr(FuProtobuf) pbuf = NULL;

	g_byte_array_set_size(self->buf, 0);
	fu_byte_array_append_bytes(self->buf, blob);
	pbuf = fu_protobuf_get_embedded(self, 4, error);
	if (pbuf == NULL)
		return FALSE;
	str = fu_protobuf_get_string(pbuf, 2, error);
	if (str == NULL)
		return FALSE;
	return fu_protobuf_get_uint64(pbuf, 3, &value, error);
}

static GBytes *
fu_protobuf_fuzzer_build_example(FuFuzzer *fuzzer, GBytes *blob, GError **error)
{
	FuProtobuf *self = FU_PROTOBUF(fuzzer);
	g_autoptr(FuProtobuf) pbuf = fu_protobuf_new();
	fu_protobuf_add_uint64(pbuf, 1, 150);
	fu_protobuf_add_string(pbuf, 2, "foo");
	fu_protobuf_add_uint64(pbuf, 3, 1);
	fu_protobuf_add_embedded(self, 4, pbuf);
	return g_bytes_new(self->buf->data, self->buf->len);
}

static void
fu_protobuf_init(FuProtobuf *self)
{
	self->buf = g_byte_array_new();
}

static void
fu_protobuf_finalize(GObject *object)
{
	FuProtobuf *self = FU_PROTOBUF(object);
	g_byte_array_unref(self->buf);
	G_OBJECT_CLASS(fu_protobuf_parent_class)->finalize(object);
}

static void
fu_protobuf_fuzzer_iface_init(FuFuzzerInterface *iface)
{
	iface->test_input = fu_protobuf_fuzzer_test_input;
	iface->build_example = fu_protobuf_fuzzer_build_example;
}

static void
fu_protobuf_class_init(FuProtobufClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_protobuf_finalize;
}

/**
 * fu_protobuf_new:
 *
 * Creates a new protobuf.
 *
 * Returns: (transfer full): a #FuProtobuf
 *
 * Since: 2.1.1
 **/
FuProtobuf *
fu_protobuf_new(void)
{
	return g_object_new(FU_TYPE_PROTOBUF, NULL);
}

/**
 * fu_protobuf_new_from_data:
 * @buf: data
 * @bufsz: size of @buf
 *
 * Creates a new protobuf from a data buffer.
 *
 * Returns: (transfer full): a #FuProtobuf
 *
 * Since: 2.1.1
 **/
FuProtobuf *
fu_protobuf_new_from_data(const guint8 *buf, gsize bufsz)
{
	g_autoptr(FuProtobuf) self = fu_protobuf_new();
	g_byte_array_append(self->buf, buf, bufsz);
	return g_steal_pointer(&self);
}
