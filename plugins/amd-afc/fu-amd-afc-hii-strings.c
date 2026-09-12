/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * nocheck:magic-defines=20 -- constants mirror the UEFI HII string format
 */

#include "config.h"

#include "fu-amd-afc-common.h"
#include "fu-amd-afc-struct.h"

#define HII_PACKAGE_STRINGS	      0x04U
#define HII_STRING_HEADER_SIZE_OFFSET 4U
#define HII_STRING_LANGUAGE_OFFSET    46U
#define SIBT_END		      0x00U
#define SIBT_STRING_SCSU	      0x10U
#define SIBT_STRING_SCSU_FONT	      0x11U
#define SIBT_STRINGS_SCSU	      0x12U
#define SIBT_STRINGS_SCSU_FONT	      0x13U
#define SIBT_STRING_UCS2	      0x14U
#define SIBT_STRING_UCS2_FONT	      0x15U
#define SIBT_STRINGS_UCS2	      0x16U
#define SIBT_STRINGS_UCS2_FONT	      0x17U
#define SIBT_DUPLICATE		      0x20U
#define SIBT_SKIP2		      0x21U
#define SIBT_SKIP1		      0x22U
#define SIBT_EXT1		      0x30U
#define SIBT_EXT2		      0x31U
#define SIBT_EXT4		      0x32U
#define SIBT_FONT		      0x40U
static gboolean
fu_amd_afc_hii_strings_store(FuAmdAfcState *self, guint id, gchar *value, GError **error)
{
	if (id > G_MAXUINT16) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "too many AFC strings");
		g_free(value);
		return FALSE;
	}
	while (self->strings->len <= id)
		g_ptr_array_add(self->strings, NULL);
	g_free(g_ptr_array_index(self->strings, id));
	g_ptr_array_index(self->strings, id) = value;
	return TRUE;
}

const gchar *
fu_amd_afc_hii_strings_get_string(FuAmdAfcState *self, guint16 id, GError **error)
{
	const gchar *value = id < self->strings->len ? g_ptr_array_index(self->strings, id) : NULL;
	if (value == NULL)
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "AFC string %u was not found",
			    id);
	return value;
}

static gboolean
fu_amd_afc_hii_strings_parse_ascii(FuAmdAfcState *self,
				   const guint8 *buf,
				   gsize bufsz,
				   gsize *offset,
				   guint16 count,
				   guint *id,
				   GError **error)
{
	for (guint i = 0; i < count; i++) {
		g_autoptr(GString) value = fu_memread_string_safe(buf, bufsz, *offset, error);
		gsize value_len;
		if (value == NULL)
			return FALSE;
		value_len = value->len;
		for (gsize j = 0; j < value->len; j++) {
			if ((((guint8)value->str[j]) & 0x80) != 0) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_NOT_SUPPORTED,
						    "compressed SCSU strings are unsupported");
				return FALSE;
			}
		}
		if (!fu_amd_afc_hii_strings_store(self,
						  *id,
						  g_string_free(g_steal_pointer(&value), FALSE),
						  error))
			return FALSE;
		(*id)++;
		*offset += value_len + 1;
	}
	return TRUE;
}

static gboolean
fu_amd_afc_hii_strings_parse_ucs2(const guint8 *buf,
				  gsize bufsz,
				  gsize *offset,
				  gchar **value,
				  GError **error)
{
	gsize offset_start = *offset;
	GByteArray utf16 = {0x0};

	while (*offset < bufsz) {
		guint16 ch = 0;
		if (!fu_memread_uint16_safe(buf, bufsz, *offset, &ch, G_LITTLE_ENDIAN, error))
			return FALSE;
		*offset += 2;
		if (ch != 0)
			continue;
		utf16.data = (guint8 *)buf + offset_start;
		utf16.len = *offset - offset_start - 2;
		*value = fu_utf16_to_utf8_byte_array(&utf16, G_LITTLE_ENDIAN, error);
		return *value != NULL;
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "unterminated AFC UCS-2 string");
	return FALSE;
}

static gboolean
fu_amd_afc_hii_strings_parse_ucs2_multiple(FuAmdAfcState *self,
					   const guint8 *buf,
					   gsize bufsz,
					   gsize *offset,
					   guint16 count,
					   guint *id,
					   GError **error)
{
	for (guint i = 0; i < count; i++) {
		gchar *value = NULL;
		if (!fu_amd_afc_hii_strings_parse_ucs2(buf, bufsz, offset, &value, error))
			return FALSE;
		if (!fu_amd_afc_hii_strings_store(self, *id, value, error))
			return FALSE;
		(*id)++;
	}
	return TRUE;
}

gboolean
fu_amd_afc_hii_strings_parse_package(FuAmdAfcState *self,
				     const guint8 *buf,
				     gsize bufsz,
				     GError **error)
{
	guint8 package_type = 0;
	guint32 package_length = 0;
	guint32 header_size = 0;
	g_autoptr(FuStructAmdAfcHiiPackageHeader) st_hdr = NULL;
	g_autoptr(GString) language = NULL;
	gsize offset;
	guint id = 1;

	if (bufsz <= HII_STRING_LANGUAGE_OFFSET) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "AFC string package is too small");
		return FALSE;
	}
	st_hdr = fu_struct_amd_afc_hii_package_header_parse(buf, bufsz, 0, error);
	if (st_hdr == NULL)
		return FALSE;
	package_type = fu_struct_amd_afc_hii_package_header_get_kind(st_hdr);
	package_length = fu_struct_amd_afc_hii_package_header_get_length(st_hdr);
	if (package_type != HII_PACKAGE_STRINGS || package_length != bufsz ||
	    !fu_memread_uint32_safe(buf,
				    bufsz,
				    HII_STRING_HEADER_SIZE_OFFSET,
				    &header_size,
				    G_LITTLE_ENDIAN,
				    error)) {
		if (error == NULL || *error == NULL)
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "invalid AFC string package");
		return FALSE;
	}
	if (header_size <= HII_STRING_LANGUAGE_OFFSET || header_size >= bufsz) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid AFC string header size");
		return FALSE;
	}
	language = fu_memread_string_safe(buf, header_size, HII_STRING_LANGUAGE_OFFSET, error);
	if (language == NULL)
		return FALSE;
	g_free(self->language);
	self->language = g_string_free(g_steal_pointer(&language), FALSE);
	if (g_strcmp0(self->language, "en") != 0 && g_strcmp0(self->language, "en-US") != 0 &&
	    g_strcmp0(self->language, "eng") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "AFC language %s is unsupported",
			    self->language);
		return FALSE;
	}
	offset = header_size;
	while (offset < bufsz) {
		guint8 block_type = buf[offset++];
		guint16 count = 1;
		if (block_type == SIBT_END)
			return TRUE;
		if (block_type == SIBT_STRING_SCSU_FONT || block_type == SIBT_STRING_UCS2_FONT ||
		    block_type == SIBT_STRINGS_SCSU_FONT || block_type == SIBT_STRINGS_UCS2_FONT) {
			if (!fu_amd_afc_hii_bounds(bufsz, offset, 1, error))
				return FALSE;
			offset++;
		}
		if (block_type == SIBT_STRINGS_SCSU || block_type == SIBT_STRINGS_SCSU_FONT ||
		    block_type == SIBT_STRINGS_UCS2 || block_type == SIBT_STRINGS_UCS2_FONT) {
			if (!fu_memread_uint16_safe(buf,
						    bufsz,
						    offset,
						    &count,
						    G_LITTLE_ENDIAN,
						    error))
				return FALSE;
			offset += 2;
		}
		if (block_type >= SIBT_STRING_SCSU && block_type <= SIBT_STRINGS_SCSU_FONT) {
			if (!fu_amd_afc_hii_strings_parse_ascii(self,
								buf,
								bufsz,
								&offset,
								count,
								&id,
								error))
				return FALSE;
			continue;
		}
		if (block_type >= SIBT_STRING_UCS2 && block_type <= SIBT_STRINGS_UCS2_FONT) {
			if (!fu_amd_afc_hii_strings_parse_ucs2_multiple(self,
									buf,
									bufsz,
									&offset,
									count,
									&id,
									error))
				return FALSE;
			continue;
		}
		if (block_type == SIBT_DUPLICATE) {
			guint16 source = 0;
			const gchar *value;
			if (!fu_memread_uint16_safe(buf,
						    bufsz,
						    offset,
						    &source,
						    G_LITTLE_ENDIAN,
						    error))
				return FALSE;
			offset += 2;
			value = fu_amd_afc_hii_strings_get_string(self, source, error);
			if (value == NULL ||
			    !fu_amd_afc_hii_strings_store(self, id++, g_strdup(value), error))
				return FALSE;
			continue;
		}
		if (block_type == SIBT_SKIP1 || block_type == SIBT_SKIP2) {
			guint16 skip = 0;
			if (block_type == SIBT_SKIP1) {
				if (!fu_amd_afc_hii_bounds(bufsz, offset, 1, error))
					return FALSE;
				skip = buf[offset++];
			} else {
				if (!fu_memread_uint16_safe(buf,
							    bufsz,
							    offset,
							    &skip,
							    G_LITTLE_ENDIAN,
							    error))
					return FALSE;
				offset += 2;
			}
			if (id > (guint)G_MAXUINT16 - skip) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_DATA,
						    "too many AFC strings");
				return FALSE;
			}
			id += skip;
			continue;
		}
		if (block_type == SIBT_EXT1 || block_type == SIBT_EXT2 || block_type == SIBT_EXT4 ||
		    block_type == SIBT_FONT) {
			guint32 length = 0;
			guint header_len =
			    block_type == SIBT_EXT1 ? 3 : (block_type == SIBT_EXT4 ? 6 : 4);
			if (block_type == SIBT_EXT1) {
				if (!fu_amd_afc_hii_bounds(bufsz, offset, 2, error))
					return FALSE;
				length = buf[offset + 1];
			} else if (block_type == SIBT_EXT4) {
				if (!fu_memread_uint32_safe(buf,
							    bufsz,
							    offset + 1,
							    &length,
							    G_LITTLE_ENDIAN,
							    error))
					return FALSE;
			} else {
				guint16 length16 = 0;
				if (!fu_memread_uint16_safe(buf,
							    bufsz,
							    offset + 1,
							    &length16,
							    G_LITTLE_ENDIAN,
							    error))
					return FALSE;
				length = length16;
			}
			if (length < header_len ||
			    !fu_amd_afc_hii_bounds(bufsz, offset, length - 1, error))
				return FALSE;
			offset += length - 1;
			continue;
		}
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unsupported AFC string block 0x%02x",
			    block_type);
		return FALSE;
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "AFC string package has no terminator");
	return FALSE;
}
