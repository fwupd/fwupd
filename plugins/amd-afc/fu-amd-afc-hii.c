/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * nocheck:magic-defines=60 -- constants mirror the UEFI HII format
 */

#include "config.h"

#include "fu-amd-afc-common.h"
#include "fu-amd-afc-struct.h"

#define HII_PACKAGE_FORMS	   0x02U
#define IFR_FORM_OP		   0x01U
#define IFR_ONE_OF_OP		   0x05U
#define IFR_CHECKBOX_OP		   0x06U
#define IFR_NUMERIC_OP		   0x07U
#define IFR_ONE_OF_OPTION_OP	   0x09U
#define IFR_FORM_SET_OP		   0x0eU
#define IFR_REF_OP		   0x0fU
#define IFR_END_OP		   0x29U
#define IFR_DEFAULT_OP		   0x5bU
#define IFR_GUID_OP		   0x5fU
#define IFR_OP_LENGTH_MASK	   0x7fU
#define IFR_OP_SCOPE		   0x80U
#define IFR_NUMERIC_VALUE_COUNT	   3U
#define IFR_TYPE_WIDTH_MASK	   0x03U
#define IFR_TYPE_U64		   3U
#define IFR_BITFIELD_WIDTH_MASK	   0x3fU
#define IFR_FLAG_READ_ONLY	   0x01U
#define IFR_OPTION_DEFAULT	   (1U << 4)

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

void
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

static gboolean
fu_amd_afc_hii_parse_varstore(FuAmdAfcState *self,
			      const guint8 *buf,
			      gsize bufsz,
			      gsize offset,
			      gsize *offset_next,
			      GError **error)
{
	g_autoptr(FuAmdAfcVarstore) varstore = g_new0(FuAmdAfcVarstore, 1);
	g_autoptr(FuStructAmdAfcVarstoreHeader) st_hdr = NULL;
	guint32 length;
	guint32 data_size;
	guint8 name_size;
	gsize data_offset;

	st_hdr = fu_struct_amd_afc_varstore_header_parse(buf, bufsz, offset, error);
	if (st_hdr == NULL)
		return FALSE;
	length = fu_struct_amd_afc_varstore_header_get_length(st_hdr);
	name_size = fu_struct_amd_afc_varstore_header_get_name_size(st_hdr);
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
	varstore->id = fu_struct_amd_afc_varstore_header_get_id(st_hdr);
	varstore->data = g_bytes_new(buf + data_offset, data_size);
	g_ptr_array_add(self->varstores, g_steal_pointer(&varstore));
	*offset_next = offset + length;
	return TRUE;
}

gboolean
fu_amd_afc_hii_parse_varstores(FuAmdAfcState *self, const guint8 *buf, gsize bufsz, GError **error)
{
	gsize offset = 0;
	while (offset < bufsz) {
		if (!fu_amd_afc_hii_parse_varstore(self, buf, bufsz, offset, &offset, error))
			return FALSE;
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
	g_autoptr(FuStructAmdAfcIfrQuestion) st_question = NULL;
	FuAmdAfcForm *form = g_ptr_array_index(self->forms, form_idx);
	const gchar *display_name;
	guint8 flags;
	guint8 bits;
	guint16 prompt;
	guint16 varstore_id;
	guint16 varstore_offset;
	gsize width;
	gsize data_width;

	st_question = fu_struct_amd_afc_ifr_question_parse(op, opsz, 0, error);
	if (st_question == NULL)
		return FALSE;
	flags = fu_struct_amd_afc_ifr_question_get_value_type(st_question);
	prompt = fu_struct_amd_afc_ifr_question_get_prompt(st_question);
	varstore_id = fu_struct_amd_afc_ifr_question_get_varstore_id(st_question);
	varstore_offset = fu_struct_amd_afc_ifr_question_get_varstore_offset(st_question);
	bits = bitfield ? flags & IFR_BITFIELD_WIDTH_MASK : 0;
	width = bitfield ? ((gsize)bits + 7) / 8 : ((gsize)1 << (flags & IFR_TYPE_WIDTH_MASK));
	data_width = bitfield ? 4 : width;

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
	display_name = fu_amd_afc_hii_strings_get_string(self, prompt, error);
	if (display_name == NULL || self->language == NULL)
		return FALSE;
	setting->display_name = g_strdup(display_name);
	setting->language = g_strdup(self->language);
	setting->question_id = fu_struct_amd_afc_ifr_question_get_id(st_question);
	setting->question_flags = fu_struct_amd_afc_ifr_question_get_flags(st_question);
	setting->kind = op[0] == IFR_ONE_OF_OP ? AFC_SETTING_ENUMERATION : AFC_SETTING_INTEGER;
	if (setting->kind == AFC_SETTING_INTEGER) {
		if (!fu_amd_afc_hii_read_value(op,
					       opsz,
					       FU_STRUCT_AMD_AFC_IFR_QUESTION_SIZE,
					       data_width,
					       &setting->minimum,
					       error) ||
		    !fu_amd_afc_hii_read_value(op,
					       opsz,
					       FU_STRUCT_AMD_AFC_IFR_QUESTION_SIZE + data_width,
					       data_width,
					       &setting->maximum,
					       error) ||
		    !fu_amd_afc_hii_read_value(op,
					       opsz,
					       FU_STRUCT_AMD_AFC_IFR_QUESTION_SIZE +
						   (2 * data_width),
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

static gboolean
fu_amd_afc_hii_parse_form(FuAmdAfcState *self,
			  const guint8 *op,
			  gsize opsz,
			  gint *form_idx,
			  gint *setting_idx,
			  GError **error)
{
	g_autoptr(FuAmdAfcForm) form = g_new0(FuAmdAfcForm, 1);
	g_autoptr(FuStructAmdAfcIfrForm) st_form = NULL;
	const gchar *name;

	st_form = fu_struct_amd_afc_ifr_form_parse(op, opsz, 0, error);
	if (st_form == NULL)
		return FALSE;
	form->id = fu_struct_amd_afc_ifr_form_get_id(st_form);
	name = fu_amd_afc_hii_strings_get_string(self,
						 fu_struct_amd_afc_ifr_form_get_title(st_form),
						 error);
	if (name == NULL)
		return FALSE;
	form->name = g_strdup(name);
	form->settings = g_array_new(FALSE, FALSE, sizeof(guint));
	form->refs = g_array_new(FALSE, FALSE, sizeof(guint16));
	*form_idx = self->forms->len;
	*setting_idx = -1;
	g_ptr_array_add(self->forms, g_steal_pointer(&form));
	return TRUE;
}

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
			if (length >= FU_STRUCT_AMD_AFC_IFR_FORM_SET_SIZE) {
				g_autoptr(FuStructAmdAfcIfrFormSet) st_form_set =
				    fu_struct_amd_afc_ifr_form_set_parse(op, length, 0, error);
				const gchar *name;
				if (st_form_set == NULL)
					return FALSE;
				name = fu_amd_afc_hii_strings_get_string(
				    self,
				    fu_struct_amd_afc_ifr_form_set_get_title(st_form_set),
				    error);
				if (name == NULL)
					return FALSE;
				g_free(self->formset_name);
				self->formset_name = g_strdup(name);
			}
			break;
		case IFR_FORM_OP:
			if (length >= FU_STRUCT_AMD_AFC_IFR_FORM_SIZE &&
			    !fu_amd_afc_hii_parse_form(self,
						       op,
						       length,
						       &form_idx,
						       &setting_idx,
						       error))
				return FALSE;
			break;
		case IFR_REF_OP:
			if (form_idx >= 0 && length >= FU_STRUCT_AMD_AFC_IFR_REF_SIZE) {
				g_autoptr(FuStructAmdAfcIfrRef) st_ref =
				    fu_struct_amd_afc_ifr_ref_parse(op, length, 0, error);
				FuAmdAfcForm *form = g_ptr_array_index(self->forms, form_idx);
				guint16 ref;
				if (st_ref == NULL)
					return FALSE;
				ref = fu_struct_amd_afc_ifr_ref_get_form_id(st_ref);
				g_array_append_val(form->refs, ref);
			}
			break;
		case IFR_GUID_OP:
			if (length >= FU_STRUCT_AMD_AFC_IFR_GUID_SIZE) {
				g_autoptr(FuStructAmdAfcIfrGuid) st_guid =
				    fu_struct_amd_afc_ifr_guid_parse(op, length, 0, error);
				if (st_guid == NULL)
					return FALSE;
				bitfield = memcmp(fu_struct_amd_afc_ifr_guid_get_guid(st_guid),
						  bit_varstore_guid,
						  sizeof(bit_varstore_guid)) == 0;
			}
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
			if (form_idx >= 0 && length >= FU_STRUCT_AMD_AFC_IFR_QUESTION_SIZE +
							   IFR_NUMERIC_VALUE_COUNT) {
				gboolean question_bitfield = bitfield;
				g_autoptr(FuStructAmdAfcIfrQuestion) st_question =
				    fu_struct_amd_afc_ifr_question_parse(op, length, 0, error);
				gsize width;
				guint16 prompt;
				if (st_question == NULL)
					return FALSE;
				width = question_bitfield
					    ? 4
					    : ((gsize)1
					       << (fu_struct_amd_afc_ifr_question_get_value_type(
						       st_question) &
						   IFR_TYPE_WIDTH_MASK));
				prompt = fu_struct_amd_afc_ifr_question_get_prompt(st_question);
				if (width <= sizeof(guint64) &&
				    length >= FU_STRUCT_AMD_AFC_IFR_QUESTION_SIZE +
						  (IFR_NUMERIC_VALUE_COUNT * width)) {
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
			if (setting_idx >= 0 && length > FU_STRUCT_AMD_AFC_IFR_OPTION_SIZE) {
				g_autoptr(FuStructAmdAfcIfrOption) st_option =
				    fu_struct_amd_afc_ifr_option_parse(op, length, 0, error);
				FuAmdAfcSetting *setting =
				    g_ptr_array_index(self->settings, setting_idx);
				guint8 value_type;
				gsize width;
				if (st_option == NULL)
					return FALSE;
				value_type = fu_struct_amd_afc_ifr_option_get_value_type(st_option);
				width = value_type <= IFR_TYPE_U64 ? ((gsize)1 << value_type) : 0;
				if (setting->kind == AFC_SETTING_ENUMERATION && width > 0 &&
				    length >= FU_STRUCT_AMD_AFC_IFR_OPTION_SIZE + width) {
					g_autoptr(FuAmdAfcOption) option =
					    g_new0(FuAmdAfcOption, 1);
					const gchar *name;
					if (!fu_amd_afc_hii_read_value(
						op,
						length,
						FU_STRUCT_AMD_AFC_IFR_OPTION_SIZE,
						width,
						&option->value,
						error))
						return FALSE;
					name = fu_amd_afc_hii_strings_get_string(
					    self, /* nocheck:depth */
					    fu_struct_amd_afc_ifr_option_get_string_id(st_option),
					    error);
					if (name == NULL)
						return FALSE;
					option->name = g_strdup(name);
					if ((fu_struct_amd_afc_ifr_option_get_flags(st_option) &
					     IFR_OPTION_DEFAULT) != /* nocheck:depth */
					    0) {
						setting->has_default = TRUE;
						setting->default_value = option->value;
					}
					g_ptr_array_add(setting->options, g_steal_pointer(&option));
				}
			}
			break;
		case IFR_DEFAULT_OP:
			if (setting_idx >= 0 && length > FU_STRUCT_AMD_AFC_IFR_DEFAULT_SIZE) {
				g_autoptr(FuStructAmdAfcIfrDefault) st_default =
				    fu_struct_amd_afc_ifr_default_parse(op, length, 0, error);
				FuAmdAfcSetting *setting =
				    g_ptr_array_index(self->settings, setting_idx);
				guint8 value_type;
				gsize width;
				if (st_default == NULL)
					return FALSE;
				value_type =
				    fu_struct_amd_afc_ifr_default_get_value_type(st_default);
				width = value_type <= IFR_TYPE_U64 ? ((gsize)1 << value_type) : 0;
				if (fu_struct_amd_afc_ifr_default_get_id(st_default) == 0 &&
				    width > 0 &&
				    length >= FU_STRUCT_AMD_AFC_IFR_DEFAULT_SIZE + width) {
					if (!fu_amd_afc_hii_read_value(
						op,
						length,
						FU_STRUCT_AMD_AFC_IFR_DEFAULT_SIZE,
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
