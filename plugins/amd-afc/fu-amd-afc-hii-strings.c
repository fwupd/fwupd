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

	while (*offset < bufsz) {
		guint16 ch = 0;
		if (!fu_memread_uint16_safe(buf, bufsz, *offset, &ch, G_LITTLE_ENDIAN, error))
			return FALSE;
		*offset += 2;
		if (ch != 0)
			continue;
		{
			g_autoptr(GBytes) utf16 =
			    g_bytes_new(buf + offset_start, *offset - offset_start - 2);
			*value = fu_utf16_to_utf8_bytes(utf16, G_LITTLE_ENDIAN, error);
		}
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
		guint8 block_type = 0;
		guint16 count = 1;
		if (!fu_memread_uint8_safe(buf, bufsz, offset++, &block_type, error))
			return FALSE;
		if (block_type == FU_AMD_AFC_HII_STRING_BLOCK_KIND_END)
			return TRUE;
		if (block_type == FU_AMD_AFC_HII_STRING_BLOCK_KIND_STRING_SCSU_FONT ||
		    block_type == FU_AMD_AFC_HII_STRING_BLOCK_KIND_STRING_UCS2_FONT ||
		    block_type == FU_AMD_AFC_HII_STRING_BLOCK_KIND_STRINGS_SCSU_FONT ||
		    block_type == FU_AMD_AFC_HII_STRING_BLOCK_KIND_STRINGS_UCS2_FONT) {
			guint8 font_id = 0;
			if (!fu_memread_uint8_safe(buf, bufsz, offset, &font_id, error))
				return FALSE;
			offset++;
		}
		if (block_type == FU_AMD_AFC_HII_STRING_BLOCK_KIND_STRINGS_SCSU ||
		    block_type == FU_AMD_AFC_HII_STRING_BLOCK_KIND_STRINGS_SCSU_FONT ||
		    block_type == FU_AMD_AFC_HII_STRING_BLOCK_KIND_STRINGS_UCS2 ||
		    block_type == FU_AMD_AFC_HII_STRING_BLOCK_KIND_STRINGS_UCS2_FONT) {
			if (!fu_memread_uint16_safe(buf,
						    bufsz,
						    offset,
						    &count,
						    G_LITTLE_ENDIAN,
						    error))
				return FALSE;
			offset += 2;
		}
		if (block_type >= FU_AMD_AFC_HII_STRING_BLOCK_KIND_STRING_SCSU &&
		    block_type <= FU_AMD_AFC_HII_STRING_BLOCK_KIND_STRINGS_SCSU_FONT) {
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
		if (block_type >= FU_AMD_AFC_HII_STRING_BLOCK_KIND_STRING_UCS2 &&
		    block_type <= FU_AMD_AFC_HII_STRING_BLOCK_KIND_STRINGS_UCS2_FONT) {
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
		if (block_type == FU_AMD_AFC_HII_STRING_BLOCK_KIND_DUPLICATE) {
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
		if (block_type == FU_AMD_AFC_HII_STRING_BLOCK_KIND_SKIP1 ||
		    block_type == FU_AMD_AFC_HII_STRING_BLOCK_KIND_SKIP2) {
			guint16 skip = 0;
			if (block_type == FU_AMD_AFC_HII_STRING_BLOCK_KIND_SKIP1) {
				guint8 skip8 = 0;
				if (!fu_memread_uint8_safe(buf, bufsz, offset, &skip8, error))
					return FALSE;
				skip = skip8;
				offset++;
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
		if (block_type == FU_AMD_AFC_HII_STRING_BLOCK_KIND_EXT1 ||
		    block_type == FU_AMD_AFC_HII_STRING_BLOCK_KIND_EXT2 ||
		    block_type == FU_AMD_AFC_HII_STRING_BLOCK_KIND_EXT4 ||
		    block_type == FU_AMD_AFC_HII_STRING_BLOCK_KIND_FONT) {
			guint32 length = 0;
			guint header_len = 4;
			if (block_type == FU_AMD_AFC_HII_STRING_BLOCK_KIND_EXT1)
				header_len = 3;
			else if (block_type == FU_AMD_AFC_HII_STRING_BLOCK_KIND_EXT4)
				header_len = 6;
			if (block_type == FU_AMD_AFC_HII_STRING_BLOCK_KIND_EXT1) {
				guint8 length8 = 0;
				if (!fu_memread_uint8_safe(buf, bufsz, offset + 1, &length8, error))
					return FALSE;
				length = length8;
			} else if (block_type == FU_AMD_AFC_HII_STRING_BLOCK_KIND_EXT4) {
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
			if (length < header_len) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_DATA,
						    "invalid AFC extended string block length");
				return FALSE;
			}
			if (!fu_amd_afc_hii_bounds(bufsz, offset, length - 1, error))
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
