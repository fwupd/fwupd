/*
 * Copyright 2026 Sean Rhodes <sean@starlabs.systems>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include <string.h>

#include "fu-bios-settings-private.h"
#include "fu-context-private.h"
#include "fu-coreboot-cfr-plugin.h"
#include "fu-coreboot-cfr-setting.h"
#include "fu-coreboot-cfr-struct.h"
#include "fu-plugin-private.h"

#define FU_COREBOOT_CFR_GUID		  "ceae4c1d-335b-4685-a4a0-fc4a94eea085"
#define FU_COREBOOT_CFR_SETTINGS_VARIABLE "CorebootCfrSettings"

typedef struct {
	guint32 value;
	const gchar *ui_name;
} FuCorebootCfrEnumTestValue;

static void
fu_coreboot_cfr_test_append_string(GByteArray *buf, const gchar *str)
{
	g_byte_array_append(buf, (const guint8 *)str, strlen(str) + 1);
}

static void
fu_coreboot_cfr_test_append_option(GByteArray *buf,
				   guint16 type,
				   guint32 cfr_flags,
				   const gchar *name,
				   const gchar *ui_name,
				   const gchar *help_text,
				   guint32 runtime_apply_method,
				   guint32 runtime_apply_id,
				   guint32 default_value,
				   guint32 min,
				   guint32 max,
				   guint32 step,
				   const FuCorebootCfrEnumTestValue *enum_values,
				   guint32 enum_count)
{
	g_autoptr(FuStructCorebootCfrOptionRecord) st = fu_struct_coreboot_cfr_option_record_new();

	fu_struct_coreboot_cfr_option_record_set_kind(st, type);
	fu_struct_coreboot_cfr_option_record_set_cfr_flags(st, cfr_flags);
	fu_struct_coreboot_cfr_option_record_set_object_id(st, 0x1234);
	fu_struct_coreboot_cfr_option_record_set_runtime_apply_method(st, runtime_apply_method);
	fu_struct_coreboot_cfr_option_record_set_runtime_apply_id(st, runtime_apply_id);
	fu_struct_coreboot_cfr_option_record_set_default_value(st, default_value);
	fu_struct_coreboot_cfr_option_record_set_min(st, min);
	fu_struct_coreboot_cfr_option_record_set_max(st, max);
	fu_struct_coreboot_cfr_option_record_set_step(st, step);
	fu_struct_coreboot_cfr_option_record_set_display_flags(st, 0x0);
	fu_struct_coreboot_cfr_option_record_set_name_size(st, strlen(name) + 1);
	fu_struct_coreboot_cfr_option_record_set_ui_name_size(st, strlen(ui_name) + 1);
	fu_struct_coreboot_cfr_option_record_set_help_text_size(st, strlen(help_text) + 1);
	fu_struct_coreboot_cfr_option_record_set_enum_count(st, enum_count);

	fu_byte_array_append_array(buf, st->buf);
	fu_coreboot_cfr_test_append_string(buf, name);
	fu_coreboot_cfr_test_append_string(buf, ui_name);
	fu_coreboot_cfr_test_append_string(buf, help_text);

	for (guint32 i = 0; i < enum_count; i++) {
		g_autoptr(FuStructCorebootCfrEnumRecord) st_rcd =
		    fu_struct_coreboot_cfr_enum_record_new();
		fu_struct_coreboot_cfr_enum_record_set_value(st_rcd, enum_values[i].value);
		fu_struct_coreboot_cfr_enum_record_set_ui_name_size(st_rcd,
								    strlen(enum_values[i].ui_name) +
									1);
		fu_byte_array_append_array(buf, st_rcd->buf);
		fu_coreboot_cfr_test_append_string(buf, enum_values[i].ui_name);
	}
}

static GByteArray *
fu_coreboot_cfr_test_build_metadata(void)
{
	g_autoptr(FuStructCorebootCfrSettingsHeader) st =
	    fu_struct_coreboot_cfr_settings_header_new();
	const FuCorebootCfrEnumTestValue kbl_timeout[] = {
	    {0, "30 seconds"},
	    {1, "1 minute"},
	};

	fu_coreboot_cfr_test_append_option(st->buf,
					   FU_COREBOOT_CFR_TYPE_ENUM,
					   0x0,
					   "kbl_timeout",
					   "Keyboard Backlight Timeout",
					   "Time before the keyboard backlight turns off",
					   FU_COREBOOT_CFR_APPLY_METHOD_APM_CNT,
					   0x1,
					   0x0,
					   0x0,
					   0x1,
					   0x1,
					   kbl_timeout,
					   G_N_ELEMENTS(kbl_timeout));
	fu_coreboot_cfr_test_append_option(st->buf,
					   FU_COREBOOT_CFR_TYPE_BOOL,
					   0x0,
					   "fan_always_on",
					   "Fan Always On",
					   "Keeps the fan running",
					   FU_COREBOOT_CFR_APPLY_METHOD_APM_CNT,
					   0x2,
					   0x0,
					   0x0,
					   0x1,
					   0x1,
					   NULL,
					   0x0);
	fu_coreboot_cfr_test_append_option(st->buf,
					   FU_COREBOOT_CFR_TYPE_NUMBER,
					   0x0,
					   "kbd_brightness",
					   "Keyboard Brightness",
					   "Keyboard backlight brightness",
					   FU_COREBOOT_CFR_APPLY_METHOD_APM_CNT,
					   0x3,
					   0x2,
					   0x0,
					   0x3,
					   0x1,
					   NULL,
					   0x0);
	fu_struct_coreboot_cfr_settings_header_set_record_count(st, 0x3);
	fu_struct_coreboot_cfr_settings_header_set_size(st, st->buf->len);
	return g_steal_pointer(&st->buf);
}

static gboolean
fu_coreboot_cfr_test_set_uint32(FuContext *ctx, const gchar *name, guint32 value, GError **error)
{
	FuEfivars *efivars = fu_context_get_efivars(ctx);
	guint8 buf[sizeof(guint32)] = {0};

	if (!fu_memwrite_uint32_safe(buf, sizeof(buf), 0x0, value, G_LITTLE_ENDIAN, error))
		return FALSE;
	return fu_efivars_set_data(efivars,
				   FU_COREBOOT_CFR_GUID,
				   name,
				   buf,
				   sizeof(buf),
				   FU_EFI_VARIABLE_ATTR_NON_VOLATILE |
				       FU_EFI_VARIABLE_ATTR_BOOTSERVICE_ACCESS |
				       FU_EFI_VARIABLE_ATTR_RUNTIME_ACCESS,
				   error);
}

static void
fu_coreboot_cfr_coldplug_func(void)
{
	FwupdBiosSetting *setting = NULL;
	GPtrArray *possible_values = NULL;
	gboolean ret;
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS | FU_CONTEXT_FLAG_DUMMY_EFIVARS);
	g_autoptr(FuBiosSettings) bios_settings = fu_context_get_bios_settings(ctx);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GByteArray) metadata = fu_coreboot_cfr_test_build_metadata();
	g_autoptr(GError) error = NULL;

	ret = fu_efivars_set_data(fu_context_get_efivars(ctx),
				  FU_COREBOOT_CFR_GUID,
				  FU_COREBOOT_CFR_SETTINGS_VARIABLE,
				  metadata->data,
				  metadata->len,
				  FU_EFI_VARIABLE_ATTR_NON_VOLATILE |
				      FU_EFI_VARIABLE_ATTR_BOOTSERVICE_ACCESS |
				      FU_EFI_VARIABLE_ATTR_RUNTIME_ACCESS,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_coreboot_cfr_test_set_uint32(ctx, "kbl_timeout", 0x1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_coreboot_cfr_test_set_uint32(ctx, "fan_always_on", 0x0, &error);
	g_assert_true(ret);
	g_assert_no_error(error);
	ret = fu_coreboot_cfr_test_set_uint32(ctx, "kbd_brightness", 0x2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	plugin = fu_plugin_new_from_gtype(fu_coreboot_cfr_plugin_get_type(), ctx);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_coldplug(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	setting = fu_bios_settings_get_attr(bios_settings, "org.coreboot.cfr.kbl_timeout");
	g_assert_nonnull(setting);
	g_assert_cmpstr(fwupd_bios_setting_get_id(setting), ==, "org.coreboot.cfr.kbl_timeout");
	g_assert_cmpstr(fwupd_bios_setting_get_name(setting), ==, "Keyboard Backlight Timeout");
	g_assert_cmpstr(fwupd_bios_setting_get_description(setting),
			==,
			"Time before the keyboard backlight turns off");
	g_assert_cmpint(fwupd_bios_setting_get_kind(setting),
			==,
			FWUPD_BIOS_SETTING_KIND_ENUMERATION);
	g_assert_cmpstr(fwupd_bios_setting_get_current_value(setting), ==, "1 minute");
	possible_values = fwupd_bios_setting_get_possible_values(setting);
	g_assert_cmpint(possible_values->len, ==, 2);
	g_assert_cmpstr(g_ptr_array_index(possible_values, 0), ==, "30 seconds");
	g_assert_cmpstr(g_ptr_array_index(possible_values, 1), ==, "1 minute");

	setting = fu_bios_settings_get_attr(bios_settings, "org.coreboot.cfr.fan_always_on");
	g_assert_nonnull(setting);
	g_assert_cmpstr(fwupd_bios_setting_get_current_value(setting), ==, "Disabled");
	possible_values = fwupd_bios_setting_get_possible_values(setting);
	g_assert_cmpint(possible_values->len, ==, 2);
	g_assert_cmpstr(g_ptr_array_index(possible_values, 0), ==, "Disabled");
	g_assert_cmpstr(g_ptr_array_index(possible_values, 1), ==, "Enabled");

	setting = fu_bios_settings_get_attr(bios_settings, "org.coreboot.cfr.kbd_brightness");
	g_assert_nonnull(setting);
	g_assert_cmpint(fwupd_bios_setting_get_kind(setting), ==, FWUPD_BIOS_SETTING_KIND_INTEGER);
	g_assert_cmpstr(fwupd_bios_setting_get_current_value(setting), ==, "2");
	g_assert_cmpint(fwupd_bios_setting_get_lower_bound(setting), ==, 0);
	g_assert_cmpint(fwupd_bios_setting_get_upper_bound(setting), ==, 3);
	g_assert_cmpint(fwupd_bios_setting_get_scalar_increment(setting), ==, 1);
}

static void
fu_coreboot_cfr_write_value_func(void)
{
	gboolean ret;
	gsize data_sz = 0;
	guint32 value = 0;
	g_autofree guint8 *data = NULL;
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS | FU_CONTEXT_FLAG_DUMMY_EFIVARS);
	g_autoptr(FuCorebootCfrSetting) setting = NULL;
	g_autoptr(GError) error = NULL;

	g_assert_true(fu_coreboot_cfr_test_set_uint32(ctx, "kbl_timeout", 0x0, &error));
	g_assert_no_error(error);

	setting = fu_coreboot_cfr_setting_new(ctx, FU_COREBOOT_CFR_APPLY_METHOD_NONE);
	g_assert_nonnull(setting);
	fu_coreboot_cfr_setting_set_id(setting, "kbl_timeout");
	fwupd_bios_setting_set_kind(FWUPD_BIOS_SETTING(setting),
				    FWUPD_BIOS_SETTING_KIND_ENUMERATION);
	fwupd_bios_setting_add_possible_value_full(FWUPD_BIOS_SETTING(setting), "30 seconds", "0");
	fwupd_bios_setting_add_possible_value_full(FWUPD_BIOS_SETTING(setting), "1 minute", "1");
	fwupd_bios_setting_set_name(FWUPD_BIOS_SETTING(setting), "Keyboard Backlight Timeout");
	fwupd_bios_setting_set_description(FWUPD_BIOS_SETTING(setting),
					   "Time before the keyboard backlight turns off");

	ret = fwupd_bios_setting_setup(FWUPD_BIOS_SETTING(setting), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fwupd_bios_setting_get_current_value(FWUPD_BIOS_SETTING(setting)),
			==,
			"30 seconds");

	ret = fwupd_bios_setting_write_value(FWUPD_BIOS_SETTING(setting), "1 minute", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fwupd_bios_setting_get_current_value(FWUPD_BIOS_SETTING(setting)),
			==,
			"1 minute");

	ret = fu_efivars_get_data(fu_context_get_efivars(ctx),
				  FU_COREBOOT_CFR_GUID,
				  "kbl_timeout",
				  &data,
				  &data_sz,
				  NULL,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(data_sz, ==, sizeof(guint32));
	g_assert_true(fu_memread_uint32_safe(data, data_sz, 0x0, &value, G_LITTLE_ENDIAN, &error));
	g_assert_no_error(error);
	g_assert_cmpint(value, ==, 0x1);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/plugin/coreboot-cfr/coldplug", fu_coreboot_cfr_coldplug_func);
	g_test_add_func("/fwupd/plugin/coreboot-cfr/write-value", fu_coreboot_cfr_write_value_func);
	return g_test_run();
}
