/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * nocheck:magic-defines=20 -- constants mirror the AFC format
 */

#include "config.h"

#include "fu-amd-afc-acpi-table.h"
#include "fu-amd-afc-bios-setting.h"
#include "fu-amd-afc-common.h"
#include "fu-amd-afc-config-entry.h"
#include "fu-amd-afc-config.h"
#include "fu-amd-afc-form.h"
#include "fu-amd-afc-option.h"
#include "fu-amd-afc-pending.h"
#include "fu-amd-afc-setting.h"
#include "fu-amd-afc-state.h"
#include "fu-amd-afc-struct.h"
#include "fu-amd-afc-varstore.h"

struct _FuAmdAfcState {
	FuFirmware parent_instance;
	FuContext *ctx;
	GPtrArray *strings;   /* gchar *, index is HII string ID */
	GPtrArray *forms;     /* FuAmdAfcForm * */
	GPtrArray *varstores; /* FuAmdAfcVarstore * */
	GPtrArray *settings;  /* FuAmdAfcSetting * */
	GPtrArray *pendings;  /* FuAmdAfcPending * */
	gchar *formset_name;
	gchar *language;
	guint16 revision;
};

enum { PROP_0, PROP_CONTEXT, PROP_LAST };

G_DEFINE_TYPE(FuAmdAfcState, fu_amd_afc_state, FU_TYPE_FIRMWARE)

static void
fu_amd_afc_state_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuAmdAfcState *self = FU_AMD_AFC_STATE(firmware);
	if (self->strings->len > 0) {
		g_autoptr(XbBuilderNode) bc = xb_builder_node_insert(bn, "strings", NULL);
		for (guint i = 0; i < self->strings->len; i++) {
			const gchar *tmp = g_ptr_array_index(self->strings, i);
			fu_xmlb_builder_insert_kv(bc, "string", tmp);
		}
	}
	for (guint i = 0; i < self->forms->len; i++) {
		FuAmdAfcForm *form = g_ptr_array_index(self->forms, i);
		g_autoptr(XbBuilderNode) bc = xb_builder_node_insert(bn, "form", NULL);
		fu_amd_afc_form_export(form, flags, bc);
	}
	for (guint i = 0; i < self->varstores->len; i++) {
		FuAmdAfcVarstore *varstore = g_ptr_array_index(self->varstores, i);
		g_autoptr(XbBuilderNode) bc = xb_builder_node_insert(bn, "varstore", NULL);
		fu_amd_afc_varstore_export(varstore, flags, bc);
	}
	for (guint i = 0; i < self->settings->len; i++) {
		FuAmdAfcSetting *setting = g_ptr_array_index(self->settings, i);
		g_autoptr(XbBuilderNode) bc = xb_builder_node_insert(bn, "setting", NULL);
		fu_amd_afc_setting_export(setting, flags, bc);
	}
	for (guint i = 0; i < self->pendings->len; i++) {
		FuAmdAfcPending *pending = g_ptr_array_index(self->pendings, i);
		g_autoptr(XbBuilderNode) bc = xb_builder_node_insert(bn, "pending", NULL);
		fu_amd_afc_pending_export(pending, flags, bc);
	}
	if (self->formset_name != NULL)
		fu_xmlb_builder_insert_kv(bn, "formset_name", self->formset_name);
	if (self->language != NULL)
		fu_xmlb_builder_insert_kv(bn, "language", self->language);
	if (self->revision != 0)
		fu_xmlb_builder_insert_kx(bn, "revision", self->revision);
}

FuContext *
fu_amd_afc_state_get_context(FuAmdAfcState *self)
{
	return self->ctx;
}

FuAmdAfcSetting *
fu_amd_afc_state_get_setting(FuAmdAfcState *self, guint setting_idx, GError **error)
{
	if (setting_idx >= self->settings->len) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "invalid AFC setting index");
		return NULL;
	}
	return g_ptr_array_index(self->settings, setting_idx);
}

static gboolean
fu_amd_afc_state_read_value(const guint8 *buf,
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
fu_amd_afc_state_read_value2(GBytes *blob,
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
	if (!fu_memcpy_safe(tmp,
			    sizeof(tmp),
			    0,
			    g_bytes_get_data(blob, NULL),
			    g_bytes_get_size(blob),
			    offset,
			    width,
			    error))
		return FALSE;
	return fu_memread_uint64_safe(tmp, sizeof(tmp), 0, value, G_LITTLE_ENDIAN, error);
}

static const gchar *
fu_amd_afc_state_strings_get_string(FuAmdAfcState *self, guint16 id, GError **error)
{
	const gchar *value = NULL;
	if (id < self->strings->len)
		value = g_ptr_array_index(self->strings, id);
	if (value == NULL)
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "AFC string %u was not found",
			    id);
	return value;
}

static gboolean
fu_amd_afc_state_parse_option(FuAmdAfcState *self,
			      guint setting_idx,
			      GBytes *blob,
			      gsize offset,
			      GError **error)
{
	FuAmdAfcSetting *setting = g_ptr_array_index(self->settings, setting_idx);
	const gchar *name;
	guint8 value_type;
	gsize width;
	g_autoptr(FuStructAmdAfcIfrOption) st_option = NULL;
	g_autoptr(FuAmdAfcOption) option = NULL;

	st_option = fu_struct_amd_afc_ifr_option_parse_bytes(blob, offset, error);
	if (st_option == NULL)
		return FALSE;
	value_type = fu_struct_amd_afc_ifr_option_get_value_type(st_option);
	width = value_type <= IFR_TYPE_U64 ? ((gsize)1 << value_type) : 0;
	if (setting->kind != FU_AMD_AFC_SETTING_KIND_ENUMERATION || width == 0)
		return TRUE;
	option = g_new0(FuAmdAfcOption, 1);
	if (!fu_amd_afc_state_read_value2(blob,
					  offset + FU_STRUCT_AMD_AFC_IFR_OPTION_SIZE,
					  width,
					  &option->value,
					  error))
		return FALSE;
	name = fu_amd_afc_state_strings_get_string(
	    self,
	    fu_struct_amd_afc_ifr_option_get_string_id(st_option),
	    error);
	if (name == NULL)
		return FALSE;
	option->name = g_strdup(name);
	if ((fu_struct_amd_afc_ifr_option_get_flags(st_option) & IFR_OPTION_DEFAULT) != 0) {
		setting->has_default = TRUE;
		setting->default_value = option->value;
	}
	g_ptr_array_add(setting->options, g_steal_pointer(&option));
	return TRUE;
}
static gboolean
fu_amd_afc_state_parse_default(FuAmdAfcState *self,
			       guint setting_idx,
			       GBytes *blob,
			       gsize offset,
			       GError **error)
{
	g_autoptr(FuStructAmdAfcIfrDefault) st_default = NULL;
	FuAmdAfcSetting *setting = g_ptr_array_index(self->settings, setting_idx);
	guint8 value_type;
	gsize width;

	st_default = fu_struct_amd_afc_ifr_default_parse_bytes(blob, offset, error);
	if (st_default == NULL)
		return FALSE;
	value_type = fu_struct_amd_afc_ifr_default_get_value_type(st_default);
	width = value_type <= IFR_TYPE_U64 ? ((gsize)1 << value_type) : 0;
	if (fu_struct_amd_afc_ifr_default_get_id(st_default) == 0 && width > 0 &&
	    g_bytes_get_size(blob) >= FU_STRUCT_AMD_AFC_IFR_DEFAULT_SIZE + width) {
		if (!fu_amd_afc_state_read_value2(blob,
						  offset + FU_STRUCT_AMD_AFC_IFR_DEFAULT_SIZE,
						  width,
						  &setting->default_value,
						  error))
			return FALSE;
		setting->has_default = TRUE;
	}
	return TRUE;
}

static gboolean
fu_amd_afc_state_current_value(FuAmdAfcState *self,
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
		if (!fu_amd_afc_state_read_value(buf, bufsz, offset, width, value, NULL))
			return FALSE;
		if (bitfield)
			*value = (*value >> shift) & ((((guint64)1) << bits) - 1);
		return TRUE;
	}
	return FALSE;
}

static gboolean
fu_amd_afc_state_new_setting(FuAmdAfcState *self,
			     guint form_idx,
			     GBytes *blob,
			     gsize offset,
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
	guint8 setting_kind = 0;
	guint16 prompt;
	guint16 varstore_id;
	guint16 varstore_offset;
	gsize width;
	gsize data_width;

	st_question = fu_struct_amd_afc_ifr_question_parse_bytes(blob, offset, error);
	if (st_question == NULL)
		return FALSE;
	flags = fu_struct_amd_afc_ifr_question_get_value_type(st_question);
	prompt = fu_struct_amd_afc_ifr_question_get_prompt(st_question);
	varstore_id = fu_struct_amd_afc_ifr_question_get_varstore_id(st_question);
	varstore_offset = fu_struct_amd_afc_ifr_question_get_varstore_offset(st_question);
	bits = bitfield ? flags & IFR_BITFIELD_WIDTH_MASK : 0;
	width = bitfield ? ((gsize)bits + 7) / 8 : ((gsize)1 << (flags & IFR_TYPE_WIDTH_MASK));
	data_width = bitfield ? 4 : width;

	setting->paths = g_ptr_array_new_with_free_func(g_free);
	setting->options = g_ptr_array_new_with_free_func((GDestroyNotify)fu_amd_afc_option_free);
	if ((bitfield && (bits == 0 || bits > 32)) || width > sizeof(guint64)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid AFC question width");
		return FALSE;
	}
	display_name = fu_amd_afc_state_strings_get_string(self, prompt, error);
	if (display_name == NULL || self->language == NULL)
		return FALSE;
	setting->display_name = g_strdup(display_name);
	setting->language = g_strdup(self->language);
	setting->question_id = fu_struct_amd_afc_ifr_question_get_id(st_question);
	setting->question_flags = fu_struct_amd_afc_ifr_question_get_flags(st_question);
	if (!fu_memread_uint8_safe(g_bytes_get_data(blob, NULL),
				   g_bytes_get_size(blob),
				   offset,
				   &setting_kind,
				   error))
		return FALSE;
	setting->kind = setting_kind == FU_AMD_AFC_IFR_OPCODE_ONE_OF
			    ? FU_AMD_AFC_SETTING_KIND_ENUMERATION
			    : FU_AMD_AFC_SETTING_KIND_INTEGER;
	if (setting->kind == FU_AMD_AFC_SETTING_KIND_INTEGER) {
		if (!fu_amd_afc_state_read_value2(blob,
						  offset + FU_STRUCT_AMD_AFC_IFR_QUESTION_SIZE,
						  data_width,
						  &setting->minimum,
						  error))
			return FALSE;
		if (!fu_amd_afc_state_read_value2(blob,
						  offset + FU_STRUCT_AMD_AFC_IFR_QUESTION_SIZE +
						      data_width,
						  data_width,
						  &setting->maximum,
						  error))
			return FALSE;
		if (!fu_amd_afc_state_read_value2(blob,
						  offset + FU_STRUCT_AMD_AFC_IFR_QUESTION_SIZE +
						      (2 * data_width),
						  data_width,
						  &setting->step,
						  error))
			return FALSE;
		if (setting->step == 0)
			setting->step = 1;
	}
	setting->has_current = fu_amd_afc_state_current_value(self,
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

static void
fu_amd_afc_state_scope_clear_bitfield(GArray *scopes, gboolean has_scope, gboolean *bitfield)
{
	*bitfield = FALSE;
	if (has_scope && scopes->len > 0)
		g_array_index(scopes, FuAmdAfcScope, scopes->len - 1).bitfield = FALSE;
}

static gboolean
fu_amd_afc_state_parse_question(FuAmdAfcState *self,
				guint form_idx,
				GBytes *blob,
				gsize offset,
				gboolean has_scope,
				GArray *scopes,
				gint *setting_idx,
				gboolean *bitfield,
				GError **error)
{
	gboolean question_bitfield = *bitfield;
	gsize width;
	guint16 prompt;
	g_autoptr(FuStructAmdAfcIfrQuestion) st_question = NULL;

	st_question = fu_struct_amd_afc_ifr_question_parse_bytes(blob, offset, error);
	if (st_question == NULL)
		return FALSE;
	width = question_bitfield
		    ? 4
		    : ((gsize)1 << (fu_struct_amd_afc_ifr_question_get_value_type(st_question) &
				    IFR_TYPE_WIDTH_MASK));
	prompt = fu_struct_amd_afc_ifr_question_get_prompt(st_question);
	if (width <= sizeof(guint64) &&
	    g_bytes_get_size(blob) >=
		FU_STRUCT_AMD_AFC_IFR_QUESTION_SIZE + (IFR_NUMERIC_VALUE_COUNT * width)) {
		if (prompt == 0) {
			if (has_scope)
				*setting_idx = -1;
		} else {
			guint new_idx = 0;
			if (!fu_amd_afc_state_new_setting(self,
							  form_idx,
							  blob,
							  offset,
							  question_bitfield,
							  &new_idx,
							  error))
				return FALSE;
			if (has_scope)
				*setting_idx = new_idx;
		}
	}
	if (question_bitfield)
		fu_amd_afc_state_scope_clear_bitfield(scopes, has_scope, bitfield);
	return TRUE;
}

/* nocheck:name -- "form set" is a UEFI IFR opcode, not a setter */
static gboolean
fu_amd_afc_state_parse_form_set(FuAmdAfcState *self, GBytes *blob, gsize offset, GError **error)
{
	g_autoptr(FuStructAmdAfcIfrFormSet) st_form_set =
	    fu_struct_amd_afc_ifr_form_set_parse_bytes(blob, offset, error);
	const gchar *name;

	if (st_form_set == NULL)
		return FALSE;
	name = fu_amd_afc_state_strings_get_string(
	    self,
	    fu_struct_amd_afc_ifr_form_set_get_title(st_form_set),
	    error);
	if (name == NULL)
		return FALSE;
	g_set_str(&self->formset_name, name);
	return TRUE;
}

static gboolean
fu_amd_afc_state_parse_form(FuAmdAfcState *self,
			    GBytes *blob,
			    gsize offset,
			    gint *form_idx,
			    gint *setting_idx,
			    GError **error)
{
	g_autoptr(FuAmdAfcForm) form = g_new0(FuAmdAfcForm, 1);
	g_autoptr(FuStructAmdAfcIfrForm) st_form = NULL;
	const gchar *name;

	st_form = fu_struct_amd_afc_ifr_form_parse_bytes(blob, offset, error);
	if (st_form == NULL)
		return FALSE;
	form->id = fu_struct_amd_afc_ifr_form_get_id(st_form);
	name = fu_amd_afc_state_strings_get_string(self,
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

static gboolean
fu_amd_afc_state_parse_ref(FuAmdAfcState *self,
			   guint form_idx,
			   GBytes *blob,
			   gsize offset,
			   GError **error)
{
	FuAmdAfcForm *form = g_ptr_array_index(self->forms, form_idx);
	guint16 ref;
	g_autoptr(FuStructAmdAfcIfrRef) st_ref = NULL;

	st_ref = fu_struct_amd_afc_ifr_ref_parse_bytes(blob, offset, error);
	if (st_ref == NULL)
		return FALSE;
	ref = fu_struct_amd_afc_ifr_ref_get_form_id(st_ref);
	g_array_append_val(form->refs, ref);
	return TRUE;
}

static gboolean
fu_amd_afc_state_parse_guid(GBytes *blob, gsize offset, gboolean *bitfield, GError **error)
{
	static const fwupd_guid_t bit_varstore_guid = {
	    /* nocheck:magic */
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
	    0x1d,
	};
	g_autoptr(FuStructAmdAfcIfrGuid) st_guid = NULL;

	st_guid = fu_struct_amd_afc_ifr_guid_parse_bytes(blob, offset, error);
	if (st_guid == NULL)
		return FALSE;
	*bitfield = memcmp(fu_struct_amd_afc_ifr_guid_get_guid(st_guid),
			   bit_varstore_guid,
			   sizeof(bit_varstore_guid)) == 0;
	return TRUE;
}

static gboolean
fu_amd_afc_state_parse_forms(FuAmdAfcState *self, GBytes *blob, gsize offset, GError **error)
{
	guint32 package_length = 0;
	g_autoptr(FuStructAmdAfcHiiPackageHeader) st_hdr = NULL;
	g_autoptr(GArray) scopes = g_array_new(FALSE, FALSE, sizeof(FuAmdAfcScope));
	gint form_idx = -1;
	gint setting_idx = -1;
	gboolean bitfield = FALSE;
	gsize bufsz = 0;
	gsize package_bufsz = 0;
	const guint8 *buf = g_bytes_get_data(blob, &bufsz);

	st_hdr = fu_struct_amd_afc_hii_package_header_parse_bytes(blob, offset, error);
	if (st_hdr == NULL)
		return FALSE;
	package_length = fu_struct_amd_afc_hii_package_header_get_length(st_hdr);
	if (fu_struct_amd_afc_hii_package_header_get_kind(st_hdr) !=
	    FU_AMD_AFC_HII_PACKAGE_KIND_FORMS) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid AFC forms package");
		return FALSE;
	}
	if (!fu_memchk_read(g_bytes_get_size(blob), offset, package_length, error)) {
		g_prefix_error_literal(error, "AFC forms package is truncated: ");
		return FALSE;
	}
	package_bufsz = offset + package_length;
	offset += FU_STRUCT_AMD_AFC_HII_PACKAGE_HEADER_SIZE;
	while (offset < package_bufsz) {
		guint8 opcode = 0;
		guint8 op_length = 0;
		guint length;
		gboolean has_scope;

		if (!fu_memread_uint8_safe(buf, bufsz, offset, &opcode, error))
			return FALSE;
		if (!fu_memread_uint8_safe(buf, bufsz, offset + 1, &op_length, error))
			return FALSE;
		length = op_length & IFR_OP_LENGTH_MASK;
		has_scope = (op_length & IFR_OP_SCOPE) != 0;
		if (length < 2) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "invalid AFC opcode length");
			return FALSE;
		}
		if (opcode == FU_AMD_AFC_IFR_OPCODE_END) {
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
			if (scopes->len >= 64) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_DATA,
						    "AFC scopes are too deep");
				return FALSE;
			}
			g_array_append_val(scopes, scope);
		}
		switch (opcode) {
		case FU_AMD_AFC_IFR_OPCODE_FORM_SET:
			if (length >= FU_STRUCT_AMD_AFC_IFR_FORM_SET_SIZE) {
				if (!fu_amd_afc_state_parse_form_set(self, blob, offset, error))
					return FALSE;
			}
			break;
		case FU_AMD_AFC_IFR_OPCODE_FORM:
			if (length >= FU_STRUCT_AMD_AFC_IFR_FORM_SIZE) {
				if (!fu_amd_afc_state_parse_form(self,
								 blob,
								 offset,
								 &form_idx,
								 &setting_idx,
								 error))
					return FALSE;
			}
			break;
		case FU_AMD_AFC_IFR_OPCODE_REF:
			if (form_idx >= 0 && length >= FU_STRUCT_AMD_AFC_IFR_REF_SIZE) {
				if (!fu_amd_afc_state_parse_ref(self,
								form_idx,
								blob,
								offset,
								error))
					return FALSE;
			}
			break;
		case FU_AMD_AFC_IFR_OPCODE_GUID:
			if (length >= FU_STRUCT_AMD_AFC_IFR_GUID_SIZE) {
				if (!fu_amd_afc_state_parse_guid(blob, offset, &bitfield, error))
					return FALSE;
			}
			break;
		case FU_AMD_AFC_IFR_OPCODE_CHECKBOX:
			if (bitfield)
				fu_amd_afc_state_scope_clear_bitfield(scopes, has_scope, &bitfield);
			break;
		case FU_AMD_AFC_IFR_OPCODE_ONE_OF:
		case FU_AMD_AFC_IFR_OPCODE_NUMERIC:
			if (form_idx >= 0 && length >= FU_STRUCT_AMD_AFC_IFR_QUESTION_SIZE +
							   IFR_NUMERIC_VALUE_COUNT) {
				if (!fu_amd_afc_state_parse_question(self,
								     form_idx,
								     blob,
								     offset,
								     has_scope,
								     scopes,
								     &setting_idx,
								     &bitfield,
								     error))
					return FALSE;
			}
			break;
		case FU_AMD_AFC_IFR_OPCODE_ONE_OF_OPTION:
			if (setting_idx >= 0 && length > FU_STRUCT_AMD_AFC_IFR_OPTION_SIZE) {
				if (!fu_amd_afc_state_parse_option(self,
								   setting_idx,
								   blob,
								   offset,
								   error))
					return FALSE;
			}
			break;
		case FU_AMD_AFC_IFR_OPCODE_DEFAULT:
			if (setting_idx >= 0 && length > FU_STRUCT_AMD_AFC_IFR_DEFAULT_SIZE) {
				if (!fu_amd_afc_state_parse_default(self,
								    setting_idx,
								    blob,
								    offset,
								    error))
					return FALSE;
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

	/* success */
	return TRUE;
}

static gint
fu_amd_afc_state_find_form(FuAmdAfcState *self, guint16 id)
{
	for (guint i = 0; i < self->forms->len; i++) {
		FuAmdAfcForm *form = g_ptr_array_index(self->forms, i);
		if (form->id == id)
			return i;
	}
	return -1;
}

static gboolean
fu_amd_afc_state_assign_form_paths(FuAmdAfcState *self,
				   guint form_idx,
				   GPtrArray *path,
				   GError **error)
{
	FuAmdAfcForm *form = g_ptr_array_index(self->forms, form_idx);
	if (form->visited)
		return TRUE;
	if (path->len >= 32) {
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
			g_ptr_array_add(setting->paths, g_strdup(g_ptr_array_index(path, j)));
		g_ptr_array_add(setting->paths, g_strdup(setting->display_name));
	}
	for (guint i = 0; i < form->refs->len; i++) {
		guint16 id = g_array_index(form->refs, guint16, i);
		gint child = fu_amd_afc_state_find_form(self, id);
		if (child >= 0 && !fu_amd_afc_state_assign_form_paths(self, child, path, error))
			return FALSE;
	}
	g_ptr_array_remove_index(path, path->len - 1);
	return TRUE;
}
static gboolean
fu_amd_afc_state_parse_varstore(FuAmdAfcState *self, GBytes *blob, gsize *offset, GError **error)
{
	guint32 length;
	g_autoptr(FuAmdAfcVarstore) varstore = g_new0(FuAmdAfcVarstore, 1);
	g_autoptr(FuStructAmdAfcVarstoreHeader) st_hdr = NULL;

	st_hdr = fu_struct_amd_afc_varstore_header_parse_bytes(blob, *offset, error);
	if (st_hdr == NULL)
		return FALSE;
	length = fu_struct_amd_afc_varstore_header_get_length(st_hdr);
	if (length < FU_STRUCT_AMD_AFC_VARSTORE_HEADER_SIZE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid AFC varstore");
		return FALSE;
	}
	varstore->id = fu_struct_amd_afc_varstore_header_get_id(st_hdr);
	varstore->data =
	    fu_bytes_new_offset(blob,
				*offset + FU_STRUCT_AMD_AFC_VARSTORE_HEADER_SIZE +
				    fu_struct_amd_afc_varstore_header_get_name_size(st_hdr),
				fu_struct_amd_afc_varstore_header_get_data_size(st_hdr),
				error);
	if (varstore->data == NULL)
		return FALSE;
	g_ptr_array_add(self->varstores, g_steal_pointer(&varstore));
	*offset += length;
	return TRUE;
}

static gboolean
fu_amd_afc_state_parse_varstores(FuAmdAfcState *self, GBytes *blob, GError **error)
{
	gsize offset = 0;
	while (offset < g_bytes_get_size(blob)) {
		if (!fu_amd_afc_state_parse_varstore(self, blob, &offset, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_amd_afc_state_strings_parse_ucs2(const guint8 *buf,
				    gsize bufsz,
				    gsize *offset,
				    gchar **value,
				    GError **error)
{
	gsize offset_start = *offset;

	while (*offset < bufsz) {
		guint16 ch = 0;
		g_autoptr(GBytes) utf16 = NULL;
		if (!fu_memread_uint16_safe(buf, bufsz, *offset, &ch, G_LITTLE_ENDIAN, error))
			return FALSE;
		*offset += 2;
		if (ch != 0)
			continue;
		utf16 = g_bytes_new(buf + offset_start, *offset - offset_start - 2);
		*value = fu_utf16_to_utf8_bytes(utf16, G_LITTLE_ENDIAN, error);
		return *value != NULL;
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "unterminated AFC UCS-2 string");
	return FALSE;
}

static gboolean
fu_amd_afc_state_strings_store(FuAmdAfcState *self, guint id, const gchar *value, GError **error)
{
	if (id > G_MAXUINT16) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "too many AFC strings");
		return FALSE;
	}
	while (self->strings->len <= id)
		g_ptr_array_add(self->strings, NULL);
	g_free(g_ptr_array_index(self->strings, id));
	g_ptr_array_index(self->strings, id) = g_strdup(value);
	return TRUE;
}

static gboolean
fu_amd_afc_state_strings_parse_ascii(FuAmdAfcState *self,
				     const guint8 *buf,
				     gsize bufsz,
				     gsize *offset,
				     guint16 count,
				     guint *id,
				     GError **error)
{
	for (guint i = 0; i < count; i++) {
		gsize value_len;
		g_autoptr(GString) value = fu_memread_string_safe(buf, bufsz, *offset, error);
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
		if (!fu_amd_afc_state_strings_store(self, *id, value->str, error))
			return FALSE;
		(*id)++;
		*offset += value_len + 1;
	}
	return TRUE;
}

static gboolean
fu_amd_afc_state_strings_parse_ucs2_multiple(FuAmdAfcState *self,
					     const guint8 *buf,
					     gsize bufsz,
					     gsize *offset,
					     guint16 count,
					     guint *id,
					     GError **error)
{
	for (guint i = 0; i < count; i++) {
		g_autofree gchar *value = NULL;
		if (!fu_amd_afc_state_strings_parse_ucs2(buf, bufsz, offset, &value, error))
			return FALSE;
		if (!fu_amd_afc_state_strings_store(self, *id, value, error))
			return FALSE;
		(*id)++;
	}
	return TRUE;
}

gboolean
fu_amd_afc_state_strings_parse_package(FuAmdAfcState *self,
				       GBytes *blob,
				       gsize offset,
				       GError **error)
{
	gsize bufsz = 0;
	gsize package_bufsz;
	guint id = 1;
	guint32 header_size = 0;
	guint32 package_length = 0;
	const guint8 *buf = g_bytes_get_data(blob, &bufsz);
	g_autoptr(FuStructAmdAfcHiiPackageHeader) st_hdr = NULL;
	g_autoptr(GString) language = NULL;

	st_hdr = fu_struct_amd_afc_hii_package_header_parse_bytes(blob, offset, error);
	if (st_hdr == NULL)
		return FALSE;
	if (fu_struct_amd_afc_hii_package_header_get_kind(st_hdr) !=
	    FU_AMD_AFC_HII_PACKAGE_KIND_STRINGS) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid AFC string package");
		return FALSE;
	}
	package_length = fu_struct_amd_afc_hii_package_header_get_length(st_hdr);
	if (package_length > bufsz - offset) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "AFC string package is too small");
		return FALSE;
	}
	if (!fu_memchk_read(bufsz, offset, package_length, error)) {
		g_prefix_error_literal(error, "AFC string package is truncated: ");
		return FALSE;
	}
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    offset + FU_STRUCT_AMD_AFC_HII_PACKAGE_HEADER_SIZE,
				    &header_size,
				    G_LITTLE_ENDIAN,
				    error)) {
		return FALSE;
	}
	if (header_size <= HII_STRING_LANGUAGE_OFFSET) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid AFC string header size");
		return FALSE;
	}
	language = fu_memread_string_safe(buf, bufsz, offset + HII_STRING_LANGUAGE_OFFSET, error);
	if (language == NULL)
		return FALSE;
	g_set_str(&self->language, language->str);
	if (g_strcmp0(self->language, "en") != 0 && g_strcmp0(self->language, "en-US") != 0 &&
	    g_strcmp0(self->language, "eng") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "AFC language %s is unsupported",
			    self->language);
		return FALSE;
	}
	package_bufsz = offset + package_length;
	offset += header_size;
	while (offset < package_bufsz) {
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
			if (!fu_amd_afc_state_strings_parse_ascii(self,
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
			if (!fu_amd_afc_state_strings_parse_ucs2_multiple(self,
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
			value = fu_amd_afc_state_strings_get_string(self, source, error);
			if (value == NULL)
				return FALSE;
			if (!fu_amd_afc_state_strings_store(self, id++, value, error))
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
			if (!fu_memchk_read(bufsz, offset, length - 1, error)) {
				g_prefix_error_literal(error,
						       "AFC extended string block is truncated: ");
				return FALSE;
			}
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

static gboolean
fu_amd_afc_state_parse_blob(FuAmdAfcState *self, GBytes *blob, gsize offset, GError **error)
{
	guint8 checksum = 0;
	guint32 length;
	guint16 revision;
	guint32 forms_offset;
	guint32 strings_offset;
	guint32 varstores_offset;
	guint32 varstores_size;
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(blob, &bufsz);
	g_autoptr(FuStructAmdAfcEieHeader) st_hdr = NULL;
	g_autoptr(GBytes) blob_varstores = NULL;
	g_autoptr(GPtrArray) path = g_ptr_array_new_with_free_func(g_free);

	st_hdr = fu_struct_amd_afc_eie_header_parse_bytes(blob, offset, error);
	if (st_hdr == NULL)
		return FALSE;
	length = fu_struct_amd_afc_eie_header_get_length(st_hdr);
	revision = fu_struct_amd_afc_eie_header_get_revision(st_hdr);
	forms_offset = fu_struct_amd_afc_eie_header_get_forms_offset(st_hdr);
	strings_offset = fu_struct_amd_afc_eie_header_get_strings_offset(st_hdr);
	varstores_offset = fu_struct_amd_afc_eie_header_get_varstores_offset(st_hdr);
	varstores_size = fu_struct_amd_afc_eie_header_get_varstores_size(st_hdr);
	if (length < FU_STRUCT_AMD_AFC_EIE_HEADER_SIZE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid AFC blob length");
		return FALSE;
	}
	if (revision != FU_STRUCT_AMD_AFC_CONFIG_HEADER_DEFAULT_REVISION &&
	    (revision >> AFC_REVISION_MAJOR_SHIFT) != AFC_REVISION_MAJOR) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid AFC header revision");
		return FALSE;
	}
	if (!fu_sum8_safe(buf, bufsz, offset, length, &checksum, error))
		return FALSE;
	if (checksum != 0) {
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
	g_set_str(&self->formset_name, NULL);
	g_set_str(&self->language, NULL);

	if (!fu_amd_afc_state_strings_parse_package(self,
						    blob,
						    offset + FU_STRUCT_AMD_AFC_EIE_HEADER_SIZE +
							strings_offset,
						    error))
		return FALSE;
	blob_varstores =
	    fu_bytes_new_offset(blob,
				offset + FU_STRUCT_AMD_AFC_EIE_HEADER_SIZE + varstores_offset,
				varstores_size,
				error);
	if (blob_varstores == NULL)
		return FALSE;
	if (!fu_amd_afc_state_parse_varstores(self, blob_varstores, error))
		return FALSE;
	if (!fu_amd_afc_state_parse_forms(self,
					  blob,
					  offset + FU_STRUCT_AMD_AFC_EIE_HEADER_SIZE + forms_offset,
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
		if (!fu_amd_afc_state_assign_form_paths(self, i, path, error)) {
			g_prefix_error(error, "cannot read form %u: ", i);
			return FALSE;
		}
	}

	/* success */
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
		st_hdr = fu_struct_amd_afc_eie_header_parse_bytes(bytes, offset, error);
		if (st_hdr == NULL)
			return FALSE;
		if (!fu_amd_afc_state_parse_blob(self, bytes, offset, error)) {
			g_prefix_error(error, "failed to parse blob at 0x%zx: ", offset);
			return FALSE;
		}
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

	/* success */
	return TRUE;
}

gboolean
fu_amd_afc_state_parse_table(FuAmdAfcState *self, GBytes *bytes, GError **error)
{
	g_autoptr(FuAmdAfcAcpiTable) table = fu_amd_afc_acpi_table_new();
	g_autoptr(FuInputStream) payload_stream = NULL;
	g_autoptr(GBytes) payload = NULL;

	g_return_val_if_fail(self != NULL, FALSE);
	g_return_val_if_fail(bytes != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fu_firmware_parse_bytes(FU_FIRMWARE(table),
				     bytes,
				     0,
				     FU_FIRMWARE_PARSE_FLAG_NONE,
				     error))
		return FALSE;

	payload_stream = fu_acpi_table_get_payload(FU_ACPI_TABLE(table), error);
	if (payload_stream == NULL)
		return FALSE;
	payload = fu_input_stream_read_bytes(payload_stream, 0, G_MAXSIZE, NULL, error);
	if (payload == NULL)
		return FALSE;
	return fu_amd_afc_state_parse_payload(self, payload, error);
}

static gboolean
fu_amd_afc_state_parse(FuFirmware *firmware,
		       FuInputStream *stream,
		       FuFirmwareParseFlags flags,
		       GError **error)
{
	FuAmdAfcState *self = FU_AMD_AFC_STATE(firmware);
	g_autoptr(FuAmdAfcAcpiTable) table = fu_amd_afc_acpi_table_new();
	g_autoptr(FuInputStream) payload_stream = NULL;
	g_autoptr(GBytes) payload = NULL;

	if (!fu_firmware_parse_stream(FU_FIRMWARE(table), stream, 0, flags, error))
		return FALSE;

	payload_stream = fu_acpi_table_get_payload(FU_ACPI_TABLE(table), error);
	if (payload_stream == NULL)
		return FALSE;
	payload = fu_input_stream_read_bytes(payload_stream, 0, G_MAXSIZE, NULL, error);
	if (payload == NULL)
		return FALSE;
	return fu_amd_afc_state_parse_payload(self, payload, error);
}

static gboolean
fu_amd_afc_state_write_pending(FuAmdAfcState *self, GError **error)
{
	FuEfivars *efivars = fu_context_get_efivars(self->ctx);
	g_autoptr(GByteArray) strings = g_byte_array_new();
	g_autoptr(GByteArray) entries_buf = g_byte_array_new();
	g_autoptr(GPtrArray) entries = NULL;
	g_autoptr(FuStructAmdAfcConfigHeader) st_hdr = fu_struct_amd_afc_config_header_new();
	g_autoptr(GBytes) existing = NULL;
	g_autoptr(GBytes) verify = NULL;
	g_autoptr(GBytes) expected = NULL;
	FuEfiVariableAttrs attrs = 0;
	guint16 string_count = 0;

	if (fu_efivars_exists(efivars, AFC_EFIVAR_GUID, AFC_EFIVAR_NAME)) {
		existing = fu_efivars_get_data_bytes(efivars,
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
	for (guint i = 0; i < self->pendings->len; i++) {
		FuAmdAfcPending *pending = g_ptr_array_index(self->pendings, i);
		FuAmdAfcSetting *setting = g_ptr_array_index(self->settings, pending->setting);
		FuAmdAfcConfigEntry *entry = NULL;
		for (guint j = 0; j < entries->len; j++) {
			FuAmdAfcConfigEntry *entry_tmp = g_ptr_array_index(entries, j);
			if (fu_amd_afc_config_paths_equal(entry_tmp->path, setting->paths)) {
				entry = entry_tmp;
				break;
			}
		}
		if (entry == NULL) {
			entry = g_new0(FuAmdAfcConfigEntry, 1);
			entry->path = g_ptr_array_new_with_free_func(g_free);
			for (guint j = 0; j < setting->paths->len; j++) {
				const gchar *path = g_ptr_array_index(setting->paths, j);
				g_ptr_array_add(entry->path, g_strdup(path));
			}
			g_ptr_array_add(entries, entry);
		}
		g_set_str(&entry->value, pending->value);
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
	fu_struct_amd_afc_config_header_set_revision(st_hdr, self->revision);
	fu_struct_amd_afc_config_header_set_strings_size(st_hdr, strings->len);
	fu_struct_amd_afc_config_header_set_entry_count(st_hdr, entries->len);
	g_byte_array_append(st_hdr->buf, strings->data, strings->len);
	g_byte_array_append(st_hdr->buf, entries_buf->data, entries_buf->len);
	fu_struct_amd_afc_config_header_set_checksum(
	    st_hdr,
	    (guint8)(0U - fu_sum8(st_hdr->buf->data, st_hdr->buf->len)));
	if (!fu_efivars_set_data(efivars,
				 AFC_EFIVAR_GUID,
				 AFC_EFIVAR_NAME,
				 st_hdr->buf->data,
				 st_hdr->buf->len,
				 AFC_EFIVAR_ATTRS,
				 error))
		return FALSE;
	verify =
	    fu_efivars_get_data_bytes(efivars, AFC_EFIVAR_GUID, AFC_EFIVAR_NAME, &attrs, error);
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

gboolean
fu_amd_afc_state_store(FuAmdAfcState *self, guint index, const gchar *value, GError **error)
{
	FuAmdAfcSetting *setting;
	g_autofree gchar *encoded = NULL;
	gint existing = -1;

	setting = fu_amd_afc_state_get_setting(self, index, error);
	if (setting == NULL)
		return FALSE;
	if (setting->kind == FU_AMD_AFC_SETTING_KIND_ENUMERATION) {
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
	for (guint i = 0; i < self->pendings->len; i++) {
		FuAmdAfcPending *pending = g_ptr_array_index(self->pendings, i);
		if (pending->setting == index) {
			existing = i;
			break;
		}
	}
	if (existing >= 0) {
		FuAmdAfcPending *pending = g_ptr_array_index(self->pendings, existing);
		gchar *old = pending->value;
		pending->value = g_steal_pointer(&encoded);
		if (!fu_amd_afc_state_write_pending(self, error)) {
			g_free(pending->value);
			pending->value = old;
			return FALSE;
		}
		g_free(old);
	} else {
		FuAmdAfcPending *pending = g_new0(FuAmdAfcPending, 1);
		pending->setting = index;
		pending->value = g_steal_pointer(&encoded);
		g_ptr_array_add(self->pendings, pending);
		if (!fu_amd_afc_state_write_pending(self, error)) {
			g_ptr_array_remove_index(self->pendings, self->pendings->len - 1);
			return FALSE;
		}
	}
	fu_context_add_flag(self->ctx, FU_CONTEXT_FLAG_PENDING_REBOOT);
	return TRUE;
}

static GByteArray *
fu_amd_afc_state_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) strings = g_byte_array_sized_new(128);
	g_autoptr(GByteArray) forms = g_byte_array_sized_new(128);
	g_autoptr(GByteArray) varstores = g_byte_array_sized_new(32);
	g_autoptr(GByteArray) buf = g_byte_array_sized_new(512);
	g_autoptr(FuAmdAfcAcpiTable) table = fu_amd_afc_acpi_table_new();
	g_autoptr(FuInputStream) table_payload = NULL;
	g_autoptr(FuStructAmdAfcEieHeader) st_eie = fu_struct_amd_afc_eie_header_new();
	g_autoptr(FuStructAmdAfcVarstoreHeader) st_varstore =
	    fu_struct_amd_afc_varstore_header_new();
	const gchar *values[] = {
	    " Setup ",
	    " Main ",
	    "  Feature  ",
	    "Disabled",
	    "Enabled",
	    "   ",
	    "Sibling",
	};
	guint8 strings_end = 0;
	guint8 op_form_set[23] = {
	    FU_AMD_AFC_IFR_OPCODE_FORM_SET,
	    sizeof(op_form_set) | IFR_OP_SCOPE,
	    [18] = 1,
	}; /* nocheck:magic */
	guint8 op_form[6] = {
	    FU_AMD_AFC_IFR_OPCODE_FORM,
	    sizeof(op_form) | IFR_OP_SCOPE,
	    0x10,
	    0,
	    2,
	    0,
	}; /* nocheck:magic */
	guint8 op_one_of[17] = {
	    FU_AMD_AFC_IFR_OPCODE_ONE_OF,
	    sizeof(op_one_of) | IFR_OP_SCOPE,
	    3,
	    0,
	    0,
	    0,
	    0x20,
	    0,
	    7,
	    0,
	}; /* nocheck:magic */
	guint8 op_form_sibling[6] = {
	    FU_AMD_AFC_IFR_OPCODE_FORM,
	    sizeof(op_form_sibling) | IFR_OP_SCOPE,
	    0x11,
	    0,
	    6,
	    0,
	}; /* nocheck:magic */
	guint8 op_one_of_sibling[17] = {
	    FU_AMD_AFC_IFR_OPCODE_ONE_OF,
	    sizeof(op_one_of_sibling) | IFR_OP_SCOPE,
	    7,
	    0,
	    0,
	    0,
	    0x21,
	    0,
	    7,
	    0,
	}; /* nocheck:magic */
	guint8 op_disabled[7] = {
	    FU_AMD_AFC_IFR_OPCODE_ONE_OF_OPTION,
	    sizeof(op_disabled),
	    4,
	    0,
	    0x10,
	    0,
	    0,
	}; /* nocheck:magic */
	guint8 op_enabled[7] = {
	    FU_AMD_AFC_IFR_OPCODE_ONE_OF_OPTION,
	    sizeof(op_enabled),
	    5,
	    0,
	    0,
	    0,
	    1,
	}; /* nocheck:magic */
	guint8 op_end[2] = {
	    FU_AMD_AFC_IFR_OPCODE_END,
	    sizeof(op_end),
	};
	guint8 checksum;

	/* english HII string package */
	fu_byte_array_set_size(strings, 50, 0x0);
	strings->data[HII_STRING_LANGUAGE_OFFSET + 0] = 'e';
	strings->data[HII_STRING_LANGUAGE_OFFSET + 1] = 'n';
	for (guint i = 0; i < G_N_ELEMENTS(values); i++) {
		guint8 block = i == G_N_ELEMENTS(values) - 1 ? 0x14 : 0x10;
		g_byte_array_append(strings, &block, 1);
		if (block == 0x14) {
			g_autoptr(GByteArray) value =
			    fu_utf8_to_utf16_byte_array(values[i],
							G_LITTLE_ENDIAN,
							FU_UTF_CONVERT_FLAG_APPEND_NUL,
							error);
			if (value == NULL)
				return NULL;
			g_byte_array_append(strings, value->data, value->len);
		} else {
			g_byte_array_append(strings,
					    (const guint8 *)values[i],
					    strlen(values[i]) + 1);
		}
	}
	g_byte_array_append(strings, &strings_end, 1);
	fu_memwrite_uint32(strings->data + 0, (0x04U << 24) | strings->len, G_LITTLE_ENDIAN);
	fu_memwrite_uint32(strings->data + 4, 50, G_LITTLE_ENDIAN);

	/* FormSet(Setup) -> Form(Main) -> OneOf(Feature) */
	fu_byte_array_set_size(forms, 4, 0x0);
	g_byte_array_append(forms, op_form_set, sizeof(op_form_set));
	g_byte_array_append(forms, op_form, sizeof(op_form));
	g_byte_array_append(forms, op_one_of, sizeof(op_one_of));
	g_byte_array_append(forms, op_disabled, sizeof(op_disabled));
	g_byte_array_append(forms, op_enabled, sizeof(op_enabled));
	for (guint i = 0; i < 2; i++)
		g_byte_array_append(forms, op_end, sizeof(op_end));
	g_byte_array_append(forms, op_form_sibling, sizeof(op_form_sibling));
	g_byte_array_append(forms, op_one_of_sibling, sizeof(op_one_of_sibling));
	g_byte_array_append(forms, op_disabled, sizeof(op_disabled));
	g_byte_array_append(forms, op_enabled, sizeof(op_enabled));
	for (guint i = 0; i < 3; i++)
		g_byte_array_append(forms, op_end, sizeof(op_end));
	fu_memwrite_uint32(forms->data + 0, (0x02U << 24) | forms->len, G_LITTLE_ENDIAN);

	/* varstore 7 contains the current value 0 */
	fu_struct_amd_afc_varstore_header_set_length(st_varstore,
						     FU_STRUCT_AMD_AFC_VARSTORE_HEADER_SIZE + 1);
	fu_struct_amd_afc_varstore_header_set_id(st_varstore, 7);
	fu_struct_amd_afc_varstore_header_set_data_size(st_varstore, 1);
	g_byte_array_append(varstores, st_varstore->buf->data, st_varstore->buf->len);
	g_byte_array_append(varstores, (const guint8 *)"\0", 1);

	/* AFC EIE header and payload */
	fu_struct_amd_afc_eie_header_set_length(st_eie,
						st_eie->buf->len + forms->len + strings->len +
						    varstores->len);
	fu_struct_amd_afc_eie_header_set_revision(st_eie, 0x1060);
	fu_struct_amd_afc_eie_header_set_forms_offset(st_eie, 0);
	fu_struct_amd_afc_eie_header_set_forms_size(st_eie, forms->len);
	fu_struct_amd_afc_eie_header_set_strings_offset(st_eie, forms->len);
	fu_struct_amd_afc_eie_header_set_strings_size(st_eie, strings->len);
	fu_struct_amd_afc_eie_header_set_varstores_offset(st_eie, forms->len + strings->len);
	fu_struct_amd_afc_eie_header_set_varstores_size(st_eie, varstores->len);
	checksum = fu_sum8(st_eie->buf->data, st_eie->buf->len) + fu_sum8(forms->data, forms->len) +
		   fu_sum8(strings->data, strings->len) + fu_sum8(varstores->data, varstores->len);
	fu_struct_amd_afc_eie_header_set_checksum(st_eie, (guint8)(0U - checksum));
	g_byte_array_append(buf, st_eie->buf->data, st_eie->buf->len);
	g_byte_array_append(buf, forms->data, forms->len);
	g_byte_array_append(buf, strings->data, strings->len);
	g_byte_array_append(buf, varstores->data, varstores->len);

	/* ACPI header */
	table_payload = fu_memory_input_stream_new_from_data(buf->data, buf->len, NULL);
	fu_acpi_table_set_payload(FU_ACPI_TABLE(table), table_payload);
	return fu_firmware_write_array(FU_FIRMWARE(table), error);
}

guint
fu_amd_afc_state_get_setting_count(FuAmdAfcState *self)
{
	g_return_val_if_fail(self != NULL, 0);
	return self->settings->len;
}

static gboolean
fu_amd_afc_state_add_bios_setting(FuAmdAfcState *self, guint setting_idx, GError **error)
{
	FuAmdAfcSetting *setting;
	const gchar *name;
	const gchar *separator;
	g_autofree gchar *id = NULL;
	g_autofree gchar *name_stripped = NULL;
	g_autofree gchar *parent = NULL;
	g_autofree gchar *path = NULL;
	g_autoptr(FuAmdAfcBiosSetting) attr = fu_amd_afc_bios_setting_new(self, setting_idx);

	setting = fu_amd_afc_state_get_setting(self, setting_idx, error);
	if (setting == NULL)
		return FALSE;
	path = fu_amd_afc_setting_build_name(setting);
	separator = strrchr(path, '|');
	if (separator != NULL) {
		name = separator + 1;
		parent = g_strndup(path, separator - path);
		g_strdelimit(parent, "|", '/');
	} else {
		name = path;
	}
	/* match the ID produced by the generic sysfs firmware-attributes loader */
	name_stripped = g_strdup(path);
	g_strdelimit(name_stripped, " ", '_');
	g_strdelimit(name_stripped, "|", '.');
	id = g_strdup_printf("com.amd-afc.%s.%04x", name_stripped, setting->question_id);
	/* the sysfs loader also ignores settings whose varstore is unavailable */
	if (!setting->has_current)
		return TRUE;
	fu_bios_setting_set_name(FU_BIOS_SETTING(attr), name);
	fwupd_bios_setting_set_parent(FWUPD_BIOS_SETTING(attr), parent);
	fu_bios_setting_set_id(FU_BIOS_SETTING(attr), id);
	fwupd_bios_setting_set_read_only(
	    FWUPD_BIOS_SETTING(attr),
	    (setting->question_flags & FU_AMD_AFC_QUESTION_FLAG_READONLY) != 0);
	if (setting->kind == FU_AMD_AFC_SETTING_KIND_ENUMERATION) {
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
		fwupd_bios_setting_set_lower_bound(FWUPD_BIOS_SETTING(attr), setting->minimum);
		fwupd_bios_setting_set_upper_bound(FWUPD_BIOS_SETTING(attr), setting->maximum);
		fwupd_bios_setting_set_scalar_increment(FWUPD_BIOS_SETTING(attr), setting->step);
	}
	if (!fwupd_bios_setting_setup(FWUPD_BIOS_SETTING(attr), error))
		return FALSE;
	return fu_context_add_bios_setting(self->ctx, FU_BIOS_SETTING(attr), error);
}

gboolean
fu_amd_afc_state_add_bios_settings(FuAmdAfcState *self, GError **error)
{
	for (guint i = 0; i < self->settings->len; i++) {
		if (!fu_amd_afc_state_add_bios_setting(self, i, error))
			return FALSE;
	}
	return TRUE;
}

static void
fu_amd_afc_state_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuAmdAfcState *self = FU_AMD_AFC_STATE(object);
	switch (prop_id) {
	case PROP_CONTEXT:
		g_value_set_object(value, self->ctx);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_amd_afc_state_set_property(GObject *object,
			      guint prop_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
	FuAmdAfcState *self = FU_AMD_AFC_STATE(object);
	switch (prop_id) {
	case PROP_CONTEXT:
		if (self->ctx != NULL)
			g_object_remove_weak_pointer(G_OBJECT(self->ctx), (gpointer *)&self->ctx);
		self->ctx = g_value_get_object(value);
		if (self->ctx != NULL)
			g_object_add_weak_pointer(G_OBJECT(self->ctx), (gpointer *)&self->ctx);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_amd_afc_state_finalize(GObject *object)
{
	FuAmdAfcState *self = FU_AMD_AFC_STATE(object);
	if (self->ctx != NULL)
		g_object_remove_weak_pointer(G_OBJECT(self->ctx), (gpointer *)&self->ctx);
	if (self->strings != NULL)
		g_ptr_array_unref(self->strings);
	if (self->forms != NULL)
		g_ptr_array_unref(self->forms);
	if (self->varstores != NULL)
		g_ptr_array_unref(self->varstores);
	if (self->settings != NULL)
		g_ptr_array_unref(self->settings);
	if (self->pendings != NULL)
		g_ptr_array_unref(self->pendings);
	g_free(self->formset_name);
	g_free(self->language);
	G_OBJECT_CLASS(fu_amd_afc_state_parent_class)->finalize(object);
}

static void
fu_amd_afc_state_init(FuAmdAfcState *self)
{
	self->strings = g_ptr_array_new_with_free_func(g_free);
	self->forms = g_ptr_array_new_with_free_func((GDestroyNotify)fu_amd_afc_form_free);
	self->varstores = g_ptr_array_new_with_free_func((GDestroyNotify)fu_amd_afc_varstore_free);
	self->settings = g_ptr_array_new_with_free_func((GDestroyNotify)fu_amd_afc_setting_free);
	self->pendings = g_ptr_array_new_with_free_func((GDestroyNotify)fu_amd_afc_pending_free);
}

static void
fu_amd_afc_state_class_init(FuAmdAfcStateClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);

	object_class->finalize = fu_amd_afc_state_finalize;
	object_class->get_property = fu_amd_afc_state_get_property;
	object_class->set_property = fu_amd_afc_state_set_property;

	firmware_class->parse = fu_amd_afc_state_parse;
	firmware_class->write = fu_amd_afc_state_write;
	firmware_class->export = fu_amd_afc_state_export;
	fu_firmware_set_size_max(firmware_class, 16 * FU_MB);

	pspec =
	    g_param_spec_object("context",
				NULL,
				NULL,
				FU_TYPE_CONTEXT,
				G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_CONTEXT, pspec);
}

FuAmdAfcState *
fu_amd_afc_state_new(FuContext *ctx)
{
	return g_object_new(FU_TYPE_AMD_AFC_STATE, "context", ctx, NULL);
}
