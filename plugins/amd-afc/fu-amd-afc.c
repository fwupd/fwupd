/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * nocheck:magic-defines=20 -- constants mirror the AFC format
 */

#include "config.h"

#include "fu-amd-afc-common.h"
#include "fu-amd-afc-struct.h"

#define AFC_REVISION_MAJOR	 1U
#define AFC_REVISION_MAJOR_SHIFT 12U

#define AFC_CONFIG_STRING_TOKEN (1U << 8)
#define AFC_CONFIG_PATH_MASK	(AFC_CONFIG_STRING_TOKEN - 1U)

#define AFC_EFIVAR_GUID "f5c0066d-dd67-4186-bcca-55d7e73ecd56"
#define AFC_EFIVAR_NAME "AmdFwConfig"
#define AFC_EFIVAR_ATTRS                                                                           \
	(FU_EFI_VARIABLE_ATTR_NON_VOLATILE | FU_EFI_VARIABLE_ATTR_BOOTSERVICE_ACCESS |             \
	 FU_EFI_VARIABLE_ATTR_RUNTIME_ACCESS)
struct _FuAmdAfcBiosSetting {
	FwupdBiosSetting parent_instance;
	FuAmdAfcState *state;
	guint index;
};

G_DEFINE_TYPE(FuAmdAfcBiosSetting, fu_amd_afc_bios_setting, FWUPD_TYPE_BIOS_SETTING)

static void
fu_amd_afc_pending_free(FuAmdAfcPending *pending)
{
	g_free(pending->value);
	g_free(pending);
}

static void
fu_amd_afc_config_entry_free(FuAmdAfcConfigEntry *entry)
{
	g_ptr_array_unref(entry->path);
	g_free(entry->value);
	g_free(entry);
}
static gboolean
fu_amd_afc_parse_blob(FuAmdAfcState *self, const guint8 *buf, gsize bufsz, GError **error)
{
	g_autoptr(FuStructAmdAfcEieHeader) st_hdr = NULL;
	guint32 length;
	guint16 revision;
	guint32 forms_offset;
	guint32 forms_size;
	guint32 strings_offset;
	guint32 strings_size;
	guint32 varstores_offset;
	guint32 varstores_size;
	gsize payload_size;
	g_autoptr(GPtrArray) path = g_ptr_array_new_with_free_func(g_free);

	st_hdr = fu_struct_amd_afc_eie_header_parse(buf, bufsz, 0, error);
	if (st_hdr == NULL)
		return FALSE;
	length = fu_struct_amd_afc_eie_header_get_length(st_hdr);
	revision = fu_struct_amd_afc_eie_header_get_revision(st_hdr);
	forms_offset = fu_struct_amd_afc_eie_header_get_forms_offset(st_hdr);
	forms_size = fu_struct_amd_afc_eie_header_get_forms_size(st_hdr);
	strings_offset = fu_struct_amd_afc_eie_header_get_strings_offset(st_hdr);
	strings_size = fu_struct_amd_afc_eie_header_get_strings_size(st_hdr);
	varstores_offset = fu_struct_amd_afc_eie_header_get_varstores_offset(st_hdr);
	varstores_size = fu_struct_amd_afc_eie_header_get_varstores_size(st_hdr);
	if (length < FU_STRUCT_AMD_AFC_EIE_HEADER_SIZE || length > bufsz ||
	    (revision != FU_STRUCT_AMD_AFC_CONFIG_HEADER_DEFAULT_REVISION &&
	     (revision >> AFC_REVISION_MAJOR_SHIFT) != AFC_REVISION_MAJOR)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid AFC blob header");
		return FALSE;
	}
	if (fu_sum8(buf, length) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid AFC blob checksum");
		return FALSE;
	}
	if (self->revision != 0 && self->revision != revision) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "mixed AFC revisions");
		return FALSE;
	}
	self->revision = revision;
	g_ptr_array_set_size(self->strings, 0);
	g_ptr_array_set_size(self->forms, 0);
	g_ptr_array_set_size(self->varstores, 0);
	g_free(self->formset_name);
	self->formset_name = NULL;
	g_free(self->language);
	self->language = NULL;
	payload_size = length - FU_STRUCT_AMD_AFC_EIE_HEADER_SIZE;
	if (forms_offset > payload_size || forms_size > payload_size - forms_offset ||
	    strings_offset > payload_size || strings_size > payload_size - strings_offset ||
	    varstores_offset > payload_size || varstores_size > payload_size - varstores_offset) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid AFC section bounds");
		return FALSE;
	}
	if (!fu_amd_afc_hii_strings_parse_package(self,
						  buf + FU_STRUCT_AMD_AFC_EIE_HEADER_SIZE +
						      strings_offset,
						  strings_size,
						  error) ||
	    !fu_amd_afc_hii_parse_varstores(self,
					    buf + FU_STRUCT_AMD_AFC_EIE_HEADER_SIZE +
						varstores_offset,
					    varstores_size,
					    error) ||
	    !fu_amd_afc_hii_parse_forms(self,
					buf + FU_STRUCT_AMD_AFC_EIE_HEADER_SIZE + forms_offset,
					forms_size,
					error))
		return FALSE;
	if (self->forms->len == 0 || self->formset_name == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "AFC blob contains no forms");
		return FALSE;
	}
	g_ptr_array_add(path, g_strdup(self->formset_name));
	for (guint i = 0; i < self->forms->len; i++) {
		if (!fu_amd_afc_hii_assign_form_paths(self, i, path, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_amd_afc_state_parse_payload(FuAmdAfcState *self, GBytes *bytes, GError **error)
{
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(bytes, &bufsz);
	gsize offset = 0;
	guint parsed = 0;
	while (bufsz - offset >= FU_STRUCT_AMD_AFC_EIE_HEADER_SIZE) {
		guint32 signature = 0;
		guint32 length;
		g_autoptr(FuStructAmdAfcEieHeader) st_hdr = NULL;
		if (!fu_memread_uint32_safe(buf, bufsz, offset, &signature, G_LITTLE_ENDIAN, error))
			return FALSE;
		if (signature != FU_STRUCT_AMD_AFC_EIE_HEADER_DEFAULT_SIGNATURE) {
			offset++;
			continue;
		}
		st_hdr = fu_struct_amd_afc_eie_header_parse(buf, bufsz, offset, error);
		if (st_hdr == NULL ||
		    !fu_amd_afc_parse_blob(self, buf + offset, bufsz - offset, error))
			return FALSE;
		length = fu_struct_amd_afc_eie_header_get_length(st_hdr);
		if (length > bufsz - offset) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "AFC blob exceeds ACPI table");
			return FALSE;
		}
		offset += length;
		parsed++;
	}
	if (parsed == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "no AFC data found in ACPI table");
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_amd_afc_state_parse_table(FuAmdAfcState *self, GBytes *bytes, GError **error)
{
	g_autoptr(FuFirmware) table = fu_acpi_table_new();
	g_autoptr(FuInputStream) payload_stream = NULL;
	g_autoptr(GBytes) payload = NULL;

	g_return_val_if_fail(self != NULL, FALSE);
	g_return_val_if_fail(bytes != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fu_firmware_parse_bytes(table, bytes, 0, FU_FIRMWARE_PARSE_FLAG_CACHE_STREAM, error))
		return FALSE;
	if (g_strcmp0(fu_firmware_get_id(table), "SSDT") != 0 ||
	    g_strcmp0(fu_acpi_table_get_oem_table_id(FU_ACPI_TABLE(table)), "AmdFwCfg") != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "ACPI table does not contain AFC data");
		return FALSE;
	}
	payload_stream = fu_acpi_table_get_payload(FU_ACPI_TABLE(table), error);
	if (payload_stream == NULL)
		return FALSE;
	payload = fu_input_stream_read_bytes(payload_stream, 0, G_MAXSIZE, NULL, error);
	if (payload == NULL)
		return FALSE;
	return fu_amd_afc_state_parse_payload(self, payload, error);
}

static const gchar *
fu_amd_afc_option_name(FuAmdAfcSetting *setting, guint64 value)
{
	for (guint i = 0; i < setting->options->len; i++) {
		FuAmdAfcOption *option = g_ptr_array_index(setting->options, i);
		if (option->value == value)
			return option->name;
	}
	return NULL;
}

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

static void
fu_amd_afc_config_append_id(GByteArray *entries, guint16 value)
{
	g_autoptr(FuStructAmdAfcConfigId) st_id = fu_struct_amd_afc_config_id_new();
	fu_struct_amd_afc_config_id_set_value(st_id, value);
	g_byte_array_append(entries, st_id->buf->data, st_id->buf->len);
}

static gboolean
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
	gsize strings_end;
	gsize offset;

	st_hdr = fu_struct_amd_afc_config_header_parse(buf, bufsz, 0, error);
	if (st_hdr == NULL)
		return NULL;
	length = fu_struct_amd_afc_config_header_get_length(st_hdr);
	strings_size = fu_struct_amd_afc_config_header_get_strings_size(st_hdr);
	entry_count = fu_struct_amd_afc_config_header_get_entry_count(st_hdr);
	if (length != bufsz ||
	    fu_struct_amd_afc_config_header_get_revision(st_hdr) !=
		FU_STRUCT_AMD_AFC_CONFIG_HEADER_DEFAULT_REVISION ||
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

gboolean
fu_amd_afc_config_validate(GBytes *bytes, GError **error)
{
	g_autoptr(GPtrArray) entries = NULL;

	g_return_val_if_fail(bytes != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	entries = fu_amd_afc_config_parse(bytes, error);
	return entries != NULL;
}

static gboolean
fu_amd_afc_write_pending(FuAmdAfcState *self, GError **error)
{
	g_autoptr(GByteArray) strings = g_byte_array_new();
	g_autoptr(GByteArray) entries_buf = g_byte_array_new();
	g_autoptr(GPtrArray) entries = NULL;
	g_autoptr(FuStructAmdAfcConfigHeader) st_hdr = fu_struct_amd_afc_config_header_new();
	g_autoptr(GBytes) existing = NULL;
	g_autoptr(GBytes) verify = NULL;
	g_autoptr(GBytes) expected = NULL;
	FuEfiVariableAttrs attrs = 0;
	guint16 string_count = 0;

	if (fu_efivars_exists(self->efivars, AFC_EFIVAR_GUID, AFC_EFIVAR_NAME)) {
		existing = fu_efivars_get_data_bytes(self->efivars,
						     AFC_EFIVAR_GUID,
						     AFC_EFIVAR_NAME,
						     &attrs,
						     error);
		if (existing == NULL)
			return FALSE;
		if (attrs != AFC_EFIVAR_ATTRS) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "invalid AFC EFI variable attributes");
			return FALSE;
		}
		entries = fu_amd_afc_config_parse(existing, error);
		if (entries == NULL)
			return FALSE;
	} else {
		entries =
		    g_ptr_array_new_with_free_func((GDestroyNotify)fu_amd_afc_config_entry_free);
	}
	for (guint i = 0; i < self->pending->len; i++) {
		FuAmdAfcPending *pending = g_ptr_array_index(self->pending, i);
		FuAmdAfcSetting *setting = g_ptr_array_index(self->settings, pending->setting);
		FuAmdAfcConfigEntry *entry = NULL;
		for (guint j = 0; j < entries->len; j++) {
			FuAmdAfcConfigEntry *entry_tmp = g_ptr_array_index(entries, j);
			if (fu_amd_afc_config_paths_equal(entry_tmp->path, setting->path)) {
				entry = entry_tmp;
				break;
			}
		}
		if (entry == NULL) {
			entry = g_new0(FuAmdAfcConfigEntry, 1);
			entry->path = g_ptr_array_new_with_free_func(g_free);
			for (guint j = 0; j < setting->path->len; j++)
				g_ptr_array_add(entry->path,
						g_strdup(g_ptr_array_index(setting->path, j)));
			g_ptr_array_add(entries, entry);
		}
		g_free(entry->value);
		entry->value = g_strdup(pending->value);
	}
	if (entries->len > G_MAXUINT16) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "too many pending AFC settings");
		return FALSE;
	}
	for (guint i = 0; i < entries->len; i++) {
		FuAmdAfcConfigEntry *entry = g_ptr_array_index(entries, i);
		g_autofree guint16 *path_ids = NULL;
		guint16 id;
		if (entry->path->len == 0 || entry->path->len > AFC_CONFIG_PATH_MASK) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "invalid AFC config path length");
			return FALSE;
		}
		fu_amd_afc_config_append_id(entries_buf,
					    entry->path->len | AFC_CONFIG_STRING_TOKEN);
		path_ids = g_new(guint16, entry->path->len);
		for (guint j = entry->path->len; j > 0; j--) {
			if (!fu_amd_afc_config_id(strings,
						  &string_count,
						  g_ptr_array_index(entry->path, j - 1),
						  &id,
						  error))
				return FALSE;
			path_ids[j - 1] = id;
		}
		for (guint j = 0; j < entry->path->len; j++)
			fu_amd_afc_config_append_id(entries_buf, path_ids[j]);
		if (!fu_amd_afc_config_id(strings, &string_count, entry->value, &id, error))
			return FALSE;
		fu_amd_afc_config_append_id(entries_buf, id);
	}
	if (strings->len > G_MAXUINT32 - st_hdr->buf->len ||
	    entries_buf->len > G_MAXUINT32 - st_hdr->buf->len - strings->len) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "AFC config is too large");
		return FALSE;
	}
	fu_struct_amd_afc_config_header_set_length(st_hdr,
						   st_hdr->buf->len + strings->len +
						       entries_buf->len);
	fu_struct_amd_afc_config_header_set_strings_size(st_hdr, strings->len);
	fu_struct_amd_afc_config_header_set_entry_count(st_hdr, entries->len);
	g_byte_array_append(st_hdr->buf, strings->data, strings->len);
	g_byte_array_append(st_hdr->buf, entries_buf->data, entries_buf->len);
	fu_struct_amd_afc_config_header_set_checksum(
	    st_hdr,
	    (guint8)(0U - fu_sum8(st_hdr->buf->data, st_hdr->buf->len)));
	if (!fu_efivars_set_data(self->efivars,
				 AFC_EFIVAR_GUID,
				 AFC_EFIVAR_NAME,
				 st_hdr->buf->data,
				 st_hdr->buf->len,
				 AFC_EFIVAR_ATTRS,
				 error))
		return FALSE;
	verify = fu_efivars_get_data_bytes(self->efivars,
					   AFC_EFIVAR_GUID,
					   AFC_EFIVAR_NAME,
					   &attrs,
					   error);
	if (verify == NULL)
		return FALSE;
	expected = g_byte_array_free_to_bytes(g_steal_pointer(&st_hdr->buf));
	if (attrs != AFC_EFIVAR_ATTRS || !g_bytes_equal(verify, expected)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "failed to verify AFC EFI variable");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_amd_afc_state_store(FuAmdAfcState *self, guint index, const gchar *value, GError **error)
{
	FuAmdAfcSetting *setting;
	g_autofree gchar *encoded = NULL;
	gint existing = -1;
	g_return_val_if_fail(index < self->settings->len, FALSE);
	setting = g_ptr_array_index(self->settings, index);
	if (setting->kind == AFC_SETTING_ENUMERATION) {
		gboolean found = FALSE;
		for (guint i = 0; i < setting->options->len; i++) {
			FuAmdAfcOption *option = g_ptr_array_index(setting->options, i);
			if (g_strcmp0(option->name, value) == 0) {
				found = TRUE;
				break;
			}
		}
		if (!found) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "%s is not a valid AFC option",
				    value);
			return FALSE;
		}
		encoded = g_strdup(value);
	} else {
		guint64 number = 0;
		if (!fu_strtoull(value, &number, 0, G_MAXUINT64, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		if (number < setting->minimum || number > setting->maximum ||
		    (number - setting->minimum) % setting->step != 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "%s is outside the AFC range",
				    value);
			return FALSE;
		}
		encoded = g_strdup_printf("0x%" G_GINT64_MODIFIER "x", number);
	}
	for (guint i = 0; i < self->pending->len; i++) {
		FuAmdAfcPending *pending = g_ptr_array_index(self->pending, i);
		if (pending->setting == index) {
			existing = i;
			break;
		}
	}
	if (existing >= 0) {
		FuAmdAfcPending *pending = g_ptr_array_index(self->pending, existing);
		gchar *old = pending->value;
		pending->value = g_steal_pointer(&encoded);
		if (!fu_amd_afc_write_pending(self, error)) {
			g_free(pending->value);
			pending->value = old;
			return FALSE;
		}
		g_free(old);
	} else {
		FuAmdAfcPending *pending = g_new0(FuAmdAfcPending, 1);
		pending->setting = index;
		pending->value = g_steal_pointer(&encoded);
		g_ptr_array_add(self->pending, pending);
		if (!fu_amd_afc_write_pending(self, error)) {
			g_ptr_array_remove_index(self->pending, self->pending->len - 1);
			return FALSE;
		}
	}
	if (self->bios_settings != NULL)
		fu_bios_settings_set_pending_reboot(self->bios_settings, TRUE);
	return TRUE;
}

static gchar *
fu_amd_afc_bios_setting_read_value(FwupdBiosSetting *bios_setting, GError **error)
{
	FuAmdAfcBiosSetting *self = FU_AMD_AFC_BIOS_SETTING(bios_setting);
	FuAmdAfcSetting *setting;
	const gchar *name;
	if (self->index >= self->state->settings->len) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "invalid AFC setting index");
		return NULL;
	}
	setting = g_ptr_array_index(self->state->settings, self->index);
	if (!setting->has_current) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "AFC current value is unavailable");
		return NULL;
	}
	if (setting->kind == AFC_SETTING_INTEGER)
		return g_strdup_printf("%" G_GUINT64_FORMAT, setting->current);
	name = fu_amd_afc_option_name(setting, setting->current);
	if (name == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "AFC value has no matching option");
		return NULL;
	}
	return g_strdup(name);
}

static gboolean
fu_amd_afc_bios_setting_write_value(FwupdBiosSetting *bios_setting,
				    const gchar *value,
				    GError **error)
{
	FuAmdAfcBiosSetting *self = FU_AMD_AFC_BIOS_SETTING(bios_setting);
	return fu_amd_afc_state_store(self->state, self->index, value, error);
}

static void
fu_amd_afc_bios_setting_finalize(GObject *object)
{
	FuAmdAfcBiosSetting *self = FU_AMD_AFC_BIOS_SETTING(object);
	if (self->state != NULL)
		fu_amd_afc_state_unref(self->state);
	G_OBJECT_CLASS(fu_amd_afc_bios_setting_parent_class)->finalize(object);
}

static void
fu_amd_afc_bios_setting_class_init(FuAmdAfcBiosSettingClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FwupdBiosSettingClass *bios_setting_class = FWUPD_BIOS_SETTING_CLASS(klass);
	object_class->finalize = fu_amd_afc_bios_setting_finalize;
	bios_setting_class->read_value = fu_amd_afc_bios_setting_read_value;
	bios_setting_class->write_value = fu_amd_afc_bios_setting_write_value;
}

static void
fu_amd_afc_bios_setting_init(FuAmdAfcBiosSetting *self)
{
}

FuAmdAfcState *
fu_amd_afc_state_new(FuContext *ctx)
{
	FuAmdAfcState *self = g_new0(FuAmdAfcState, 1);
	g_ref_count_init(&self->refcount);
	self->efivars = g_object_ref(fu_context_get_efivars(ctx));
	self->strings = g_ptr_array_new_with_free_func(g_free);
	self->forms = g_ptr_array_new_with_free_func((GDestroyNotify)fu_amd_afc_hii_form_free);
	self->varstores =
	    g_ptr_array_new_with_free_func((GDestroyNotify)fu_amd_afc_hii_varstore_free);
	self->settings =
	    g_ptr_array_new_with_free_func((GDestroyNotify)fu_amd_afc_hii_setting_free);
	self->pending = g_ptr_array_new_with_free_func((GDestroyNotify)fu_amd_afc_pending_free);
	return self;
}

FuAmdAfcState *
fu_amd_afc_state_ref(FuAmdAfcState *self)
{
	g_ref_count_inc(&self->refcount);
	return self;
}

void
fu_amd_afc_state_unref(FuAmdAfcState *self)
{
	if (self == NULL || !g_ref_count_dec(&self->refcount))
		return;
	if (self->bios_settings != NULL)
		g_object_remove_weak_pointer(G_OBJECT(self->bios_settings),
					     (gpointer *)&self->bios_settings);
	g_clear_object(&self->efivars);
	g_ptr_array_unref(self->strings);
	g_ptr_array_unref(self->forms);
	g_ptr_array_unref(self->varstores);
	g_ptr_array_unref(self->settings);
	g_ptr_array_unref(self->pending);
	g_free(self->formset_name);
	g_free(self->language);
	g_free(self);
}

guint
fu_amd_afc_state_get_setting_count(FuAmdAfcState *self)
{
	g_return_val_if_fail(self != NULL, 0);
	return self->settings->len;
}

static gchar *
fu_amd_afc_setting_name(FuAmdAfcSetting *setting)
{
	g_autoptr(GString) name = g_string_new(NULL);
	g_autofree gchar *component = NULL;
	for (guint i = 0; i < setting->path->len; i++) {
		g_clear_pointer(&component, g_free);
		component = g_strdup(g_ptr_array_index(setting->path, i));
		g_strstrip(component);
		if (component[0] == '\0')
			continue;
		if (name->len > 0)
			g_string_append_c(name, '|');
		for (const gchar *p = component; *p != '\0'; p++)
			g_string_append_c(name, *p == '/' ? '!' : *p);
	}
	return g_string_free(g_steal_pointer(&name), FALSE);
}

gboolean
fu_amd_afc_state_add_bios_settings(FuAmdAfcState *self,
				   FuBiosSettings *bios_settings,
				   GError **error)
{
	if (self->bios_settings == NULL) {
		self->bios_settings = bios_settings;
		g_object_add_weak_pointer(G_OBJECT(bios_settings),
					  (gpointer *)&self->bios_settings);
	}

	for (guint i = 0; i < self->settings->len; i++) {
		FuAmdAfcSetting *setting = g_ptr_array_index(self->settings, i);
		g_autofree gchar *name = fu_amd_afc_setting_name(setting);
		g_autofree gchar *name_stripped = NULL;
		g_autofree gchar *id = NULL;
		g_autoptr(FuAmdAfcBiosSetting) attr =
		    g_object_new(FU_TYPE_AMD_AFC_BIOS_SETTING, NULL);
		/* match the ID produced by the generic sysfs firmware-attributes loader */
		name_stripped = g_strdup(name);
		g_strdelimit(name_stripped, " ", '_');
		g_strdelimit(name_stripped, "|", '.');
		id = g_strdup_printf("com.amd-afc.%s.%04x", name_stripped, setting->question_id);
		/* the sysfs loader also ignores settings whose varstore is unavailable */
		if (!setting->has_current)
			continue;
		attr->state = fu_amd_afc_state_ref(self);
		attr->index = i;
		fwupd_bios_setting_set_name(FWUPD_BIOS_SETTING(attr), name);
		fwupd_bios_setting_set_id(FWUPD_BIOS_SETTING(attr), id);
		fwupd_bios_setting_set_read_only(
		    FWUPD_BIOS_SETTING(attr),
		    (setting->question_flags & AFC_IFR_FLAG_READ_ONLY) != 0);
		if (setting->kind == AFC_SETTING_ENUMERATION) {
			fwupd_bios_setting_set_kind(FWUPD_BIOS_SETTING(attr),
						    FWUPD_BIOS_SETTING_KIND_ENUMERATION);
			for (guint j = 0; j < setting->options->len; j++) {
				FuAmdAfcOption *option = g_ptr_array_index(setting->options, j);
				g_autofree gchar *display = g_strdup(option->name);
				fwupd_bios_setting_add_possible_value_full(FWUPD_BIOS_SETTING(attr),
									   g_strchomp(display),
									   option->name);
			}
		} else {
			fwupd_bios_setting_set_kind(FWUPD_BIOS_SETTING(attr),
						    FWUPD_BIOS_SETTING_KIND_INTEGER);
			fwupd_bios_setting_set_lower_bound(FWUPD_BIOS_SETTING(attr),
							   setting->minimum);
			fwupd_bios_setting_set_upper_bound(FWUPD_BIOS_SETTING(attr),
							   setting->maximum);
			fwupd_bios_setting_set_scalar_increment(FWUPD_BIOS_SETTING(attr),
								setting->step);
		}
		if (!fwupd_bios_setting_setup(FWUPD_BIOS_SETTING(attr), error))
			return FALSE;
		if (!fu_bios_settings_register_attr(bios_settings, FWUPD_BIOS_SETTING(attr), error))
			return FALSE;
	}
	return TRUE;
}
