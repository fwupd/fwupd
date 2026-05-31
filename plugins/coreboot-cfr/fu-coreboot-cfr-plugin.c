/*
 * Copyright 2026 Sean Rhodes <sean@starlabs.systems>
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-coreboot-cfr-plugin.h"
#include "fu-coreboot-cfr-setting.h"
#include "fu-coreboot-cfr-struct.h"

#define FU_COREBOOT_CFR_GUID		  "ceae4c1d-335b-4685-a4a0-fc4a94eea085"
#define FU_COREBOOT_CFR_SETTINGS_VARIABLE "CorebootCfrSettings"

struct _FuCorebootCfrPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuCorebootCfrPlugin, fu_coreboot_cfr_plugin, FU_TYPE_PLUGIN)

static gchar *
fu_coreboot_cfr_plugin_read_string(GBytes *blob, gsize *offset, guint32 sz, GError **error)
{
	g_autofree gchar *str = NULL;
	if (sz == 0)
		return g_strdup("");
	str =
	    fu_memstrsafe(g_bytes_get_data(blob, NULL), g_bytes_get_size(blob), *offset, sz, error);
	if (str == NULL)
		return NULL;
	*offset += sz;
	return g_steal_pointer(&str);
}

static gboolean
fu_coreboot_cfr_plugin_parse_option(FuContext *ctx,
				    GBytes *blob,
				    gsize *offset,
				    GError **error)
{
	FuCorebootCfrApplyMethod runtime_apply_method = FU_COREBOOT_CFR_APPLY_METHOD_NONE;
	FuCorebootCfrType kind = FU_COREBOOT_CFR_TYPE_ENUM;
	guint32 runtime_apply_id = 0;
	g_autofree gchar *help_text = NULL;
	g_autofree gchar *name = NULL;
	g_autofree gchar *ui_name = NULL;
	g_autoptr(FuBiosSettings) bios_settings = fu_context_get_bios_settings(ctx);
	g_autoptr(FuCorebootCfrSetting) setting = NULL;
	g_autoptr(FuStructCorebootCfrOptionRecord) st_option = NULL;

	st_option = fu_struct_coreboot_cfr_option_record_parse_bytes(blob, *offset, error);
	if (st_option == NULL)
		return FALSE;
	if (!fu_size_checked_inc(offset, FU_STRUCT_COREBOOT_CFR_OPTION_RECORD_SIZE, error))
		return FALSE;

	name = fu_coreboot_cfr_plugin_read_string(
	    blob,
	    offset,
	    fu_struct_coreboot_cfr_option_record_get_name_size(st_option),
	    error);
	if (name == NULL)
		return FALSE;
	ui_name = fu_coreboot_cfr_plugin_read_string(
	    blob,
	    offset,
	    fu_struct_coreboot_cfr_option_record_get_ui_name_size(st_option),
	    error);
	if (ui_name == NULL)
		return FALSE;
	help_text = fu_coreboot_cfr_plugin_read_string(
	    blob,
	    offset,
	    fu_struct_coreboot_cfr_option_record_get_help_text_size(st_option),
	    error);
	if (help_text == NULL)
		return FALSE;

	runtime_apply_method =
	    fu_struct_coreboot_cfr_option_record_get_runtime_apply_method(st_option);
	runtime_apply_id = fu_struct_coreboot_cfr_option_record_get_runtime_apply_id(st_option);
	setting = fu_coreboot_cfr_setting_new(ctx, runtime_apply_method);
	fu_coreboot_cfr_setting_set_id(setting, name);
	fu_coreboot_cfr_setting_set_runtime_apply_id(setting, runtime_apply_id);
	fwupd_bios_setting_set_name(FWUPD_BIOS_SETTING(setting), ui_name);
	fwupd_bios_setting_set_description(FWUPD_BIOS_SETTING(setting), help_text);

	kind = fu_struct_coreboot_cfr_option_record_get_kind(st_option);
	if (kind == FU_COREBOOT_CFR_TYPE_BOOL) {
		fwupd_bios_setting_set_kind(FWUPD_BIOS_SETTING(setting),
					    FWUPD_BIOS_SETTING_KIND_ENUMERATION);
		fwupd_bios_setting_add_possible_value_full(FWUPD_BIOS_SETTING(setting),
							   "Disabled",
							   "0");
		fwupd_bios_setting_add_possible_value_full(FWUPD_BIOS_SETTING(setting),
							   "Enabled",
							   "1");
	} else if (kind == FU_COREBOOT_CFR_TYPE_NUMBER) {
		fwupd_bios_setting_set_kind(FWUPD_BIOS_SETTING(setting),
					    FWUPD_BIOS_SETTING_KIND_INTEGER);
		fwupd_bios_setting_set_lower_bound(
		    FWUPD_BIOS_SETTING(setting),
		    fu_struct_coreboot_cfr_option_record_get_min(st_option));
		fwupd_bios_setting_set_upper_bound(
		    FWUPD_BIOS_SETTING(setting),
		    fu_struct_coreboot_cfr_option_record_get_max(st_option));
		fwupd_bios_setting_set_scalar_increment(
		    FWUPD_BIOS_SETTING(setting),
		    fu_struct_coreboot_cfr_option_record_get_step(st_option));
	} else if (kind == FU_COREBOOT_CFR_TYPE_ENUM) {
		fwupd_bios_setting_set_kind(FWUPD_BIOS_SETTING(setting),
					    FWUPD_BIOS_SETTING_KIND_ENUMERATION);
		for (guint32 i = 0;
		     i < fu_struct_coreboot_cfr_option_record_get_enum_count(st_option);
		     i++) {
			g_autoptr(FuStructCorebootCfrEnumRecord) st_enum = NULL;
			g_autofree gchar *key = NULL;
			g_autofree gchar *value = NULL;

			st_enum =
			    fu_struct_coreboot_cfr_enum_record_parse_bytes(blob, *offset, error);
			if (st_enum == NULL)
				return FALSE;
			if (!fu_size_checked_inc(offset,
						 FU_STRUCT_COREBOOT_CFR_ENUM_RECORD_SIZE,
						 error))
				return FALSE;
			key = fu_coreboot_cfr_plugin_read_string(
			    blob,
			    offset,
			    fu_struct_coreboot_cfr_enum_record_get_ui_name_size(st_enum),
			    error);
			if (key == NULL)
				return FALSE;

			value =
			    g_strdup_printf("%u",
					    fu_struct_coreboot_cfr_enum_record_get_value(st_enum));
			fwupd_bios_setting_add_possible_value_full(FWUPD_BIOS_SETTING(setting),
								   key,
								   value);
		}
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "unsupported CFR option kind 0x%x",
			    kind);
		return FALSE;
	}
	fwupd_bios_setting_set_read_only(
	    FWUPD_BIOS_SETTING(setting),
	    (fu_struct_coreboot_cfr_option_record_get_cfr_flags(st_option) &
	     FU_COREBOOT_CFR_FLAG_READONLY) > 0);

	/* no point adding as we cannot change this */
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

	/* read initial value and register */
	if (!fwupd_bios_setting_setup(FWUPD_BIOS_SETTING(setting), error))
		return FALSE;
	if (!fu_bios_settings_register_attr(bios_settings, FWUPD_BIOS_SETTING(setting), error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_coreboot_cfr_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);

	if (!fu_efivars_exists(fu_context_get_efivars(ctx),
			       FU_COREBOOT_CFR_GUID,
			       FU_COREBOOT_CFR_SETTINGS_VARIABLE)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "CFR settings metadata not found");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_coreboot_cfr_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	gsize offset = 0;
	g_autoptr(FuStructCorebootCfrSettingsHeader) st_hdr = NULL;
	g_autoptr(GBytes) blob = NULL;

	blob = fu_efivars_get_data_bytes(fu_context_get_efivars(ctx),
					 FU_COREBOOT_CFR_GUID,
					 FU_COREBOOT_CFR_SETTINGS_VARIABLE,
					 NULL,
					 error);
	if (blob == NULL) {
		g_prefix_error_literal(error, "failed to read CFR settings metadata: ");
		return FALSE;
	}

	st_hdr = fu_struct_coreboot_cfr_settings_header_parse_bytes(blob, offset, error);
	if (st_hdr == NULL)
		return FALSE;
	if (!fu_size_checked_inc(&offset, FU_STRUCT_COREBOOT_CFR_SETTINGS_HEADER_SIZE, error))
		return FALSE;

	if (fu_struct_coreboot_cfr_settings_header_get_size(st_hdr) != g_bytes_get_size(blob)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "CFR settings metadata has invalid size");
		return FALSE;
	}
	for (guint32 i = 0; i < fu_struct_coreboot_cfr_settings_header_get_record_count(st_hdr);
	     i++) {
		if (!fu_coreboot_cfr_plugin_parse_option(ctx, blob, &offset, error))
			return FALSE;
	}
	if (offset != g_bytes_get_size(blob)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "CFR settings metadata has trailing data");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_coreboot_cfr_plugin_class_init(FuCorebootCfrPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->startup = fu_coreboot_cfr_plugin_startup;
	plugin_class->coldplug = fu_coreboot_cfr_plugin_coldplug;
}

static void
fu_coreboot_cfr_plugin_init(FuCorebootCfrPlugin *self)
{
}
