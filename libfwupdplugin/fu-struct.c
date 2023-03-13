/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuStruct"

#include "config.h"

#include "fwupd.h"

#include "fu-byte-array.h"
#include "fu-common.h"
#include "fu-mem.h"
#include "fu-struct.h"

#define FU_STRUCT_ENDIAN_UNKNOWN 0

typedef enum {
	FU_STRUCT_FMT_TOKEN_ID_PAD = 'x',
	FU_STRUCT_FMT_TOKEN_ID_UINT8 = 'B',
	FU_STRUCT_FMT_TOKEN_ID_UINT16 = 'H',
	FU_STRUCT_FMT_TOKEN_ID_UINT24 = 'T',
	FU_STRUCT_FMT_TOKEN_ID_UINT32 = 'L',
	FU_STRUCT_FMT_TOKEN_ID_UINT64 = 'Q',
	FU_STRUCT_FMT_TOKEN_ID_STRING = 's',
	FU_STRUCT_FMT_TOKEN_ID_GUID = 'G',
} FuStructFmtTokenId;

typedef enum {
	FU_STRUCT_FMT_TOKEN_FLAG_NONE = 0,
	FU_STRUCT_FMT_TOKEN_FLAG_ADVANCE_OFFSET = 1 << 0,
} FuStructFmtTokenFlag;

typedef struct {
	FuEndianType endian;
	gsize arraysz;
	FuStructFmtTokenId id;
	FuStructFmtTokenFlag flags;
} FuStructFmtToken;

static gsize
fu_struct_fmt_token_id_size(FuStructFmtToken *fmt_token)
{
	gsize arraysz = fmt_token->arraysz > 0 ? fmt_token->arraysz : 1;
	if (fmt_token->id == FU_STRUCT_FMT_TOKEN_ID_PAD ||
	    fmt_token->id == FU_STRUCT_FMT_TOKEN_ID_UINT8 ||
	    fmt_token->id == FU_STRUCT_FMT_TOKEN_ID_STRING)
		return 1 * arraysz;
	if (fmt_token->id == FU_STRUCT_FMT_TOKEN_ID_UINT16)
		return 2 * arraysz;
	if (fmt_token->id == FU_STRUCT_FMT_TOKEN_ID_UINT24)
		return 3 * arraysz;
	if (fmt_token->id == FU_STRUCT_FMT_TOKEN_ID_UINT32)
		return 4 * arraysz;
	if (fmt_token->id == FU_STRUCT_FMT_TOKEN_ID_UINT64)
		return 8 * arraysz;
	if (fmt_token->id == FU_STRUCT_FMT_TOKEN_ID_GUID)
		return sizeof(fwupd_guid_t) * arraysz;
	return 0;
}

static const gchar *
fu_struct_endian_to_string(FuEndianType endian_type)
{
	if (endian_type == G_LITTLE_ENDIAN)
		return "<";
	if (endian_type == G_BIG_ENDIAN)
		return ">";
	if (endian_type == G_BYTE_ORDER)
		return "!";
	return NULL;
}

static FuEndianType
fu_struct_fmt_token_get_endian(FuStructFmtToken *fmt_token)
{
	if (fmt_token->endian == G_LITTLE_ENDIAN || fmt_token->endian == G_BIG_ENDIAN)
		return fmt_token->endian;
	return G_BYTE_ORDER;
}

static gchar *
fu_struct_fmt_token_to_string(FuStructFmtToken *fmt_token)
{
	g_autoptr(GString) str = g_string_new(fu_struct_endian_to_string(fmt_token->endian));
	if (fmt_token->flags & FU_STRUCT_FMT_TOKEN_FLAG_ADVANCE_OFFSET)
		g_string_append_c(str, '[');
	if (fmt_token->arraysz > 0)
		g_string_append_printf(str, "%u", (guint)fmt_token->arraysz);
	g_string_append_c(str, fmt_token->id);
	if (fmt_token->flags & FU_STRUCT_FMT_TOKEN_FLAG_ADVANCE_OFFSET)
		g_string_append_c(str, ']');
	return g_string_free(g_steal_pointer(&str), FALSE);
}

static gchar *
fu_struct_fmt_tokens_to_string(GPtrArray *fmt_tokens)
{
	g_autoptr(GString) str = g_string_new(NULL);
	for (guint i = 0; i < fmt_tokens->len; i++) {
		FuStructFmtToken *fmt_token = g_ptr_array_index(fmt_tokens, i);
		g_autofree gchar *tmp = fu_struct_fmt_token_to_string(fmt_token);
		if (str->len > 0)
			g_string_append_c(str, ',');
		g_string_append(str, tmp);
	}
	return g_string_free(g_steal_pointer(&str), FALSE);
}

static GPtrArray *
fu_struct_parse_fmt(const gchar *fmt, GError **error)
{
	FuEndianType curr_endian = FU_STRUCT_ENDIAN_UNKNOWN;
	FuStructFmtTokenFlag curr_flags = FU_STRUCT_FMT_TOKEN_FLAG_NONE;
	guint curr_arraysz = 0;
	g_autofree gchar *str = NULL;
	g_autoptr(GPtrArray) fmt_tokens = g_ptr_array_new_with_free_func(g_free);

	for (guint i = 0; fmt[i] != '\0'; i++) {
		if (fmt[i] == '!') {
			curr_endian = G_BYTE_ORDER;
			continue;
		}
		if (fmt[i] == '<') {
			curr_endian = G_LITTLE_ENDIAN;
			continue;
		}
		if (fmt[i] == '>') {
			curr_endian = G_BIG_ENDIAN;
			continue;
		}
		if (fmt[i] == '[') {
			curr_flags |= FU_STRUCT_FMT_TOKEN_FLAG_ADVANCE_OFFSET;
			continue;
		}
		if (fmt[i] == ']') {
			curr_flags &= ~FU_STRUCT_FMT_TOKEN_FLAG_ADVANCE_OFFSET;
			continue;
		}
		if (fmt[i] == FU_STRUCT_FMT_TOKEN_ID_UINT8 ||
		    fmt[i] == FU_STRUCT_FMT_TOKEN_ID_UINT16 ||
		    fmt[i] == FU_STRUCT_FMT_TOKEN_ID_UINT24 ||
		    fmt[i] == FU_STRUCT_FMT_TOKEN_ID_UINT32 ||
		    fmt[i] == FU_STRUCT_FMT_TOKEN_ID_UINT64 ||
		    fmt[i] == FU_STRUCT_FMT_TOKEN_ID_PAD) {
			g_autofree FuStructFmtToken *fmt_token = g_new0(FuStructFmtToken, 1);
			fmt_token->id = fmt[i];
			fmt_token->flags = curr_flags;
			fmt_token->endian = curr_endian;
			fmt_token->arraysz = curr_arraysz;
			g_ptr_array_add(fmt_tokens, g_steal_pointer(&fmt_token));
			curr_arraysz = 0;
			continue;
		}
		if (fmt[i] == FU_STRUCT_FMT_TOKEN_ID_STRING) {
			g_autofree FuStructFmtToken *fmt_token = g_new0(FuStructFmtToken, 1);
			fmt_token->id = fmt[i];
			fmt_token->flags = curr_flags;
			fmt_token->endian = curr_endian;
			if (curr_arraysz == 0) {
				g_set_error_literal(error,
						    G_IO_ERROR,
						    G_IO_ERROR_INVALID_DATA,
						    "strings need an explicit size");
				return NULL;
			}
			fmt_token->arraysz = curr_arraysz;
			g_ptr_array_add(fmt_tokens, g_steal_pointer(&fmt_token));
			curr_arraysz = 0;
			continue;
		}
		if (fmt[i] == FU_STRUCT_FMT_TOKEN_ID_GUID) {
			g_autofree FuStructFmtToken *fmt_token = g_new0(FuStructFmtToken, 1);
			fmt_token->id = fmt[i];
			fmt_token->flags = curr_flags;
			fmt_token->endian = curr_endian;
			if (curr_arraysz > 0) {
				g_set_error_literal(error,
						    G_IO_ERROR,
						    G_IO_ERROR_INVALID_DATA,
						    "multiple GUIDs not supported");
				return NULL;
			}
			g_ptr_array_add(fmt_tokens, g_steal_pointer(&fmt_token));
			curr_arraysz = 0;
			continue;
		}
		if (g_ascii_isdigit(fmt[i])) {
			guint8 arraysz = fmt[i] - '0';
			if (curr_arraysz == 0 && arraysz == 0) {
				g_set_error_literal(error,
						    G_IO_ERROR,
						    G_IO_ERROR_INVALID_DATA,
						    "zero-size arrays are not supported");
				return NULL;
			}
			if (curr_arraysz > 0)
				curr_arraysz *= 10;
			curr_arraysz += arraysz;
			continue;
		}
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "failed to parse: %c",
			    fmt[i]);
		return NULL;
	}

	/* success */
	str = fu_struct_fmt_tokens_to_string(fmt_tokens);
	g_debug("parsed '%s' as '%s'", fmt, str);
	return g_steal_pointer(&fmt_tokens);
}

static gsize
fu_struct_fmt_tokens_size(GPtrArray *fmt_tokens)
{
	guint size = 0;
	for (guint i = 0; i < fmt_tokens->len; i++) {
		FuStructFmtToken *fmt_token = g_ptr_array_index(fmt_tokens, i);
		size += fu_struct_fmt_token_id_size(fmt_token);
	}
	return size;
}

/**
 * fu_struct_size:
 * @fmt: (not nullable): format string, e.g. `<BH20Q`
 * @error: (nullable): optional return location for an error
 *
 * Calculate the size of a struct format string.
 *
 * A format string consists of a variable number of tokens consisting of endian descriptors, array
 * sizes and type descriptors.
 *
 * An endian descriptor can be specified any number of times, and consists of:
 * * `!` for native
 * * `<` for little-endian
 * * `>` for big-endian
 *
 * The optional multiplier value is then specified as a decimal integer. This means that values are
 * read as arrays, rather than raw values. This means that the format string of `1B` and `B` are NOT
 * the same.
 *
 * Lastly a field descriptor of:
 * * `B` for byte, i.e. `guint8`
 * * `H` for half, i.e. #guint16
 * * `L` for long, i.e. #guint32
 * * `Q` for quad, i.e. #guint64
 * * `T` for triple, i.e. 3 bytes, but loaded and saved as a #guint32
 * * `x` for padding bytes
 * * `s` for a fixed length string
 * * `G` for GUID, i.e. #fwupd_guid_t
 *
 * NOTE: The format strings supported here are a strict subset of the ones supported by the Python
 * `struct` module. Do not assume all Python features are supported.
 *
 * Additionally, if `[` is seen during unpacking, the offset is advanced until the `]` character
 * is seen. So, the offset pointer in fu_struct_unpack_from(), would be incremented by just 2 bytes
 * with the format string `<BB[BB]` and zero bytes for the format string `<BBBB`.
 *
 * Returns: a number of bytes, or %G_MAXSIZE on parsing error
 *
 * Since: 1.8.13
 **/
gsize
fu_struct_size(const gchar *fmt, GError **error)
{
	g_autoptr(GPtrArray) fmt_tokens = NULL;

	g_return_val_if_fail(fmt != NULL, G_MAXSIZE);
	g_return_val_if_fail(error == NULL || *error == NULL, G_MAXSIZE);

	/* just add up */
	fmt_tokens = fu_struct_parse_fmt(fmt, error);
	if (fmt_tokens == NULL)
		return G_MAXSIZE;
	return fu_struct_fmt_tokens_size(fmt_tokens);
}

static GByteArray *
fu_struct_pack_from_args(const gchar *fmt, va_list args, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GPtrArray) fmt_tokens = NULL;

	fmt_tokens = fu_struct_parse_fmt(fmt, error);
	if (fmt_tokens == NULL)
		return NULL;

	for (guint i = i = 0; i < fmt_tokens->len; i++) {
		FuStructFmtToken *fmt_token = g_ptr_array_index(fmt_tokens, i);
		FuEndianType endian = fu_struct_fmt_token_get_endian(fmt_token);
		guint arraysz = fmt_token->arraysz == 0 ? 1 : fmt_token->arraysz;

		if (fmt_token->id == FU_STRUCT_FMT_TOKEN_ID_PAD) {
			for (guint j = 0; j < arraysz; j++)
				fu_byte_array_append_uint8(buf, 0x0);
			continue;
		}
		if (fmt_token->id == FU_STRUCT_FMT_TOKEN_ID_UINT8) {
			if (fmt_token->arraysz == 0) {
				guint tmp = va_arg(args, guint);
				if (tmp > G_MAXUINT8) {
					g_set_error(error,
						    G_IO_ERROR,
						    G_IO_ERROR_INVALID_DATA,
						    "argument %u is out of range: %u",
						    i,
						    tmp);
					return NULL;
				}
				fu_byte_array_append_uint8(buf, tmp);
			} else {
				guint8 *tmp = va_arg(args, guint8 *);
				for (guint j = 0; j < fmt_token->arraysz; j++)
					fu_byte_array_append_uint8(buf, tmp[j]);
			}
			continue;
		}
		if (fmt_token->id == FU_STRUCT_FMT_TOKEN_ID_UINT16) {
			if (fmt_token->arraysz == 0) {
				guint tmp = va_arg(args, guint);
				if (tmp > G_MAXUINT16) {
					g_set_error(error,
						    G_IO_ERROR,
						    G_IO_ERROR_INVALID_DATA,
						    "argument %u is out of range: %u",
						    i,
						    tmp);
					return NULL;
				}
				fu_byte_array_append_uint16(buf, tmp, endian);
			} else {
				guint16 *tmp = va_arg(args, guint16 *);
				for (guint j = 0; j < fmt_token->arraysz; j++)
					fu_byte_array_append_uint16(buf, tmp[j], endian);
			}
			continue;
		}
		if (fmt_token->id == FU_STRUCT_FMT_TOKEN_ID_UINT24) {
			if (fmt_token->arraysz == 0) {
				guint tmp = va_arg(args, guint);
				if (tmp > 0xFFFFFF) {
					g_set_error(error,
						    G_IO_ERROR,
						    G_IO_ERROR_INVALID_DATA,
						    "argument %u is out of range: %u",
						    i,
						    tmp);
					return NULL;
				}
				fu_byte_array_append_uint24(buf, tmp, endian);
			} else {
				guint32 *tmp = va_arg(args, guint32 *);
				for (guint j = 0; j < fmt_token->arraysz; j++)
					fu_byte_array_append_uint24(buf, tmp[j], endian);
			}
			continue;
		}
		if (fmt_token->id == FU_STRUCT_FMT_TOKEN_ID_UINT32) {
			if (fmt_token->arraysz == 0) {
				guint tmp = va_arg(args, guint);
				if (tmp > G_MAXUINT32) {
					g_set_error(error,
						    G_IO_ERROR,
						    G_IO_ERROR_INVALID_DATA,
						    "argument %u is out of range: %u",
						    i,
						    tmp);
					return NULL;
				}
				fu_byte_array_append_uint32(buf, tmp, endian);
			} else {
				guint32 *tmp = va_arg(args, guint32 *);
				for (guint j = 0; j < fmt_token->arraysz; j++)
					fu_byte_array_append_uint32(buf, tmp[j], endian);
			}
			continue;
		}
		if (fmt_token->id == FU_STRUCT_FMT_TOKEN_ID_UINT64) {
			if (fmt_token->arraysz == 0) {
				fu_byte_array_append_uint64(buf, va_arg(args, guint64), endian);
			} else {
				guint64 *tmp = va_arg(args, guint64 *);
				for (guint j = 0; j < fmt_token->arraysz; j++)
					fu_byte_array_append_uint64(buf, tmp[j], endian);
			}
			continue;
		}
		if (fmt_token->id == FU_STRUCT_FMT_TOKEN_ID_STRING) {
			const gchar *tmp = va_arg(args, const gchar *);
			gsize tmpsz = 0;

			/* allow NULL */
			if (tmp != NULL)
				tmpsz = strlen(tmp);

			/* sanity check */
			if (tmpsz > fmt_token->arraysz) {
				g_set_error(error,
					    G_IO_ERROR,
					    G_IO_ERROR_INVALID_DATA,
					    "string '%s' cannot fit in buffer of size %u",
					    tmp,
					    (guint)fmt_token->arraysz);
				return NULL;
			}

			/* copy string, and pad with NULs as required */
			for (guint j = 0; j < fmt_token->arraysz; j++) {
				if (j >= tmpsz) {
					fu_byte_array_append_uint8(buf, 0);
					continue;
				}
				fu_byte_array_append_uint8(buf, tmp[j]);
			}
			continue;
		}
		if (fmt_token->id == FU_STRUCT_FMT_TOKEN_ID_GUID) {
			fwupd_guid_t *tmp = va_arg(args, fwupd_guid_t *);
			g_byte_array_append(buf, (const guint8 *)tmp, sizeof(*tmp));
			continue;
		}
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid format token '%c'",
			    (gchar)fmt_token->id);
		return NULL;
	}

	/* success */
	return g_steal_pointer(&buf);
}

/**
 * fu_struct_pack:
 * @fmt: (not nullable): format string, e.g. `<BH20Q`
 * @error: (nullable): optional return location for an error
 * @...: variable number of values to pack into the struct
 *
 * Packs values into a structure defined by a format string.
 *
 * NOTE: Supplying the wrong type in the variable argument list will result in undefined behavior.
 *
 * Returns: (transfer full): a #GByteArray or %NULL on error
 *
 * Since: 1.8.13
 **/
GByteArray *
fu_struct_pack(const gchar *fmt, GError **error, ...)
{
	va_list args;
	g_autoptr(GByteArray) buf = NULL;

	g_return_val_if_fail(fmt != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	va_start(args, error);
	buf = fu_struct_pack_from_args(fmt, args, error);
	va_end(args);

	/* we set an error */
	if (buf == NULL)
		return NULL;

	/* success */
	return g_steal_pointer(&buf);
}

/**
 * fu_struct_pack_into:
 * @fmt: (not nullable): format string, e.g. `<BH20Q`
 * @error: (nullable): optional return location for an error
 * @buf: (not nullable): source buffer
 * @bufsz: maximum size of @buf, typically `sizeof(buf)`
 * @offset: offset in bytes into @buf to copy to
 * @...: variable number of values to pack into the struct
 *
 * Packs values into a raw buffer of a defined offset defined by a format string.
 *
 * Returns: a number of bytes, or %G_MAXSIZE on parsing error
 *
 * Since: 1.8.13
 **/
gboolean
fu_struct_pack_into(const gchar *fmt, GError **error, guint8 *buf, gsize bufsz, gsize offset, ...)
{
	va_list args;
	g_autoptr(GByteArray) buf2 = NULL;

	g_return_val_if_fail(fmt != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(bufsz != 0, FALSE);

	va_start(args, offset);
	buf2 = fu_struct_pack_from_args(fmt, args, error);
	va_end(args);
	if (buf2 == NULL)
		return FALSE;

	/* success */
	return fu_memcpy_safe(buf, bufsz, offset, buf2->data, buf2->len, 0x0, buf2->len, error);
}

static gboolean
fu_struct_unpack_from_args(GPtrArray *fmt_tokens,
			   const guint8 *buf,
			   gsize bufsz,
			   gsize *offset_ref,
			   va_list args,
			   GError **error)
{
	gsize offset = offset_ref != NULL ? *offset_ref : 0;
	for (guint i = i = 0; i < fmt_tokens->len; i++) {
		FuStructFmtToken *fmt_token = g_ptr_array_index(fmt_tokens, i);
		FuEndianType endian = fu_struct_fmt_token_get_endian(fmt_token);
		guint arraysz = fmt_token->arraysz == 0 ? 1 : fmt_token->arraysz;

		/* advance offset */
		if (fmt_token->flags & FU_STRUCT_FMT_TOKEN_FLAG_ADVANCE_OFFSET)
			*offset_ref += fu_struct_fmt_token_id_size(fmt_token);

		/* ids */
		if (fmt_token->id == FU_STRUCT_FMT_TOKEN_ID_PAD) {
			offset += arraysz;
			continue;
		}
		if (fmt_token->id == FU_STRUCT_FMT_TOKEN_ID_STRING) {
			gchar *tmp = va_arg(args, gchar *);
			for (guint j = 0; j < arraysz; j++) {
				if (!fu_memread_uint8_safe(buf,
							   bufsz,
							   offset,
							   (guint8 *)tmp + j,
							   error))
					return FALSE;
				offset += sizeof(*tmp);
			}
			continue;
		}
		if (fmt_token->id == FU_STRUCT_FMT_TOKEN_ID_GUID) {
			fwupd_guid_t *tmp = va_arg(args, fwupd_guid_t *);
			if (tmp != NULL) {
				if (!fu_memcpy_safe((guint8 *)tmp,
						    sizeof(*tmp),
						    0x0, /* dst */
						    buf,
						    bufsz,
						    offset, /* src */
						    sizeof(fwupd_guid_t),
						    error))
					return FALSE;
			}
			offset += sizeof(*tmp);
			continue;
		}
		if (fmt_token->id == FU_STRUCT_FMT_TOKEN_ID_UINT8) {
			guint8 *tmp = va_arg(args, guint8 *);
			if (tmp == NULL) {
				offset += arraysz * sizeof(*tmp);
				continue;
			}
			for (guint j = 0; j < arraysz; j++) {
				if (!fu_memread_uint8_safe(buf, bufsz, offset, tmp + j, error))
					return FALSE;
				offset += sizeof(*tmp);
			}
			continue;
		}
		if (fmt_token->id == FU_STRUCT_FMT_TOKEN_ID_UINT16) {
			guint16 *tmp = va_arg(args, guint16 *);
			if (tmp == NULL) {
				offset += arraysz * sizeof(*tmp);
				continue;
			}
			for (guint j = 0; j < arraysz; j++) {
				if (!fu_memread_uint16_safe(buf,
							    bufsz,
							    offset,
							    tmp + j,
							    endian,
							    error))
					return FALSE;
				g_debug("read %x", *(tmp + j));
				offset += sizeof(*tmp);
			}
			continue;
		}
		if (fmt_token->id == FU_STRUCT_FMT_TOKEN_ID_UINT24) {
			guint32 *tmp = va_arg(args, guint32 *);
			if (tmp == NULL) {
				offset += arraysz * sizeof(*tmp);
				continue;
			}
			for (guint j = 0; j < arraysz; j++) {
				if (!fu_memread_uint24_safe(buf,
							    bufsz,
							    offset,
							    tmp + j,
							    endian,
							    error))
					return FALSE;
				offset += 3;
			}
			continue;
		}
		if (fmt_token->id == FU_STRUCT_FMT_TOKEN_ID_UINT32) {
			guint32 *tmp = va_arg(args, guint32 *);
			if (tmp == NULL) {
				offset += arraysz * sizeof(*tmp);
				continue;
			}
			for (guint j = 0; j < arraysz; j++) {
				if (!fu_memread_uint32_safe(buf,
							    bufsz,
							    offset,
							    tmp + j,
							    endian,
							    error))
					return FALSE;
				offset += sizeof(*tmp);
			}
			continue;
		}
		if (fmt_token->id == FU_STRUCT_FMT_TOKEN_ID_UINT64) {
			guint64 *tmp = va_arg(args, guint64 *);
			if (tmp == NULL) {
				offset += arraysz * sizeof(*tmp);
				continue;
			}
			for (guint j = 0; j < arraysz; j++) {
				if (!fu_memread_uint64_safe(buf,
							    bufsz,
							    offset,
							    tmp + j,
							    endian,
							    error))
					return FALSE;
				offset += sizeof(*tmp);
			}
			continue;
		}
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid format token '%c'",
			    (gchar)fmt_token->id);
		return FALSE;
	}

	/* success */
	return TRUE;
}

/**
 * fu_struct_unpack:
 * @fmt: (not nullable): format string, e.g. `<BH20Q`
 * @error: (nullable): optional return location for an error
 * @buf: (not nullable): a source buffer
 * @...: variable number of values to unpack from the struct
 *
 * Unpacks values from a buffer specified by a format string.
 *
 * Returns: %TRUE if the struct could be parsed correctly.
 *
 * Since: 1.8.13
 **/
gboolean
fu_struct_unpack(const gchar *fmt, GError **error, GByteArray *buf, ...)
{
	gboolean ret;
	va_list args;
	g_autoptr(GPtrArray) fmt_tokens = NULL;

	g_return_val_if_fail(fmt != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	fmt_tokens = fu_struct_parse_fmt(fmt, error);
	if (fmt_tokens == NULL)
		return FALSE;

	va_start(args, buf);
	ret = fu_struct_unpack_from_args(fmt_tokens, buf->data, buf->len, NULL, args, error);
	va_end(args);
	return ret;
}

/**
 * fu_struct_unpack_from:
 * @fmt: (not nullable): format string, e.g. `<BH20Q`
 * @error: (nullable): optional return location for an error
 * @buf: (not nullable): source buffer
 * @bufsz: maximum size of @buf, typically `sizeof(buf)`
 * @offset: offset in bytes into @buf to copy from
 * @...: variable number of values to pack into the struct
 *
 * Unpacks values from a buffer specified by a format string.
 *
 * Returns: %TRUE if the struct could be parsed correctly.
 *
 * Since: 1.8.13
 **/
gboolean
fu_struct_unpack_from(const gchar *fmt,
		      GError **error,
		      const guint8 *buf,
		      gsize bufsz,
		      gsize *offset,
		      ...)
{
	gboolean ret;
	va_list args;
	gsize size;
	gsize offset_deref = offset != NULL ? *offset : 0;
	g_autoptr(GPtrArray) fmt_tokens = NULL;

	g_return_val_if_fail(fmt != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail(offset == NULL || *offset < bufsz, FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(bufsz != 0, FALSE);

	/* check we have enough data */
	fmt_tokens = fu_struct_parse_fmt(fmt, error);
	if (fmt_tokens == NULL)
		return FALSE;
	size = fu_struct_fmt_tokens_size(fmt_tokens);
	if (size > bufsz - offset_deref) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "buffer not at least 0x%x: bytes in size at offset 0x%x",
			    (guint)size,
			    (guint)offset_deref);
		return FALSE;
	}

	va_start(args, offset);
	ret = fu_struct_unpack_from_args(fmt_tokens, buf, bufsz, offset, args, error);
	va_end(args);
	return ret;
}
