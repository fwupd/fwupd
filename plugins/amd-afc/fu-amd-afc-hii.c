/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * nocheck:magic-defines=60 -- constants mirror the UEFI HII format
 */

#include "config.h"

#include "fu-amd-afc-common.h"
#include "fu-amd-afc-struct.h"

#define HII_PACKAGE_FORMS		0x02U
#define IFR_FORM_OP			0x01U
#define IFR_ONE_OF_OP			0x05U
#define IFR_CHECKBOX_OP			0x06U
#define IFR_NUMERIC_OP			0x07U
#define IFR_ONE_OF_OPTION_OP		0x09U
#define IFR_FORM_SET_OP			0x0eU
#define IFR_REF_OP			0x0fU
#define IFR_END_OP			0x29U
#define IFR_DEFAULT_OP			0x5bU
#define IFR_GUID_OP			0x5fU
#define IFR_OP_LENGTH_MASK		0x7fU
#define IFR_OP_SCOPE			0x80U
#define IFR_FORM_SET_MIN_SIZE		23U
#define IFR_FORM_SET_TITLE_OFFSET	18U
#define IFR_FORM_MIN_SIZE		6U
#define IFR_FORM_ID_OFFSET		2U
#define IFR_FORM_TITLE_OFFSET		4U
#define IFR_REF_MIN_SIZE		15U
#define IFR_REF_FORM_ID_OFFSET		13U
#define IFR_GUID_MIN_SIZE		18U
#define IFR_GUID_VALUE_OFFSET		2U
#define IFR_QUESTION_MIN_SIZE		17U
#define IFR_QUESTION_PROMPT_OFFSET	2U
#define IFR_QUESTION_ID_OFFSET		6U
#define IFR_QUESTION_VARSTORE_ID_OFFSET 8U
#define IFR_QUESTION_VARSTORE_OFFSET	10U
#define IFR_QUESTION_FLAGS_OFFSET	12U
#define IFR_QUESTION_TYPE_OFFSET	13U
#define IFR_QUESTION_VALUES_OFFSET	14U
#define IFR_NUMERIC_VALUE_COUNT		3U
#define IFR_TYPE_WIDTH_MASK		0x03U
#define IFR_TYPE_U64			3U
#define IFR_BITFIELD_WIDTH_MASK		0x3fU
#define IFR_FLAG_READ_ONLY		0x01U
#define IFR_OPTION_DEFAULT		(1U << 4)
#define IFR_OPTION_MIN_SIZE		7U
#define IFR_OPTION_STRING_OFFSET	2U
#define IFR_OPTION_FLAGS_OFFSET		4U
#define IFR_OPTION_TYPE_OFFSET		5U
#define IFR_OPTION_VALUE_OFFSET		6U
#define IFR_DEFAULT_MIN_SIZE		6U
#define IFR_DEFAULT_ID_OFFSET		2U
#define IFR_DEFAULT_TYPE_OFFSET		4U
#define IFR_DEFAULT_VALUE_OFFSET	5U

#define AFC_MAX_PATH_DEPTH 32U
#define AFC_MAX_IFR_DEPTH  64U
static const fwupd_guid_t bit_varstore_guid = {/* nocheck:magic */
					       0x8b,
					       0xd6,
					       0xdd,
					       0x82,
					       0x63,
					       0x91,
					       0x87,
					       0x41,
					       0x9b,
					       0x27,
					       0x20,
					       0xa8,
					       0xfd,
					       0x60,
					       0xa7,
					       0x1d};

static void
fu_amd_afc_hii_option_free(FuAmdAfcOption *option)
{
	g_free(option->name);
	g_free(option);
}

void
fu_amd_afc_hii_setting_free(FuAmdAfcSetting *setting)
{
	g_free(setting->display_name);
	g_free(setting->language);
	g_ptr_array_unref(setting->path);
	g_ptr_array_unref(setting->options);
	g_free(setting);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuAmdAfcSetting, fu_amd_afc_hii_setting_free)

void
fu_amd_afc_hii_form_free(FuAmdAfcForm *form)
{
	g_free(form->name);
	g_array_unref(form->settings);
	g_array_unref(form->refs);
	g_free(form);
}

void
fu_amd_afc_hii_varstore_free(FuAmdAfcVarstore *varstore)
{
	g_bytes_unref(varstore->data);
	g_free(varstore);
}

gboolean
fu_amd_afc_hii_bounds(gsize bufsz, gsize offset, gsize length, GError **error)
{
	if (offset > bufsz || length > bufsz - offset) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "AFC data is truncated");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_amd_afc_hii_read_value(const guint8 *buf,
			  gsize bufsz,
			  gsize offset,
			  gsize width,
			  guint64 *value,
			  GError **error)
{
	guint8 tmp[sizeof(guint64)] = {0};
	if (width == 0 || width > sizeof(tmp)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid AFC integer width");
		return FALSE;
	}
	if (!fu_memcpy_safe(tmp, sizeof(tmp), 0, buf, bufsz, offset, width, error))
		return FALSE;
	return fu_memread_uint64_safe(tmp, sizeof(tmp), 0, value, G_LITTLE_ENDIAN, error);
}

gboolean
fu_amd_afc_hii_parse_varstores(FuAmdAfcState *self, const guint8 *buf, gsize bufsz, GError **error)
{
	gsize offset = 0;
	while (offset < bufsz) {
		FuAmdAfcVarstore *varstore = NULL;
		g_autoptr(FuStructAmdAfcVarstoreHeader) st_hdr = NULL;
		guint32 length;
		guint32 data_size;
		guint16 id;
		guint8 name_size;
		gsize data_offset;
		st_hdr = fu_struct_amd_afc_varstore_header_parse(buf, bufsz, offset, error);
		if (st_hdr == NULL)
			return FALSE;
		length = fu_struct_amd_afc_varstore_header_get_length(st_hdr);
		name_size = fu_struct_amd_afc_varstore_header_get_name_size(st_hdr);
		id = fu_struct_amd_afc_varstore_header_get_id(st_hdr);
		data_size = fu_struct_amd_afc_varstore_header_get_data_size(st_hdr);
		if (length < FU_STRUCT_AMD_AFC_VARSTORE_HEADER_SIZE || length > bufsz - offset ||
		    name_size > length - FU_STRUCT_AMD_AFC_VARSTORE_HEADER_SIZE ||
		    data_size > length - FU_STRUCT_AMD_AFC_VARSTORE_HEADER_SIZE - name_size) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "invalid AFC varstore");
			return FALSE;
		}
		data_offset = offset + FU_STRUCT_AMD_AFC_VARSTORE_HEADER_SIZE + name_size;
		varstore = g_new0(FuAmdAfcVarstore, 1);
		varstore->id = id;
		varstore->data = g_bytes_new(buf + data_offset, data_size);
		g_ptr_array_add(self->varstores, varstore);
		offset += length;
	}
	return TRUE;
}

static gboolean
fu_amd_afc_hii_current_value(FuAmdAfcState *self,
			     guint16 varstore_id,
			     gsize offset,
			     gboolean bitfield,
			     guint8 bits,
			     gsize setting_width,
			     guint64 *value)
{
	for (guint i = 0; i < self->varstores->len; i++) {
		FuAmdAfcVarstore *varstore = g_ptr_array_index(self->varstores, i);
		gsize bufsz = 0;
		const guint8 *buf;
		gsize width = setting_width;
		guint shift = 0;
		if (varstore->id != varstore_id)
			continue;
		buf = g_bytes_get_data(varstore->data, &bufsz);
		if (bitfield) {
			shift = offset % 8;
			offset /= 8;
			width = (shift + bits + 7) / 8;
		}
		if (!fu_amd_afc_hii_read_value(buf, bufsz, offset, width, value, NULL))
			return FALSE;
		if (bitfield)
			*value = (*value >> shift) & ((((guint64)1) << bits) - 1);
		return TRUE;
	}
	return FALSE;
}

static gboolean
fu_amd_afc_hii_new_setting(FuAmdAfcState *self,
			   guint form_idx,
			   const guint8 *op,
			   gsize opsz,
			   gboolean bitfield,
			   guint *setting_idx,
			   GError **error)
{
	g_autoptr(FuAmdAfcSetting) setting = g_new0(FuAmdAfcSetting, 1);
	FuAmdAfcForm *form = g_ptr_array_index(self->forms, form_idx);
	guint16 prompt = 0;
	guint16 varstore_id = 0;
	guint16 varstore_offset = 0;
	const gchar *display_name;
	guint8 flags = op[IFR_QUESTION_TYPE_OFFSET];
	guint8 bits = bitfield ? flags & IFR_BITFIELD_WIDTH_MASK : 0;
	gsize width = bitfield ? ((gsize)bits + 7) / 8
			       : ((gsize)1 << (flags & IFR_TYPE_WIDTH_MASK));
	gsize data_width = bitfield ? 4 : width;

	setting->path = g_ptr_array_new_with_free_func(g_free);
	setting->options =
	    g_ptr_array_new_with_free_func((GDestroyNotify)fu_amd_afc_hii_option_free);
	if ((bitfield && (bits == 0 || bits > 32)) || width > sizeof(guint64)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid AFC question width");
		return FALSE;
	}
	if (!fu_memread_uint16_safe(op,
				    opsz,
				    IFR_QUESTION_PROMPT_OFFSET,
				    &prompt,
				    G_LITTLE_ENDIAN,
				    error) ||
	    !fu_memread_uint16_safe(op,
				    opsz,
				    IFR_QUESTION_ID_OFFSET,
				    &setting->question_id,
				    G_LITTLE_ENDIAN,
				    error) ||
	    !fu_memread_uint16_safe(op,
				    opsz,
				    IFR_QUESTION_VARSTORE_ID_OFFSET,
				    &varstore_id,
				    G_LITTLE_ENDIAN,
				    error) ||
	    !fu_memread_uint16_safe(op,
				    opsz,
				    IFR_QUESTION_VARSTORE_OFFSET,
				    &varstore_offset,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	display_name = fu_amd_afc_hii_strings_get_string(self, prompt, error);
	if (display_name == NULL || self->language == NULL)
		return FALSE;
	setting->display_name = g_strdup(display_name);
	setting->language = g_strdup(self->language);
	setting->question_flags = op[IFR_QUESTION_FLAGS_OFFSET];
	setting->kind = op[0] == IFR_ONE_OF_OP ? AFC_SETTING_ENUMERATION : AFC_SETTING_INTEGER;
	if (setting->kind == AFC_SETTING_INTEGER) {
		if (!fu_amd_afc_hii_read_value(op,
					       opsz,
					       IFR_QUESTION_VALUES_OFFSET,
					       data_width,
					       &setting->minimum,
					       error) ||
		    !fu_amd_afc_hii_read_value(op,
					       opsz,
					       IFR_QUESTION_VALUES_OFFSET + data_width,
					       data_width,
					       &setting->maximum,
					       error) ||
		    !fu_amd_afc_hii_read_value(op,
					       opsz,
					       IFR_QUESTION_VALUES_OFFSET + (2 * data_width),
					       data_width,
					       &setting->step,
					       error))
			return FALSE;
		if (setting->step == 0)
			setting->step = 1;
	}
	setting->has_current = fu_amd_afc_hii_current_value(self,
							    varstore_id,
							    varstore_offset,
							    bitfield,
							    bits,
							    width,
							    &setting->current);
	*setting_idx = self->settings->len;
	g_ptr_array_add(self->settings, g_steal_pointer(&setting));
	g_array_append_val(form->settings, *setting_idx);
	return TRUE;
}

typedef struct {
	gint form;
	gint setting;
	gboolean bitfield;
} FuAmdAfcScope;

gboolean
fu_amd_afc_hii_parse_forms(FuAmdAfcState *self, const guint8 *buf, gsize bufsz, GError **error)
{
	guint8 package_type = 0;
	guint32 package_length = 0;
	g_autoptr(FuStructAmdAfcHiiPackageHeader) st_hdr = NULL;
	g_autoptr(GArray) scopes = g_array_new(FALSE, FALSE, sizeof(FuAmdAfcScope));
	gint form_idx = -1;
	gint setting_idx = -1;
	gboolean bitfield = FALSE;
	gsize offset = FU_STRUCT_AMD_AFC_HII_PACKAGE_HEADER_SIZE;

	st_hdr = fu_struct_amd_afc_hii_package_header_parse(buf, bufsz, 0, error);
	if (st_hdr == NULL)
		return FALSE;
	package_type = fu_struct_amd_afc_hii_package_header_get_kind(st_hdr);
	package_length = fu_struct_amd_afc_hii_package_header_get_length(st_hdr);
	if (package_type != HII_PACKAGE_FORMS || package_length != bufsz) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid AFC forms package");
		return FALSE;
	}
	while (offset < bufsz) {
		const guint8 *op = buf + offset;
		gsize remaining = bufsz - offset;
		guint8 opcode;
		guint length;
		gboolean has_scope;
		if (remaining < 2) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "truncated AFC opcode");
			return FALSE;
		}
		opcode = op[0];
		length = op[1] & IFR_OP_LENGTH_MASK;
		has_scope = (op[1] & IFR_OP_SCOPE) != 0;
		if (length < 2 || length > remaining) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "invalid AFC opcode length");
			return FALSE;
		}
		if (opcode == IFR_END_OP) {
			FuAmdAfcScope parent;
			if (scopes->len == 0) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_DATA,
						    "unbalanced AFC scope");
				return FALSE;
			}
			parent = g_array_index(scopes, FuAmdAfcScope, scopes->len - 1);
			g_array_set_size(scopes, scopes->len - 1);
			form_idx = parent.form;
			setting_idx = parent.setting;
			bitfield = parent.bitfield;
			offset += length;
			continue;
		}
		if (has_scope) {
			FuAmdAfcScope scope = {form_idx, setting_idx, bitfield};
			if (scopes->len == AFC_MAX_IFR_DEPTH) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_DATA,
						    "AFC scopes are too deep");
				return FALSE;
			}
			g_array_append_val(scopes, scope);
		}
		switch (opcode) {
		case IFR_FORM_SET_OP:
			if (length >= IFR_FORM_SET_MIN_SIZE) {
				guint16 title = 0;
				const gchar *name;
				if (!fu_memread_uint16_safe(op,
							    length,
							    IFR_FORM_SET_TITLE_OFFSET,
							    &title,
							    G_LITTLE_ENDIAN,
							    error))
					return FALSE;
				name = fu_amd_afc_hii_strings_get_string(self, title, error);
				if (name == NULL)
					return FALSE;
				g_free(self->formset_name);
				self->formset_name = g_strdup(name);
			}
			break;
		case IFR_FORM_OP:
			if (length >= IFR_FORM_MIN_SIZE) {
				FuAmdAfcForm *form = g_new0(FuAmdAfcForm, 1);
				guint16 title = 0;
				const gchar *name;
				if (!fu_memread_uint16_safe(op,
							    length,
							    IFR_FORM_ID_OFFSET,
							    &form->id,
							    G_LITTLE_ENDIAN,
							    error) ||
				    !fu_memread_uint16_safe(op,
							    length,
							    IFR_FORM_TITLE_OFFSET,
							    &title,
							    G_LITTLE_ENDIAN,
							    error)) {
					g_free(form);
					return FALSE;
				}
				name = fu_amd_afc_hii_strings_get_string(self, title, error);
				if (name == NULL) {
					g_free(form);
					return FALSE;
				}
				form->name = g_strdup(name);
				form->settings = g_array_new(FALSE, FALSE, sizeof(guint));
				form->refs = g_array_new(FALSE, FALSE, sizeof(guint16));
				form_idx = self->forms->len;
				setting_idx = -1;
				g_ptr_array_add(self->forms, form);
			}
			break;
		case IFR_REF_OP:
			if (form_idx >= 0 && length >= IFR_REF_MIN_SIZE) {
				FuAmdAfcForm *form = g_ptr_array_index(self->forms, form_idx);
				guint16 ref = 0;
				if (!fu_memread_uint16_safe(op,
							    length,
							    IFR_REF_FORM_ID_OFFSET,
							    &ref,
							    G_LITTLE_ENDIAN,
							    error))
					return FALSE;
				g_array_append_val(form->refs, ref);
			}
			break;
		case IFR_GUID_OP:
			if (length >= IFR_GUID_MIN_SIZE)
				bitfield = memcmp(op + IFR_GUID_VALUE_OFFSET,
						  bit_varstore_guid,
						  sizeof(bit_varstore_guid)) == 0;
			break;
		case IFR_CHECKBOX_OP:
			if (bitfield) {
				bitfield = FALSE;
				if (has_scope)
					g_array_index(scopes, FuAmdAfcScope, scopes->len - 1)
					    .bitfield = FALSE;
			}
			break;
		case IFR_ONE_OF_OP:
		case IFR_NUMERIC_OP:
			if (form_idx >= 0 && length >= IFR_QUESTION_MIN_SIZE) {
				gboolean question_bitfield = bitfield;
				gsize width = question_bitfield
						  ? 4
						  : ((gsize)1 << (op[IFR_QUESTION_TYPE_OFFSET] &
								  IFR_TYPE_WIDTH_MASK));
				guint16 prompt = 0;
				if (!fu_memread_uint16_safe(op,
							    length,
							    IFR_QUESTION_PROMPT_OFFSET,
							    &prompt,
							    G_LITTLE_ENDIAN,
							    error))
					return FALSE;
				if (width <= sizeof(guint64) &&
				    length >= IFR_QUESTION_VALUES_OFFSET +
						  IFR_NUMERIC_VALUE_COUNT * width) {
					if (prompt == 0) { /* nocheck:depth */
						if (has_scope)
							setting_idx = -1;
					} else { /* nocheck:depth */
						guint new_idx = 0;
						if (!fu_amd_afc_hii_new_setting(self,
										form_idx,
										op,
										length,
										question_bitfield,
										&new_idx,
										error))
							return FALSE;
						if (has_scope)
							setting_idx = new_idx;
					}
				}
				if (question_bitfield) {
					bitfield = FALSE;
					if (has_scope)
						g_array_index(scopes,
							      FuAmdAfcScope,
							      scopes->len - 1)
						    .bitfield = FALSE;
				}
			}
			break;
		case IFR_ONE_OF_OPTION_OP:
			if (setting_idx >= 0 && length >= IFR_OPTION_MIN_SIZE) {
				FuAmdAfcSetting *setting =
				    g_ptr_array_index(self->settings, setting_idx);
				guint8 value_type = op[IFR_OPTION_TYPE_OFFSET];
				gsize width = value_type <= IFR_TYPE_U64 ? ((gsize)1 << value_type)
									 : 0;
				if (setting->kind == AFC_SETTING_ENUMERATION && width > 0 &&
				    length >= IFR_OPTION_VALUE_OFFSET + width) {
					FuAmdAfcOption *option = g_new0(FuAmdAfcOption, 1);
					guint16 string_id = 0;
					const gchar *name;
					if (!fu_memread_uint16_safe(op, /* nocheck:depth */
								    length,
								    IFR_OPTION_STRING_OFFSET,
								    &string_id,
								    G_LITTLE_ENDIAN,
								    error) ||
					    !fu_amd_afc_hii_read_value(op,
								       length,
								       IFR_OPTION_VALUE_OFFSET,
								       width,
								       &option->value,
								       error)) {
						g_free(option);
						return FALSE;
					}
					name = fu_amd_afc_hii_strings_get_string(
					    self, /* nocheck:depth */
					    string_id,
					    error);
					if (name == NULL) {
						g_free(option);
						return FALSE;
					}
					option->name = g_strdup(name);
					if ((op[IFR_OPTION_FLAGS_OFFSET] &
					     IFR_OPTION_DEFAULT) != /* nocheck:depth */
					    0) {
						setting->has_default = TRUE;
						setting->default_value = option->value;
					}
					g_ptr_array_add(setting->options, option);
				}
			}
			break;
		case IFR_DEFAULT_OP:
			if (setting_idx >= 0 && length >= IFR_DEFAULT_MIN_SIZE) {
				FuAmdAfcSetting *setting =
				    g_ptr_array_index(self->settings, setting_idx);
				guint16 default_id = 0;
				guint8 value_type = op[IFR_DEFAULT_TYPE_OFFSET];
				gsize width = value_type <= IFR_TYPE_U64 ? ((gsize)1 << value_type)
									 : 0;
				if (!fu_memread_uint16_safe(op,
							    length,
							    IFR_DEFAULT_ID_OFFSET,
							    &default_id,
							    G_LITTLE_ENDIAN,
							    error))
					return FALSE;
				if (default_id == 0 && width > 0 &&
				    length >= IFR_DEFAULT_VALUE_OFFSET + width) {
					if (!fu_amd_afc_hii_read_value(op,
								       length,
								       IFR_DEFAULT_VALUE_OFFSET,
								       width,
								       &setting->default_value,
								       error))
						return FALSE;
					setting->has_default = TRUE;
				}
			}
			break;
		default:
			break;
		}
		offset += length;
	}
	if (scopes->len != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "unterminated AFC scope");
		return FALSE;
	}
	return TRUE;
}

static gint
fu_amd_afc_hii_find_form(FuAmdAfcState *self, guint16 id)
{
	for (guint i = 0; i < self->forms->len; i++) {
		FuAmdAfcForm *form = g_ptr_array_index(self->forms, i);
		if (form->id == id)
			return i;
	}
	return -1;
}

gboolean
fu_amd_afc_hii_assign_form_paths(FuAmdAfcState *self,
				 guint form_idx,
				 GPtrArray *path,
				 GError **error)
{
	FuAmdAfcForm *form = g_ptr_array_index(self->forms, form_idx);
	if (form->visited)
		return TRUE;
	if (path->len == AFC_MAX_PATH_DEPTH) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "AFC form path is too deep");
		return FALSE;
	}
	form->visited = TRUE;
	g_ptr_array_add(path, g_strdup(form->name));
	for (guint i = 0; i < form->settings->len; i++) {
		guint idx = g_array_index(form->settings, guint, i);
		FuAmdAfcSetting *setting = g_ptr_array_index(self->settings, idx);
		for (guint j = 0; j < path->len; j++)
			g_ptr_array_add(setting->path, g_strdup(g_ptr_array_index(path, j)));
		g_ptr_array_add(setting->path, g_strdup(setting->display_name));
	}
	for (guint i = 0; i < form->refs->len; i++) {
		guint16 id = g_array_index(form->refs, guint16, i);
		gint child = fu_amd_afc_hii_find_form(self, id);
		if (child >= 0 && !fu_amd_afc_hii_assign_form_paths(self, child, path, error))
			return FALSE;
	}
	g_ptr_array_remove_index(path, path->len - 1);
	return TRUE;
}
