/*
 * Copyright 2026 Star Labs
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-coreboot-cfr-plugin.h"
#include "fu-coreboot-cfr-setting.h"
#include "fu-coreboot-cfr-struct.h"

#define COREBOOT_CFR_GUID	       "ceae4c1d-335b-4685-a4a0-fc4a94eea085"
#define COREBOOT_CFR_SETTINGS_VARIABLE "CorebootCfrSettings"

struct _FuCorebootCfrPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuCorebootCfrPlugin, fu_coreboot_cfr_plugin, FU_TYPE_PLUGIN)

static gchar *
fu_coreboot_cfr_plugin_read_string(const guint8 *buf,
				   gsize bufsz,
				   gsize *offset,
				   guint32 sz,
				   GError **error)
{
	gchar *str = NULL;

	if (sz == 0)
		return g_strdup("");

	if (*offset > bufsz || sz > bufsz - *offset) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "CFR settings metadata is truncated");
		return NULL;
	}

	if (buf[*offset + sz - 1] == '\0') {
		str = g_strndup((const gchar *)buf + *offset, sz - 1);
		*offset += sz;
		return str;
	}

	str = g_strndup((const gchar *)buf + *offset, sz);
	*offset += sz;
	return str;
}

static FwupdBiosSettingKind
fu_coreboot_cfr_plugin_setting_kind(FuCorebootCfrType kind)
{
	if (kind == FU_COREBOOT_CFR_TYPE_NUMBER)
		return FWUPD_BIOS_SETTING_KIND_INTEGER;
	return FWUPD_BIOS_SETTING_KIND_ENUMERATION;
}

static gboolean
fu_coreboot_cfr_plugin_add_bool_values(GPtrArray *possible_values,
				       GHashTable *value_map,
				       GHashTable *reverse_value_map)
{
	g_ptr_array_add(possible_values, g_strdup("Disabled"));
	g_ptr_array_add(possible_values, g_strdup("Enabled"));
	g_hash_table_insert(value_map, g_strdup("Disabled"), g_strdup("0"));
	g_hash_table_insert(value_map, g_strdup("Enabled"), g_strdup("1"));
	g_hash_table_insert(reverse_value_map, g_strdup("0"), g_strdup("Disabled"));
	g_hash_table_insert(reverse_value_map, g_strdup("1"), g_strdup("Enabled"));
	return TRUE;
}

static gboolean
fu_coreboot_cfr_plugin_parse_option(FuEfivars *efivars,
				    FuBiosSettings *bios_settings,
				    GInputStream *stream,
				    const guint8 *buf,
				    gsize bufsz,
				    gsize *offset,
				    GError **error)
{
	FuCorebootCfrApplyMethod runtime_apply_method = FU_COREBOOT_CFR_APPLY_METHOD_NONE;
	FuCorebootCfrType kind = FU_COREBOOT_CFR_TYPE_ENUM;
	FuEfiVariableAttrs attrs = 0;
	guint32 runtime_apply_id = 0;
	g_autoptr(FuStructCorebootCfrOptionRecord) st_option = NULL;
	g_autofree gchar *name = NULL;
	g_autofree gchar *ui_name = NULL;
	g_autofree gchar *help_text = NULL;
	g_autoptr(FwupdBiosSetting) setting = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GHashTable) reverse_value_map =
	    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	g_autoptr(GHashTable) value_map =
	    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	g_autoptr(GPtrArray) possible_values = g_ptr_array_new_with_free_func(g_free);

	st_option = fu_struct_coreboot_cfr_option_record_parse_stream(stream, *offset, error);
	if (st_option == NULL)
		return FALSE;
	if (!fu_size_checked_inc(offset, FU_STRUCT_COREBOOT_CFR_OPTION_RECORD_SIZE, error))
		return FALSE;

	name = fu_coreboot_cfr_plugin_read_string(
	    buf,
	    bufsz,
	    offset,
	    fu_struct_coreboot_cfr_option_record_get_name_size(st_option),
	    error);
	if (name == NULL)
		return FALSE;
	ui_name = fu_coreboot_cfr_plugin_read_string(
	    buf,
	    bufsz,
	    offset,
	    fu_struct_coreboot_cfr_option_record_get_ui_name_size(st_option),
	    error);
	if (ui_name == NULL)
		return FALSE;
	help_text = fu_coreboot_cfr_plugin_read_string(
	    buf,
	    bufsz,
	    offset,
	    fu_struct_coreboot_cfr_option_record_get_help_text_size(st_option),
	    error);
	if (help_text == NULL)
		return FALSE;

	for (guint32 i = 0; i < fu_struct_coreboot_cfr_option_record_get_enum_count(st_option);
	     i++) {
		g_autoptr(FuStructCorebootCfrEnumRecord) st_enum = NULL;
		g_autofree gchar *enum_label = NULL;
		g_autofree gchar *value = NULL;

		st_enum = fu_struct_coreboot_cfr_enum_record_parse_stream(stream, *offset, error);
		if (st_enum == NULL)
			return FALSE;
		if (!fu_size_checked_inc(offset, FU_STRUCT_COREBOOT_CFR_ENUM_RECORD_SIZE, error))
			return FALSE;
		enum_label = fu_coreboot_cfr_plugin_read_string(
		    buf,
		    bufsz,
		    offset,
		    fu_struct_coreboot_cfr_enum_record_get_ui_name_size(st_enum),
		    error);
		if (enum_label == NULL)
			return FALSE;

		value =
		    g_strdup_printf("%u", fu_struct_coreboot_cfr_enum_record_get_value(st_enum));
		g_ptr_array_add(possible_values, g_strdup(enum_label));
		g_hash_table_insert(value_map, g_strdup(enum_label), g_strdup(value));
		g_hash_table_insert(reverse_value_map,
				    g_strdup(value),
				    g_steal_pointer(&enum_label));
	}

	kind = fu_struct_coreboot_cfr_option_record_get_kind(st_option);
	if (kind == FU_COREBOOT_CFR_TYPE_BOOL)
		fu_coreboot_cfr_plugin_add_bool_values(possible_values,
						       value_map,
						       reverse_value_map);

	runtime_apply_method =
	    fu_struct_coreboot_cfr_option_record_get_runtime_apply_method(st_option);
	runtime_apply_id = fu_struct_coreboot_cfr_option_record_get_runtime_apply_id(st_option);
	if (runtime_apply_method == FU_COREBOOT_CFR_APPLY_METHOD_NONE || runtime_apply_id == 0) {
		g_debug("skipping CFR option %s without a runtime apply hook", name);
		return TRUE;
	}

	if (runtime_apply_method != FU_COREBOOT_CFR_APPLY_METHOD_APM_CNT) {
		g_debug("skipping CFR option %s with unsupported runtime apply method 0x%x",
			name,
			runtime_apply_method);
		return TRUE;
	}

	if (!fu_efivars_get_attrs(efivars, COREBOOT_CFR_GUID, name, &attrs, error)) {
		g_prefix_error(error, "failed to get attributes for %s: ", name);
		return FALSE;
	}

	setting =
	    fu_coreboot_cfr_setting_new(efivars,
					name,
					ui_name,
					help_text,
					fu_coreboot_cfr_plugin_setting_kind(kind),
					attrs,
					runtime_apply_method,
					runtime_apply_id,
					fu_struct_coreboot_cfr_option_record_get_min(st_option),
					fu_struct_coreboot_cfr_option_record_get_max(st_option),
					fu_struct_coreboot_cfr_option_record_get_step(st_option),
					possible_values,
					value_map,
					reverse_value_map,
					error);
	if (setting == NULL)
		return FALSE;
	fwupd_bios_setting_set_read_only(
	    setting,
	    (fu_struct_coreboot_cfr_option_record_get_cfr_flags(st_option) &
	     FU_COREBOOT_CFR_FLAG_READONLY) > 0);

	if (!fu_bios_settings_register_attr(bios_settings, setting, &error_local)) {
		g_debug("failed to register CFR option %s: %s", name, error_local->message);
		return TRUE;
	}

	g_debug("registered CFR option %s", name);
	return TRUE;
}

static gboolean
fu_coreboot_cfr_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	FuEfivars *efivars = fu_context_get_efivars(ctx);
	gsize bufsz = 0;
	gsize offset = 0;
	g_autofree guint8 *buf = NULL;
	g_autoptr(FuBiosSettings) bios_settings = NULL;
	g_autoptr(FuStructCorebootCfrSettingsHeader) st_hdr = NULL;
	g_autoptr(GInputStream) stream = NULL;

	if (!fu_efivars_exists(efivars, COREBOOT_CFR_GUID, COREBOOT_CFR_SETTINGS_VARIABLE)) {
		g_debug("CFR settings metadata not found");
		return TRUE;
	}

	if (!fu_efivars_get_data(efivars,
				 COREBOOT_CFR_GUID,
				 COREBOOT_CFR_SETTINGS_VARIABLE,
				 &buf,
				 &bufsz,
				 NULL,
				 error)) {
		g_prefix_error_literal(error, "failed to read CFR settings metadata: ");
		return FALSE;
	}

	stream = g_memory_input_stream_new_from_data(buf, bufsz, NULL);
	st_hdr = fu_struct_coreboot_cfr_settings_header_parse_stream(stream, offset, error);
	if (st_hdr == NULL)
		return FALSE;
	if (!fu_size_checked_inc(&offset, FU_STRUCT_COREBOOT_CFR_SETTINGS_HEADER_SIZE, error))
		return FALSE;

	if (fu_struct_coreboot_cfr_settings_header_get_size(st_hdr) != bufsz) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "CFR settings metadata has invalid size");
		return FALSE;
	}

	bios_settings = fu_context_get_bios_settings(ctx);
	if (bios_settings == NULL)
		return TRUE;

	for (guint32 i = 0; i < fu_struct_coreboot_cfr_settings_header_get_record_count(st_hdr);
	     i++) {
		if (!fu_coreboot_cfr_plugin_parse_option(efivars,
							 bios_settings,
							 stream,
							 buf,
							 bufsz,
							 &offset,
							 error))
			return FALSE;
	}

	if (offset != bufsz) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "CFR settings metadata has trailing data");
		return FALSE;
	}

	return TRUE;
}

static void
fu_coreboot_cfr_plugin_class_init(FuCorebootCfrPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->coldplug = fu_coreboot_cfr_plugin_coldplug;
}

static void
fu_coreboot_cfr_plugin_init(FuCorebootCfrPlugin *self)
{
}
