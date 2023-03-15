/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuStruct"

#include "config.h"

#include <gio/gio.h>
#include <string.h>

#include "fu-byte-array.h"
#include "fu-common.h"
#include "fu-mem.h"
#include "fu-string.h"
#include "fu-struct.h"

/**
 * FuStruct:
 *
 * A smart structure that supports endian conversion, arrays, GUIDs, strings, default and constant
 * data of variable size.
 *
 * In most cases the smart structure will be defined in the #GObject `_init()` function:
 *
 *    fu_struct_register(self,
 *                       "StructureName {"
 *                       "    member_id: u16le,"
 *                       "}");
 *
 * ..and then the structure can be used in parsing functions like this:
 *
 *    FuStruct *st = fu_struct_lookup(self, "StructureName");
 *    if (!fu_struct_unpack_full(st, buf, bufsz, offset, error))
 *        return FALSE;
 *    value = fu_struct_get_u16(st, "member_id");
 *
 * ..and binary writing functions like this:
 *
 *    FuStruct *st = fu_struct_lookup(self, "StructureName");
 *    fu_struct_set_u16(st, "member_id", value);
 *    fu_struct_pack_into(st, buf);
 *
 * NOTE: is is very important to only read and write the correct member ID type and non-constant
 * values. For instance, it is invalid to read a `u16le` type into a `guint32` value using
 * `fu_struct_get_u32()`.
 *
 * A more complete example is:
 *
 *    FuAcpiTableHdr {
 *        signature: u32be:: 0x12345678,
 *        length: u32le: $struct_size,
 *        revision: u8,
 *        checksum: u8,
 *        oem_id: 6s: LENOVO,
 *        oem_table_id: 8s,
 *        oem_revision: u32le,
 *        asl_compiler_id: 4s,
 *        asl_compiler_revision: 4u8,
 *    }
 *
 * The types currently supported are:
 *
 * - `u8`: a #guint8
 * - `u16`: a #guint16
 * - `u24`: a 24 bit number represented as a #guint32
 * - `u32`:  #guint32
 * - `u64`:  #guint64
 * - `s`: a string
 * - `guid`: a packed GUID, aliased to `16u8`
 *
 * Additionally, default values can be auto-populated:
 *
 * - `$struct_size`: the struct size, e.g. the value of `fu_struct_size()`
 * - `$struct_offset`: the internal offset in the struct
 * - string values
 * - integer values, specified with a `0x` prefix for base-16 and with no prefix for base-10
 *
 * Any default value prefixed with an additional `:` is set as the default, and is **also**
 * verified during unpacking.
 * This is suitable for constant signature fields where there is no other valid value.
 *
 * NOTE: The endian can be specified either per-struct in the header, or per-field in the type
 * definition. Use `<` for little endian and `>` for big endian.
 */

typedef enum {
	FU_STRUCT_ITEM_TYPE_UNKNOWN,
	FU_STRUCT_ITEM_TYPE_U8,
	FU_STRUCT_ITEM_TYPE_U16,
	FU_STRUCT_ITEM_TYPE_U24,
	FU_STRUCT_ITEM_TYPE_U32,
	FU_STRUCT_ITEM_TYPE_U64,
	FU_STRUCT_ITEM_TYPE_STRING,
} FuStructItemType;

typedef enum {
	FU_STRUCT_ITEM_FLAG_NONE = 0,
	FU_STRUCT_ITEM_FLAG_CONSTANT = 1 << 0,
} FuStructItemFlags;

typedef struct {
	gchar *id;
	gchar *default_value;
	guint multiplier;
	FuStructItemType type;
	FuStructItemFlags flags;
	FuEndianType endian;
	gsize offset;
	GByteArray *buf;
} FuStructItem;

struct _FuStruct {
	GObject parent_instance;
	gchar *name;
	gsize size;
	GHashTable *kvs;
};

G_DEFINE_TYPE(FuStruct, fu_struct, G_TYPE_OBJECT)

static const gchar *
fu_struct_endian_to_string(FuEndianType endian)
{
	if (endian == G_LITTLE_ENDIAN)
		return "le";
	if (endian == G_BIG_ENDIAN)
		return "be";
	return NULL;
}

static const gchar *
fu_struct_item_type_to_string(FuStructItemType type)
{
	if (type == FU_STRUCT_ITEM_TYPE_U8)
		return "u8";
	if (type == FU_STRUCT_ITEM_TYPE_U16)
		return "u16";
	if (type == FU_STRUCT_ITEM_TYPE_U24)
		return "u24";
	if (type == FU_STRUCT_ITEM_TYPE_U32)
		return "u32";
	if (type == FU_STRUCT_ITEM_TYPE_U64)
		return "u64";
	if (type == FU_STRUCT_ITEM_TYPE_STRING)
		return "s";
	return NULL;
}

static gsize
fu_struct_item_size(FuStructItem *item)
{
	gsize multiplier = item->multiplier != 0 ? item->multiplier : 1;
	if (item->type == FU_STRUCT_ITEM_TYPE_U8 || item->type == FU_STRUCT_ITEM_TYPE_STRING)
		return multiplier;
	if (item->type == FU_STRUCT_ITEM_TYPE_U16)
		return multiplier * 2;
	if (item->type == FU_STRUCT_ITEM_TYPE_U24)
		return multiplier * 3;
	if (item->type == FU_STRUCT_ITEM_TYPE_U32)
		return multiplier * 4;
	if (item->type == FU_STRUCT_ITEM_TYPE_U64)
		return multiplier * 8;
	return 0;
}

static gchar *
fu_struct_item_to_string(FuStructItem *item)
{
	GString *str = g_string_new(NULL);
	if (item->id != NULL)
		g_string_append_printf(str, "%s: ", item->id);
	if (item->multiplier > 0)
		g_string_append_printf(str, "%u", item->multiplier);
	g_string_append(str, fu_struct_item_type_to_string(item->type));
	if (item->endian == G_LITTLE_ENDIAN || item->endian == G_BIG_ENDIAN)
		g_string_append(str, fu_struct_endian_to_string(item->endian));
	if (item->default_value != NULL) {
		if (item->flags & FU_STRUCT_ITEM_FLAG_CONSTANT)
			g_string_append_printf(str, ":: %s", item->default_value);
		else
			g_string_append_printf(str, ": %s", item->default_value);
	}
	g_string_append(str, ",");
	g_string_append_printf(str, " // @0x%04x", (guint)item->offset);
	return g_string_free(str, FALSE);
}

static gboolean
fu_struct_parse_item(FuStruct *self,
		     const gchar *id,
		     const gchar *token,
		     const gchar *default_value,
		     FuStructItemFlags flags,
		     GError **error)
{
	const gchar *type;
	guint multiplier = 0;
	guint i;
	g_autofree FuStructItem *item = g_new0(FuStructItem, 1);

	/* find multiplier split */
	for (i = 0; token[i] != '\0'; i++) {
		if (!g_ascii_isdigit(token[i]))
			break;
		multiplier *= 10;
		multiplier += token[i] - '0';
	}

	/* create item */
	type = token + i;
	if (g_strcmp0(type, "u8") == 0) {
		item->type = FU_STRUCT_ITEM_TYPE_U8;
		item->multiplier = multiplier;
	} else if (g_strcmp0(type, "u16le") == 0) {
		item->type = FU_STRUCT_ITEM_TYPE_U16;
		item->endian = G_LITTLE_ENDIAN;
	} else if (g_strcmp0(type, "u16be") == 0) {
		item->type = FU_STRUCT_ITEM_TYPE_U16;
		item->endian = G_BIG_ENDIAN;
	} else if (g_strcmp0(type, "u24le") == 0) {
		item->type = FU_STRUCT_ITEM_TYPE_U24;
		item->endian = G_LITTLE_ENDIAN;
	} else if (g_strcmp0(type, "u24be") == 0) {
		item->type = FU_STRUCT_ITEM_TYPE_U24;
		item->endian = G_BIG_ENDIAN;
	} else if (g_strcmp0(type, "u32le") == 0) {
		item->type = FU_STRUCT_ITEM_TYPE_U32;
		item->endian = G_LITTLE_ENDIAN;
	} else if (g_strcmp0(type, "u32be") == 0) {
		item->type = FU_STRUCT_ITEM_TYPE_U32;
		item->endian = G_BIG_ENDIAN;
	} else if (g_strcmp0(type, "u64le") == 0) {
		item->type = FU_STRUCT_ITEM_TYPE_U64;
		item->endian = G_LITTLE_ENDIAN;
	} else if (g_strcmp0(type, "u64be") == 0) {
		item->type = FU_STRUCT_ITEM_TYPE_U64;
		item->endian = G_BIG_ENDIAN;
	} else if (g_strcmp0(type, "s") == 0) {
		item->type = FU_STRUCT_ITEM_TYPE_STRING;
		item->multiplier = multiplier;
	} else if (g_strcmp0(type, "guid") == 0) {
		item->type = FU_STRUCT_ITEM_TYPE_U8;
		item->multiplier = 16;
	} else {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "unknown type, got %s",
			    token + i);
		return FALSE;
	}
	item->buf = g_byte_array_new();
	item->offset = self->size;
	item->flags = flags;
	item->id = g_strdup(id);
	item->default_value = g_strdup(default_value);

	/* success */
	self->size += fu_struct_item_size(item);
	g_hash_table_insert(self->kvs, g_strdup(id), g_steal_pointer(&item));
	return TRUE;
}

static gboolean
fu_struct_parse_id_format(FuStruct *self, const gchar *token, GError **error)
{
	FuStructItemFlags flags = FU_STRUCT_ITEM_FLAG_NONE;
	const gchar *default_value;
	const gchar *id;
	const gchar *multiplier_type;
	g_auto(GStrv) split = g_strsplit(token, ":", -1);

	if (g_strv_length(split) == 2 || g_strv_length(split) == 3) {
		id = split[0];
		multiplier_type = split[1];
		default_value = split[2];
	} else if (g_strv_length(split) == 4) {
		id = split[0];
		multiplier_type = split[1];
		default_value = split[3];
		flags |= FU_STRUCT_ITEM_FLAG_CONSTANT;
	} else {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "expected id:type, id:type:default or id:type::constant, got %s",
			    token);
		return FALSE;
	}

	/* check key name valid */
	if (id[0] == '\0' || id[1] == '\0') {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "invalid id name %s", id);
		return FALSE;
	}

	/* check key does not already exist */
	if (g_hash_table_contains(self->kvs, id)) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "already added id %s", id);
		return FALSE;
	}

	/* parse type */
	return fu_struct_parse_item(self, id, multiplier_type, default_value, flags, error);
}

static gboolean
fu_struct_parse_token(FuStruct *self, const gchar *token, GError **error)
{
	g_autoptr(GString) str = g_string_new(NULL);

	/* remove all whitespace */
	for (guint i = 0; token[i] != '\0'; i++) {
		if (token[i] == ' ')
			continue;
		g_string_append_c(str, token[i]);
	}
	if (str->len == 0)
		return TRUE;

	/* first line */
	if (self->name == NULL) {
		self->name = g_string_free(g_steal_pointer(&str), FALSE);
		return TRUE;
	}
	return fu_struct_parse_id_format(self, str->str, error);
}

static gint
fu_struct_item_sort_cb(gconstpointer a, gconstpointer b)
{
	FuStructItem *item_a = *((FuStructItem **)a);
	FuStructItem *item_b = *((FuStructItem **)b);
	if (item_a->offset < item_b->offset)
		return -1;
	if (item_a->offset > item_b->offset)
		return 1;
	return 0;
}

static GPtrArray *
fu_struct_get_items(FuStruct *self)
{
	GPtrArray *array = g_ptr_array_new();
	g_autoptr(GList) values = NULL;

	g_return_val_if_fail(FU_IS_STRUCT(self), NULL);

	values = g_hash_table_get_values(self->kvs);
	for (GList *l = values; l != NULL; l = l->next)
		g_ptr_array_add(array, l->data);
	g_ptr_array_sort(array, fu_struct_item_sort_cb);
	return array;
}

static gboolean
fu_struct_item_set_uint(FuStructItem *item, gsize val, GError **error)
{
	if (item->type == FU_STRUCT_ITEM_TYPE_U8) {
		g_byte_array_set_size(item->buf, 0);
		fu_byte_array_append_uint8(item->buf, val);
		return TRUE;
	}
	if (item->type == FU_STRUCT_ITEM_TYPE_U16) {
		g_byte_array_set_size(item->buf, 0);
		fu_byte_array_append_uint16(item->buf, val, item->endian);
		return TRUE;
	}
	if (item->type == FU_STRUCT_ITEM_TYPE_U24) {
		g_byte_array_set_size(item->buf, 0);
		fu_byte_array_append_uint24(item->buf, val, item->endian);
		return TRUE;
	}
	if (item->type == FU_STRUCT_ITEM_TYPE_U32) {
		g_byte_array_set_size(item->buf, 0);
		fu_byte_array_append_uint32(item->buf, val, item->endian);
		return TRUE;
	}
	if (item->type == FU_STRUCT_ITEM_TYPE_U64) {
		g_byte_array_set_size(item->buf, 0);
		fu_byte_array_append_uint64(item->buf, val, item->endian);
		return TRUE;
	}
	g_set_error(error,
		    G_IO_ERROR,
		    G_IO_ERROR_INVALID_DATA,
		    "cannot set item of type %s to %u",
		    fu_struct_item_type_to_string(item->type),
		    (guint)val);
	return FALSE;
}

static gboolean
fu_struct_item_set_string(FuStructItem *item, const gchar *val, GError **error)
{
	gsize len = strlen(val);
	if (item->multiplier < len) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "cannot set string of length 0x%x into buffer of size 0x%x",
			    (guint)len,
			    (guint)item->multiplier);
		return FALSE;
	}
	g_byte_array_set_size(item->buf, len);
	memcpy(item->buf->data, val, len);
	return TRUE;
}

/**
 * fu_struct_new:
 * @fmt: (not nullable): a Rust-style struct string
 * @error: (nullable): optional return location for an error
 *
 * Creates a new smart structure.
 *
 * Returns: (transfer full): a #FuStruct, or %NULL on error
 *
 * Since: 1.8.13
 **/
FuStruct *
fu_struct_new(const gchar *fmt, GError **error)
{
	g_auto(GStrv) tokens = NULL;
	g_autoptr(FuStruct) self = g_object_new(FU_TYPE_STRUCT, NULL);
	g_autoptr(GPtrArray) items = NULL;

	g_return_val_if_fail(fmt != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* split and parse tokens */
	tokens = g_strsplit_set(fmt, "{},\n\t", -1);
	for (guint i = 0; tokens[i] != NULL; i++) {
		if (!fu_struct_parse_token(self, tokens[i], error))
			return NULL;
	}

	/* sanity check */
	if (self->name == NULL) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "no name defined");
		return NULL;
	}
	if (g_str_has_prefix(self->name, "Fu")) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "%s should not be prefixed with 'Fu'",
			    self->name);
		return NULL;
	}
	items = fu_struct_get_items(self);
	if (items->len == 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "no IDs defined for %s",
			    self->name);
		return NULL;
	}

	/* replace any special values */
	for (guint i = 0; i < items->len; i++) {
		FuStructItem *item = g_ptr_array_index(items, i);
		if (item->default_value == NULL)
			continue;
		if (g_strcmp0(item->default_value, "$struct_size") == 0) {
			if (!fu_struct_item_set_uint(item, self->size, error))
				return NULL;
			continue;
		}
		if (g_strcmp0(item->default_value, "$struct_offset") == 0) {
			if (!fu_struct_item_set_uint(item, item->offset, error))
				return NULL;
			continue;
		}
		if (item->type == FU_STRUCT_ITEM_TYPE_STRING) {
			if (!fu_struct_item_set_string(item, item->default_value, error))
				return NULL;
			continue;
		}
		if (item->type == FU_STRUCT_ITEM_TYPE_U8) {
			guint64 val = 0;
			if (!fu_strtoull(item->default_value, &val, 0x0, G_MAXUINT8, error))
				return NULL;
			g_byte_array_set_size(item->buf, 0);
			fu_byte_array_append_uint8(item->buf, val);
			continue;
		}
		if (item->type == FU_STRUCT_ITEM_TYPE_U16) {
			guint64 val = 0;
			if (!fu_strtoull(item->default_value, &val, 0x0, G_MAXUINT16, error))
				return NULL;
			g_byte_array_set_size(item->buf, 0);
			fu_byte_array_append_uint16(item->buf, val, item->endian);
			continue;
		}
		if (item->type == FU_STRUCT_ITEM_TYPE_U24) {
			guint64 val = 0;
			if (!fu_strtoull(item->default_value, &val, 0x0, 0xFFFFFF, error))
				return NULL;
			g_byte_array_set_size(item->buf, 0);
			fu_byte_array_append_uint24(item->buf, val, item->endian);
			continue;
		}
		if (item->type == FU_STRUCT_ITEM_TYPE_U32) {
			guint64 val = 0;
			if (!fu_strtoull(item->default_value, &val, 0x0, G_MAXUINT32, error))
				return NULL;
			g_byte_array_set_size(item->buf, 0);
			fu_byte_array_append_uint32(item->buf, val, item->endian);
			continue;
		}
		if (item->type == FU_STRUCT_ITEM_TYPE_U64) {
			guint64 val = 0;
			if (!fu_strtoull(item->default_value, &val, 0x0, G_MAXUINT64, error))
				return NULL;
			g_byte_array_set_size(item->buf, 0);
			fu_byte_array_append_uint64(item->buf, val, item->endian);
			continue;
		}
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "special value %s is not defined",
			    item->default_value);
		return NULL;
	}

	/* success */
	return g_steal_pointer(&self);
}

/**
 * fu_struct_to_string:
 * @self: a #FuStruct
 *
 * Gets the struct as a format string which is useful for debugging.
 *
 * Returns: (transfer full): a string
 *
 * Since: 1.8.13
 **/
gchar *
fu_struct_to_string(FuStruct *self)
{
	g_autoptr(GPtrArray) items = fu_struct_get_items(self);
	g_autoptr(GString) str = g_string_new(NULL);

	g_return_val_if_fail(FU_IS_STRUCT(self), NULL);

	if (self->name != NULL)
		g_string_append_printf(str, "%s ", self->name);
	g_string_append(str, "{\n");
	for (guint i = 0; i < items->len; i++) {
		FuStructItem *item = g_ptr_array_index(items, i);
		g_autofree gchar *tmp = fu_struct_item_to_string(item);
		g_string_append_printf(str, "    %s\n", tmp);
	}
	g_string_append(str, "}");
	return g_string_free(g_steal_pointer(&str), FALSE);
}

/**
 * fu_struct_size:
 * @self: a #FuStruct
 *
 * Gets the complete size of this smart structure.
 *
 * This value is cached internally.
 *
 * Returns: size in bytes
 *
 * Since: 1.8.13
 **/
gsize
fu_struct_size(FuStruct *self)
{
	g_return_val_if_fail(FU_IS_STRUCT(self), G_MAXSIZE);
	return self->size;
}

/**
 * fu_struct_get_name:
 * @self: a #FuStruct
 *
 * Gets the smart structure name.
 *
 * Returns: string, e.g. `FuEfiFirmwareFile`
 *
 * Since: 1.8.13
 **/
const gchar *
fu_struct_get_name(FuStruct *self)
{
	g_return_val_if_fail(FU_IS_STRUCT(self), NULL);
	return self->name;
}

/**
 * fu_struct_pack:
 * @self: a #FuStruct
 *
 * Packs the smart structure into a byte array.
 *
 * Returns: (transfer full): a #GByteArray, or %NULL on error
 *
 * Since: 1.8.13
 **/
GByteArray *
fu_struct_pack(FuStruct *self)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GPtrArray) items = fu_struct_get_items(self);

	g_return_val_if_fail(FU_IS_STRUCT(self), NULL);

	/* preallocate in one chunk */
	fu_byte_array_set_size(buf, self->size, 0x0);

	/* copy any set fields */
	for (guint i = 0; i < items->len; i++) {
		FuStructItem *item = g_ptr_array_index(items, i);
		if (item->buf->len == 0)
			continue;
		memcpy(buf->data + item->offset, item->buf->data, item->buf->len);
	}

	/* success */
	return g_steal_pointer(&buf);
}

/**
 * fu_struct_pack_into:
 * @self: a #FuStruct
 * @buf: a #GByteArray
 *
 * Appends the smart structure into a byte array.
 *
 * Since: 1.8.13
 **/
void
fu_struct_pack_into(FuStruct *self, GByteArray *buf)
{
	g_autoptr(GByteArray) tmp = NULL;
	g_return_if_fail(FU_IS_STRUCT(self));
	g_return_if_fail(buf != NULL);
	tmp = fu_struct_pack(self);
	g_byte_array_append(buf, tmp->data, tmp->len);
}

/**
 * fu_struct_pack_bytes:
 * @self: a #FuStruct
 *
 * Packs the smart structure into a immutable #GBytes.
 *
 * Returns: (transfer full): a #GByteArray, or %NULL on error
 *
 * Since: 1.8.13
 **/
GBytes *
fu_struct_pack_bytes(FuStruct *self)
{
	g_autoptr(GByteArray) buf = fu_struct_pack(self);
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

/**
 * fu_struct_pack_full:
 * @self: a #FuStruct
 * @buf: a mutable buffer
 * @bufsz: size of @buf
 * @offset: address to start writing from
 * @flags: #FuStructFlags, e.g. %FU_STRUCT_FLAG_NONE
 * @error: (nullable): optional return location for an error
 *
 * Packs the smart structure into an existing byte buffer.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.8.13
 **/
gboolean
fu_struct_pack_full(FuStruct *self,
		    guint8 *buf,
		    gsize bufsz,
		    gsize offset,
		    FuStructFlags flags,
		    GError **error)
{
	g_autoptr(GByteArray) tmp = NULL;

	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(bufsz > 0, FALSE);
	g_return_val_if_fail(offset < bufsz, FALSE);

	tmp = fu_struct_pack(self);
	return fu_memcpy_safe(buf, bufsz, offset, tmp->data, tmp->len, 0x0, tmp->len, error);
}

/**
 * fu_struct_unpack:
 * @self: a #FuStruct
 * @buf: a #GByteArray
 * @error: (nullable): optional return location for an error
 *
 * Unpacks the smart structure from a byte array.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.8.13
 **/
gboolean
fu_struct_unpack(FuStruct *self, GByteArray *buf, GError **error)
{
	g_return_val_if_fail(FU_IS_STRUCT(self), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return fu_struct_unpack_full(self, buf->data, buf->len, 0x0, FU_STRUCT_FLAG_NONE, error);
}

/**
 * fu_struct_unpack_full:
 * @self: a #FuStruct
 * @buf: a const buffer
 * @bufsz: size of @buf
 * @offset: address to start reading from
 * @flags: #FuStructFlags, e.g. %FU_STRUCT_FLAG_ONLY_CONSTANTS
 * @error: (nullable): optional return location for an error
 *
 * Unpacks the smart structure from an existing byte buffer.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.8.13
 **/
gboolean
fu_struct_unpack_full(FuStruct *self,
		      const guint8 *buf,
		      gsize bufsz,
		      gsize offset,
		      FuStructFlags flags,
		      GError **error)
{
	g_autoptr(GPtrArray) items = fu_struct_get_items(self);

	g_return_val_if_fail(FU_IS_STRUCT(self), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(bufsz > 0, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* unpack fields */
	for (guint i = 0; i < items->len; i++) {
		FuStructItem *item = g_ptr_array_index(items, i);
		if (item->flags & FU_STRUCT_ITEM_FLAG_CONSTANT) {
			if (offset + item->offset + item->buf->len > bufsz) {
				g_set_error(error,
					    G_IO_ERROR,
					    G_IO_ERROR_INVALID_DATA,
					    "buffer too small, got 0x%x, needed 0x%x",
					    (guint)bufsz,
					    (guint)(offset + item->offset + item->buf->len));
				return FALSE;
			}
			if (!fu_memcmp_safe(buf + offset + item->offset,
					    item->buf->len,
					    item->buf->data,
					    item->buf->len,
					    error)) {
				g_prefix_error(error,
					       "constant value for %s invalid, expected %s: ",
					       item->id,
					       item->default_value);
				return FALSE;
			}
			continue;
		}
		if ((flags & FU_STRUCT_FLAG_ONLY_CONSTANTS) == 0) {
			g_byte_array_set_size(item->buf, fu_struct_item_size(item));
			if (!fu_memcpy_safe(item->buf->data,
					    item->buf->len,
					    0x0, /* dst */
					    buf,
					    bufsz,
					    offset + item->offset,
					    item->buf->len,
					    error)) {
				g_prefix_error(error, "failed to unpack %s: ", item->id);
				return FALSE;
			}
		}
	}

	return TRUE;
}

/**
 * fu_struct_get_id_offset:
 * @self: a #FuStruct
 * @id: struct ID, e.g. `vendor_id`
 *
 * Gets the offset to the struct ID.
 *
 * NOTE: It is a programmer to attempt to get a structure ID that does not exist.
 *
 * Returns: offset in bytes
 *
 * Since: 1.8.13
 **/
gsize
fu_struct_get_id_offset(FuStruct *self, const gchar *id)
{
	FuStructItem *item;

	g_return_val_if_fail(FU_IS_STRUCT(self), 0);
	g_return_val_if_fail(id != NULL, 0);

	item = g_hash_table_lookup(self->kvs, id);
	g_return_val_if_fail(item != NULL, 0);
	return item->offset;
}

/**
 * fu_struct_get_id_size:
 * @self: a #FuStruct
 * @id: struct ID, e.g. `vendor_id`
 *
 * Gets the size of the struct ID.
 *
 * NOTE: It is a programmer to attempt to get a structure ID that does not exist.
 *
 * Returns: offset in bytes
 *
 * Since: 1.8.13
 **/
gsize
fu_struct_get_id_size(FuStruct *self, const gchar *id)
{
	FuStructItem *item;

	g_return_val_if_fail(FU_IS_STRUCT(self), 0);
	g_return_val_if_fail(id != NULL, 0);

	item = g_hash_table_lookup(self->kvs, id);
	g_return_val_if_fail(item != NULL, 0);
	return fu_struct_item_size(item);
}

/**
 * fu_struct_get_u8:
 * @self: a #FuStruct
 * @id: struct ID, e.g. `vendor_id`
 *
 * Gets a #guint8 value.
 *
 * NOTE: It is a programmer to attempt to get a structure ID that does not exist, or to try to read
 * the ID as the wrong type.
 *
 * Returns: integer value
 *
 * Since: 1.8.13
 **/
guint8
fu_struct_get_u8(FuStruct *self, const gchar *id)
{
	FuStructItem *item;

	g_return_val_if_fail(FU_IS_STRUCT(self), 0);
	g_return_val_if_fail(id != NULL, 0);

	item = g_hash_table_lookup(self->kvs, id);
	g_return_val_if_fail(item != NULL, 0);
	g_return_val_if_fail(item->type == FU_STRUCT_ITEM_TYPE_U8, 0);
	g_return_val_if_fail(item->multiplier == 0, 0);
	g_return_val_if_fail(item->buf->len > 0, 0);
	g_return_val_if_fail((item->flags & FU_STRUCT_ITEM_FLAG_CONSTANT) == 0, 0);

	return item->buf->data[0];
}

/**
 * fu_struct_get_u24:
 * @self: a #FuStruct
 * @id: struct ID, e.g. `vendor_id`
 *
 * Gets a uint24 value.
 *
 * NOTE: It is a programmer to attempt to get a structure ID that does not exist, or to try to read
 * the ID as the wrong type.
 *
 * Returns: integer value
 *
 * Since: 1.8.13
 **/
guint32
fu_struct_get_u24(FuStruct *self, const gchar *id)
{
	FuStructItem *item;

	g_return_val_if_fail(FU_IS_STRUCT(self), 0);
	g_return_val_if_fail(id != NULL, 0);

	item = g_hash_table_lookup(self->kvs, id);
	g_return_val_if_fail(item != NULL, 0);
	g_return_val_if_fail(item->type == FU_STRUCT_ITEM_TYPE_U24, 0);
	g_return_val_if_fail(item->multiplier == 0, 0);
	g_return_val_if_fail(item->buf->len > 0, 0);
	g_return_val_if_fail((item->flags & FU_STRUCT_ITEM_FLAG_CONSTANT) == 0, 0);

	return fu_memread_uint24(item->buf->data, item->endian);
}

/**
 * fu_struct_get_u32:
 * @self: a #FuStruct
 * @id: struct ID, e.g. `vendor_id`
 *
 * Gets a #guint32 value.
 *
 * NOTE: It is a programmer to attempt to get a structure ID that does not exist, or to try to read
 * the ID as the wrong type.
 *
 * Returns: integer value
 *
 * Since: 1.8.13
 **/
guint32
fu_struct_get_u32(FuStruct *self, const gchar *id)
{
	FuStructItem *item;

	g_return_val_if_fail(FU_IS_STRUCT(self), 0);
	g_return_val_if_fail(id != NULL, 0);

	item = g_hash_table_lookup(self->kvs, id);
	g_return_val_if_fail(item != NULL, 0);
	g_return_val_if_fail(item->type == FU_STRUCT_ITEM_TYPE_U32, 0);
	g_return_val_if_fail(item->multiplier == 0, 0);
	g_return_val_if_fail(item->buf->len > 0, 0);
	g_return_val_if_fail((item->flags & FU_STRUCT_ITEM_FLAG_CONSTANT) == 0, 0);

	return fu_memread_uint32(item->buf->data, item->endian);
}

/**
 * fu_struct_get_u64:
 * @self: a #FuStruct
 * @id: struct ID, e.g. `vendor_id`
 *
 * Gets a #guint64 value.
 *
 * NOTE: It is a programmer to attempt to get a structure ID that does not exist, or to try to read
 * the ID as the wrong type.
 *
 * Returns: integer value
 *
 * Since: 1.8.13
 **/
guint64
fu_struct_get_u64(FuStruct *self, const gchar *id)
{
	FuStructItem *item;

	g_return_val_if_fail(FU_IS_STRUCT(self), 0);
	g_return_val_if_fail(id != NULL, 0);

	item = g_hash_table_lookup(self->kvs, id);
	g_return_val_if_fail(item != NULL, 0);
	g_return_val_if_fail(item->type == FU_STRUCT_ITEM_TYPE_U64, 0);
	g_return_val_if_fail(item->multiplier == 0, 0);
	g_return_val_if_fail(item->buf->len > 0, 0);
	g_return_val_if_fail((item->flags & FU_STRUCT_ITEM_FLAG_CONSTANT) == 0, 0);

	return fu_memread_uint64(item->buf->data, item->endian);
}

/**
 * fu_struct_get_u16:
 * @self: a #FuStruct
 * @id: struct ID, e.g. `vendor_id`
 *
 * Gets a #guint16 value.
 *
 * NOTE: It is a programmer to attempt to get a structure ID that does not exist, or to try to read
 * the ID as the wrong type.
 *
 * Returns: integer value
 *
 * Since: 1.8.13
 **/
guint16
fu_struct_get_u16(FuStruct *self, const gchar *id)
{
	FuStructItem *item;

	g_return_val_if_fail(FU_IS_STRUCT(self), 0);
	g_return_val_if_fail(id != NULL, 0);

	item = g_hash_table_lookup(self->kvs, id);
	g_return_val_if_fail(item != NULL, 0);
	g_return_val_if_fail(item->type == FU_STRUCT_ITEM_TYPE_U16, 0);
	g_return_val_if_fail(item->multiplier == 0, 0);
	g_return_val_if_fail(item->buf->len > 0, 0);
	g_return_val_if_fail((item->flags & FU_STRUCT_ITEM_FLAG_CONSTANT) == 0, 0);

	return fu_memread_uint16(item->buf->data, item->endian);
}

/**
 * fu_struct_get_u8ptr:
 * @self: a #FuStruct
 * @id: struct ID, e.g. `vendor_id`
 * @bufsz: size of @buf
 *
 * Gets a raw byte buffer.
 *
 * Returns: (transfer none): a immutable byte buffer, or %NULL on error
 *
 * Since: 1.8.13
 **/
const guint8 *
fu_struct_get_u8ptr(FuStruct *self, const gchar *id, gsize *bufsz)
{
	FuStructItem *item;

	g_return_val_if_fail(FU_IS_STRUCT(self), NULL);
	g_return_val_if_fail(id != NULL, NULL);

	item = g_hash_table_lookup(self->kvs, id);
	g_return_val_if_fail(item != NULL, NULL);
	g_return_val_if_fail(item->type == FU_STRUCT_ITEM_TYPE_U8, NULL);
	g_return_val_if_fail(item->multiplier > 0, NULL);
	g_return_val_if_fail(item->buf->len > 0, NULL);
	g_return_val_if_fail((item->flags & FU_STRUCT_ITEM_FLAG_CONSTANT) == 0, NULL);

	if (bufsz != NULL)
		*bufsz = item->buf->len;
	return (guint8 *)item->buf->data;
}

/**
 * fu_struct_get_guid:
 * @self: a #FuStruct
 * @id: struct ID, e.g. `vendor_id`
 *
 * Gets a GUID.
 *
 * Returns: (transfer none): a immutable byte buffer, or %NULL on error
 *
 * Since: 1.8.13
 **/
const fwupd_guid_t *
fu_struct_get_guid(FuStruct *self, const gchar *id)
{
	FuStructItem *item;

	g_return_val_if_fail(FU_IS_STRUCT(self), NULL);
	g_return_val_if_fail(id != NULL, NULL);

	item = g_hash_table_lookup(self->kvs, id);
	g_return_val_if_fail(item != NULL, NULL);
	g_return_val_if_fail(item->type == FU_STRUCT_ITEM_TYPE_U8, NULL);
	g_return_val_if_fail(item->multiplier == 16, NULL);
	g_return_val_if_fail((item->flags & FU_STRUCT_ITEM_FLAG_CONSTANT) == 0, NULL);
	return (fwupd_guid_t *)item->buf->data;
}

/**
 * fu_struct_get_string:
 * @self: a #FuStruct
 * @id: struct ID, e.g. `vendor_id`
 *
 * Gets a string value.
 *
 * NOTE: It is a programmer to attempt to get a structure ID that does not exist, or to try to read
 * the ID as the wrong type.
 *
 * Returns: (transfer full): a string that can safely be printed, or %NULL if invalid
 *
 * Since: 1.8.13
 **/
gchar *
fu_struct_get_string(FuStruct *self, const gchar *id)
{
	FuStructItem *item;

	g_return_val_if_fail(FU_IS_STRUCT(self), NULL);
	g_return_val_if_fail(id != NULL, NULL);

	item = g_hash_table_lookup(self->kvs, id);
	g_return_val_if_fail(item != NULL, NULL);
	g_return_val_if_fail(item->type == FU_STRUCT_ITEM_TYPE_STRING, NULL);
	g_return_val_if_fail(item->multiplier > 0, NULL);
	g_return_val_if_fail(item->buf->len > 0, NULL);
	g_return_val_if_fail((item->flags & FU_STRUCT_ITEM_FLAG_CONSTANT) == 0, NULL);

	return fu_strsafe((const gchar *)item->buf->data, item->buf->len);
}

/**
 * fu_struct_set_u8:
 * @self: a #FuStruct
 * @id: struct ID, e.g. `vendor_id`
 * @val: value to set
 *
 * Sets a #guint8 value.
 *
 * NOTE: It is a programmer to attempt to set a structure ID that does not exist, or to try to
 * write the ID as the wrong type.
 *
 * Since: 1.8.13
 **/
void
fu_struct_set_u8(FuStruct *self, const gchar *id, guint8 val)
{
	FuStructItem *item;

	g_return_if_fail(FU_IS_STRUCT(self));

	item = g_hash_table_lookup(self->kvs, id);
	g_return_if_fail(item != NULL);
	g_return_if_fail(item->type == FU_STRUCT_ITEM_TYPE_U8);
	g_return_if_fail(item->multiplier == 0);
	g_return_if_fail((item->flags & FU_STRUCT_ITEM_FLAG_CONSTANT) == 0);

	g_byte_array_set_size(item->buf, 0);
	fu_byte_array_append_uint8(item->buf, val);
}

/**
 * fu_struct_set_u16:
 * @self: a #FuStruct
 * @id: struct ID, e.g. `vendor_id`
 * @val: value to set
 *
 * Sets a #guint16 value.
 *
 * NOTE: It is a programmer to attempt to set a structure ID that does not exist, or to try to
 * write the ID as the wrong type.
 *
 * Since: 1.8.13
 **/
void
fu_struct_set_u16(FuStruct *self, const gchar *id, guint16 val)
{
	FuStructItem *item;

	g_return_if_fail(FU_IS_STRUCT(self));

	item = g_hash_table_lookup(self->kvs, id);
	g_return_if_fail(item != NULL);
	g_return_if_fail(item->type == FU_STRUCT_ITEM_TYPE_U16);
	g_return_if_fail(item->multiplier == 0);
	g_return_if_fail((item->flags & FU_STRUCT_ITEM_FLAG_CONSTANT) == 0);

	g_byte_array_set_size(item->buf, 0);
	fu_byte_array_append_uint16(item->buf, val, item->endian);
}

/**
 * fu_struct_set_u24:
 * @self: a #FuStruct
 * @id: struct ID, e.g. `vendor_id`
 * @val: value to set
 *
 * Sets a uint24 value.
 *
 * NOTE: It is a programmer to attempt to set a structure ID that does not exist, or to try to
 * write the ID as the wrong type.
 *
 * Since: 1.8.13
 **/
void
fu_struct_set_u24(FuStruct *self, const gchar *id, guint32 val)
{
	FuStructItem *item;

	g_return_if_fail(FU_IS_STRUCT(self));

	item = g_hash_table_lookup(self->kvs, id);
	g_return_if_fail(item != NULL);
	g_return_if_fail(item->type == FU_STRUCT_ITEM_TYPE_U24);
	g_return_if_fail(item->multiplier == 0);
	g_return_if_fail((item->flags & FU_STRUCT_ITEM_FLAG_CONSTANT) == 0);

	g_byte_array_set_size(item->buf, 0);
	fu_byte_array_append_uint24(item->buf, val, item->endian);
}

/**
 * fu_struct_set_u32:
 * @self: a #FuStruct
 * @id: struct ID, e.g. `vendor_id`
 * @val: value to set
 *
 * Sets a #guint32 value.
 *
 * NOTE: It is a programmer to attempt to set a structure ID that does not exist, or to try to
 * write the ID as the wrong type.
 *
 * Since: 1.8.13
 **/
void
fu_struct_set_u32(FuStruct *self, const gchar *id, guint32 val)
{
	FuStructItem *item;

	g_return_if_fail(FU_IS_STRUCT(self));

	item = g_hash_table_lookup(self->kvs, id);
	g_return_if_fail(item != NULL);
	g_return_if_fail(item->type == FU_STRUCT_ITEM_TYPE_U32);
	g_return_if_fail(item->multiplier == 0);
	g_return_if_fail((item->flags & FU_STRUCT_ITEM_FLAG_CONSTANT) == 0);

	g_byte_array_set_size(item->buf, 0);
	fu_byte_array_append_uint32(item->buf, val, item->endian);
}

/**
 * fu_struct_set_u64:
 * @self: a #FuStruct
 * @id: struct ID, e.g. `vendor_id`
 * @val: value to set
 *
 * Sets a #guint64 value.
 *
 * NOTE: It is a programmer to attempt to set a structure ID that does not exist, or to try to
 * write the ID as the wrong type.
 *
 * Since: 1.8.13
 **/
void
fu_struct_set_u64(FuStruct *self, const gchar *id, guint64 val)
{
	FuStructItem *item;

	g_return_if_fail(FU_IS_STRUCT(self));

	item = g_hash_table_lookup(self->kvs, id);
	g_return_if_fail(item != NULL);
	g_return_if_fail(item->type == FU_STRUCT_ITEM_TYPE_U64);
	g_return_if_fail(item->multiplier == 0);
	g_return_if_fail((item->flags & FU_STRUCT_ITEM_FLAG_CONSTANT) == 0);

	g_byte_array_set_size(item->buf, 0);
	fu_byte_array_append_uint64(item->buf, val, item->endian);
}

/**
 * fu_struct_set_u8ptr:
 * @self: a #FuStruct
 * @id: struct ID, e.g. `vendor_id`
 * @buf: a const buffer
 * @bufsz: size of @buf
 *
 * Sets a byte buffer.
 *
 * NOTE: It is a programmer to attempt to set a structure ID that does not exist, or to try to
 * write the ID as the wrong type.
 *
 * Since: 1.8.13
 **/
void
fu_struct_set_u8ptr(FuStruct *self, const gchar *id, const guint8 *buf, gsize bufsz)
{
	FuStructItem *item;

	g_return_if_fail(FU_IS_STRUCT(self));
	g_return_if_fail(id != NULL);
	g_return_if_fail(buf != NULL);
	g_return_if_fail(bufsz > 0);

	item = g_hash_table_lookup(self->kvs, id);
	g_return_if_fail(item != NULL);
	g_return_if_fail(item->type == FU_STRUCT_ITEM_TYPE_U8);
	g_return_if_fail(item->multiplier > 0);
	g_return_if_fail(item->multiplier <= bufsz);
	g_return_if_fail((item->flags & FU_STRUCT_ITEM_FLAG_CONSTANT) == 0);

	g_byte_array_set_size(item->buf, bufsz);
	memcpy(item->buf->data, buf, bufsz);
}

/**
 * fu_struct_set_guid:
 * @self: a #FuStruct
 * @id: struct ID, e.g. `vendor_id`
 * @guid: a #fwupd_guid_t
 *
 * Sets a GUID.
 *
 * NOTE: It is a programmer to attempt to set a structure ID that does not exist, or to try to
 * write the ID as the wrong type.
 *
 * Since: 1.8.13
 **/
void
fu_struct_set_guid(FuStruct *self, const gchar *id, const fwupd_guid_t *guid)
{
	FuStructItem *item;

	g_return_if_fail(FU_IS_STRUCT(self));
	g_return_if_fail(id != NULL);
	g_return_if_fail(guid != NULL);

	item = g_hash_table_lookup(self->kvs, id);
	g_return_if_fail(item != NULL);
	g_return_if_fail(item->type == FU_STRUCT_ITEM_TYPE_U8);
	g_return_if_fail(item->multiplier == 16);
	g_return_if_fail((item->flags & FU_STRUCT_ITEM_FLAG_CONSTANT) == 0);

	g_byte_array_set_size(item->buf, sizeof(*guid));
	memcpy(item->buf->data, guid, sizeof(*guid));
}

/**
 * fu_struct_set_string:
 * @self: a #FuStruct
 * @id: struct ID, e.g. `vendor_id`
 * @val: value to set
 * @error: (nullable): optional return location for an error
 *
 * Sets a string value if possible.
 *
 * NOTE: It is a programmer to attempt to set a structure ID that does not exist, or to try to
 * write the ID as the wrong type.
 *
 * Returns: %TRUE if the string was set
 *
 * Since: 1.8.13
 **/
gboolean
fu_struct_set_string(FuStruct *self, const gchar *id, const gchar *val, GError **error)
{
	FuStructItem *item;

	g_return_val_if_fail(FU_IS_STRUCT(self), FALSE);
	g_return_val_if_fail(id != NULL, FALSE);
	g_return_val_if_fail(val != NULL, FALSE);

	item = g_hash_table_lookup(self->kvs, id);
	g_return_val_if_fail(item != NULL, FALSE);
	g_return_val_if_fail(item->type == FU_STRUCT_ITEM_TYPE_STRING, FALSE);
	g_return_val_if_fail(item->multiplier > 0, FALSE);
	g_return_val_if_fail((item->flags & FU_STRUCT_ITEM_FLAG_CONSTANT) == 0, FALSE);

	return fu_struct_item_set_string(item, val, error);
}

/**
 * fu_struct_set_string_literal:
 * @self: a #FuStruct
 * @id: struct ID, e.g. `vendor_id`
 * @val: value to set
 *
 * Sets a literal value.
 *
 * NOTE: It is a programmer to attempt to set a structure ID that does not exist, to try to
 * write the ID as the wrong type, or to write a string longer than the buffer size.
 *
 * Since: 1.8.13
 **/
void
fu_struct_set_string_literal(FuStruct *self, const gchar *id, const gchar *val)
{
	FuStructItem *item;
	g_autoptr(GError) error_local = NULL;

	g_return_if_fail(FU_IS_STRUCT(self));
	g_return_if_fail(id != NULL);
	g_return_if_fail(val != NULL);

	item = g_hash_table_lookup(self->kvs, id);
	g_return_if_fail(item != NULL);
	g_return_if_fail(item->type == FU_STRUCT_ITEM_TYPE_STRING);
	g_return_if_fail(item->multiplier > 0);
	g_return_if_fail((item->flags & FU_STRUCT_ITEM_FLAG_CONSTANT) == 0);

	if (!fu_struct_item_set_string(item, val, &error_local))
		g_critical("%s", error_local->message);
}

static void
fu_struct_item_free(FuStructItem *item)
{
	g_free(item->id);
	g_free(item->default_value);
	g_byte_array_unref(item->buf);
	g_free(item);
}

static void
fu_struct_finalize(GObject *object)
{
	FuStruct *self = FU_STRUCT(object);

	g_free(self->name);
	g_hash_table_unref(self->kvs);

	G_OBJECT_CLASS(fu_struct_parent_class)->finalize(object);
}

static void
fu_struct_class_init(FuStructClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_struct_finalize;
}

static void
fu_struct_init(FuStruct *self)
{
	self->kvs = g_hash_table_new_full(g_str_hash,
					  g_str_equal,
					  g_free,
					  (GDestroyNotify)fu_struct_item_free);
}

/**
 * fu_struct_register:
 * @obj: a #GObject
 * @fmt: (not nullable): a Rust-style struct string
 *
 * Registers a smart structure with the current GObject.
 *
 * Since: 1.8.13
 **/
void
fu_struct_register(gpointer obj, const gchar *fmt)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(FuStruct) st = NULL;
	g_return_if_fail(G_IS_OBJECT(obj));
	g_return_if_fail(fmt != NULL);
	st = fu_struct_new(fmt, &error);
	if (st == NULL)
		g_critical("failed to create FuStruct: %s", error->message);
	g_return_if_fail(g_object_get_data(G_OBJECT(obj), fu_struct_get_name(st)) == NULL);
	g_object_set_data_full(G_OBJECT(obj),
			       fu_struct_get_name(st),
			       g_object_ref(st),
			       (GDestroyNotify)g_object_unref);
}

/**
 * fu_struct_lookup:
 * @obj: a #GObject
 * @name: struct name, e.g. `vendor_id`
 *
 * Gets a smart structure registered with fu_struct_register().
 *
 * Returns: (transfer none): a #FuStruct
 *
 * Since: 1.8.13
 **/
FuStruct *
fu_struct_lookup(gpointer obj, const gchar *name)
{
	return FU_STRUCT(g_object_get_data(G_OBJECT(obj), name));
}
