/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-amd-afc-common.h"
#include "fu-amd-afc-config-entry.h"
#include "fu-amd-afc-config.h"
#include "fu-amd-afc-struct.h"

gboolean
fu_amd_afc_config_id(GByteArray *strings,
		     guint16 *count,
		     const gchar *value,
		     guint16 *id,
		     GError **error)
{
	gsize offset = 0;
	guint16 index = 0;
	while (offset < strings->len) {
		gsize length = strlen((const gchar *)strings->data + offset);
		if (g_strcmp0((const gchar *)strings->data + offset, value) == 0) {
			*id = index;
			return TRUE;
		}
		offset += length + 1;
		index++;
	}
	if (*count == G_MAXUINT16) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "too many AFC config strings");
		return FALSE;
	}
	*id = (*count)++;
	g_byte_array_append(strings, (const guint8 *)value, strlen(value) + 1);
	return TRUE;
}

void
fu_amd_afc_config_append_id(GByteArray *entries, guint16 value)
{
	g_autoptr(FuStructAmdAfcConfigId) st_id = fu_struct_amd_afc_config_id_new();
	fu_struct_amd_afc_config_id_set_value(st_id, value);
	g_byte_array_append(entries, st_id->buf->data, st_id->buf->len);
}

gboolean
fu_amd_afc_config_paths_equal(GPtrArray *path1, GPtrArray *path2)
{
	if (path1->len != path2->len)
		return FALSE;
	for (guint i = 0; i < path1->len; i++) {
		if (g_strcmp0(g_ptr_array_index(path1, i), g_ptr_array_index(path2, i)) != 0)
			return FALSE;
	}
	return TRUE;
}

GPtrArray *
fu_amd_afc_config_parse(GBytes *bytes, GError **error)
{
	g_autoptr(FuStructAmdAfcConfigHeader) st_hdr = NULL;
	g_autoptr(GPtrArray) entries =
	    g_ptr_array_new_with_free_func((GDestroyNotify)fu_amd_afc_config_entry_free);
	g_autoptr(GPtrArray) strings = g_ptr_array_new_with_free_func(g_free);
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(bytes, &bufsz);
	guint32 length;
	guint32 strings_size;
	guint16 entry_count;
	guint16 revision;
	gsize strings_end;
	gsize offset;

	st_hdr = fu_struct_amd_afc_config_header_parse(buf, bufsz, 0, error);
	if (st_hdr == NULL)
		return NULL;
	length = fu_struct_amd_afc_config_header_get_length(st_hdr);
	revision = fu_struct_amd_afc_config_header_get_revision(st_hdr);
	strings_size = fu_struct_amd_afc_config_header_get_strings_size(st_hdr);
	entry_count = fu_struct_amd_afc_config_header_get_entry_count(st_hdr);
	if (length != bufsz ||
	    (revision != FU_STRUCT_AMD_AFC_CONFIG_HEADER_DEFAULT_REVISION &&
	     (revision >> AFC_REVISION_MAJOR_SHIFT) != AFC_REVISION_MAJOR) ||
	    strings_size > bufsz - FU_STRUCT_AMD_AFC_CONFIG_HEADER_SIZE ||
	    fu_sum8(buf, bufsz) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid AFC config header");
		return NULL;
	}
	strings_end = FU_STRUCT_AMD_AFC_CONFIG_HEADER_SIZE + strings_size;
	offset = FU_STRUCT_AMD_AFC_CONFIG_HEADER_SIZE;
	while (offset < strings_end) {
		const guint8 *end = memchr(buf + offset, '\0', strings_end - offset);
		gsize string_size;
		if (end == NULL || end == buf + offset) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "invalid AFC config string table");
			return NULL;
		}
		string_size = end - (buf + offset);
		if (!g_utf8_validate((const gchar *)buf + offset, string_size, NULL)) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "invalid UTF-8 in AFC config string table");
			return NULL;
		}
		g_ptr_array_add(strings, g_strndup((const gchar *)buf + offset, string_size));
		offset += string_size + 1;
	}
	for (guint i = 0; i < entry_count; i++) {
		g_autoptr(GPtrArray) path = g_ptr_array_new_with_free_func(g_free);
		FuAmdAfcConfigEntry *entry;
		guint16 token;
		guint path_length;

		if (!fu_memread_uint16_safe(buf, bufsz, offset, &token, G_LITTLE_ENDIAN, error))
			return NULL;
		offset += sizeof(token);
		if ((token & AFC_CONFIG_STRING_TOKEN) == 0 ||
		    (token & ~(AFC_CONFIG_STRING_TOKEN | AFC_CONFIG_PATH_MASK)) != 0 ||
		    (token & AFC_CONFIG_PATH_MASK) == 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "invalid AFC config entry token");
			return NULL;
		}
		path_length = token & AFC_CONFIG_PATH_MASK;
		for (guint j = 0; j <= path_length; j++) {
			const gchar *value;
			guint16 id;
			if (!fu_memread_uint16_safe(buf,
						    bufsz,
						    offset,
						    &id,
						    G_LITTLE_ENDIAN,
						    error))
				return NULL;
			offset += sizeof(id);
			if (id >= strings->len) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_DATA,
						    "invalid AFC config string ID");
				return NULL;
			}
			value = g_ptr_array_index(strings, id);
			if (j < path_length) {
				g_ptr_array_add(path, g_strdup(value));
				continue;
			}
			entry = g_new0(FuAmdAfcConfigEntry, 1);
			entry->path = g_steal_pointer(&path);
			entry->value = g_strdup(value);
		}
		for (guint j = 0; j < entries->len; j++) {
			FuAmdAfcConfigEntry *entry_old = g_ptr_array_index(entries, j);
			if (fu_amd_afc_config_paths_equal(entry_old->path, entry->path)) {
				fu_amd_afc_config_entry_free(entry);
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_DATA,
						    "duplicate AFC config entry");
				return NULL;
			}
		}
		g_ptr_array_add(entries, entry);
	}
	if (offset != bufsz) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "trailing data in AFC config");
		return NULL;
	}
	return g_steal_pointer(&entries);
}
