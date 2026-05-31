/*
 * Copyright 2026 Star Labs
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-bios-settings-private.h"
#include "fu-byte-array.h"
#include "fu-context-private.h"
#include "fu-coreboot-cfr-plugin.h"
#include "fu-coreboot-cfr-setting.h"
#include "fu-dummy-efivars.h"
#include "fu-plugin-private.h"

#define COREBOOT_CFR_GUID		  "ceae4c1d-335b-4685-a4a0-fc4a94eea085"
#define COREBOOT_CFR_SETTINGS_VARIABLE	  "CorebootCfrSettings"
#define COREBOOT_CFR_SETTINGS_MAGIC	  0x44574643 /* CFWD */
#define COREBOOT_CFR_SETTINGS_VERSION	  2
#define COREBOOT_CFR_TYPE_ENUM		  1
#define COREBOOT_CFR_TYPE_NUMBER	  2
#define COREBOOT_CFR_TYPE_BOOL		  3
#define COREBOOT_CFR_APPLY_METHOD_NONE	  0
#define COREBOOT_CFR_APPLY_METHOD_APM_CNT 1
#define COREBOOT_CFR_SETTINGS_HDR_SZ	  16
#define COREBOOT_CFR_OPTION_RECORD_SZ	  60

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
	fu_byte_array_append_uint16(buf, type, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint16(buf, COREBOOT_CFR_OPTION_RECORD_SZ, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, cfr_flags, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint64(buf, 0x1234, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, runtime_apply_method, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, runtime_apply_id, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, default_value, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, min, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, max, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, step, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, 0x0, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, strlen(name) + 1, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, strlen(ui_name) + 1, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, strlen(help_text) + 1, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, enum_count, G_LITTLE_ENDIAN);

	fu_coreboot_cfr_test_append_string(buf, name);
	fu_coreboot_cfr_test_append_string(buf, ui_name);
	fu_coreboot_cfr_test_append_string(buf, help_text);

	for (guint32 i = 0; i < enum_count; i++) {
		fu_byte_array_append_uint32(buf, enum_values[i].value, G_LITTLE_ENDIAN);
		fu_byte_array_append_uint32(buf,
					    strlen(enum_values[i].ui_name) + 1,
					    G_LITTLE_ENDIAN);
		fu_coreboot_cfr_test_append_string(buf, enum_values[i].ui_name);
	}
}

static GByteArray *
fu_coreboot_cfr_test_build_metadata(void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	const FuCorebootCfrEnumTestValue kbl_timeout[] = {
	    {0, "30 seconds"},
	    {1, "1 minute"},
	};

	fu_byte_array_append_uint32(buf, COREBOOT_CFR_SETTINGS_MAGIC, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint16(buf, COREBOOT_CFR_SETTINGS_VERSION, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint16(buf, COREBOOT_CFR_SETTINGS_HDR_SZ, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, 0x0, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, 0x3, G_LITTLE_ENDIAN);

	fu_coreboot_cfr_test_append_option(buf,
					   COREBOOT_CFR_TYPE_ENUM,
					   0x0,
					   "kbl_timeout",
					   "Keyboard Backlight Timeout",
					   "Time before the keyboard backlight turns off",
					   COREBOOT_CFR_APPLY_METHOD_APM_CNT,
					   0x1,
					   0x0,
					   0x0,
					   0x1,
					   0x1,
					   kbl_timeout,
					   G_N_ELEMENTS(kbl_timeout));
	fu_coreboot_cfr_test_append_option(buf,
					   COREBOOT_CFR_TYPE_BOOL,
					   0x0,
					   "fan_always_on",
					   "Fan Always On",
					   "Keeps the fan running",
					   COREBOOT_CFR_APPLY_METHOD_APM_CNT,
					   0x2,
					   0x0,
					   0x0,
					   0x1,
					   0x1,
					   NULL,
					   0x0);
	fu_coreboot_cfr_test_append_option(buf,
					   COREBOOT_CFR_TYPE_NUMBER,
					   0x0,
					   "kbd_brightness",
					   "Keyboard Brightness",
					   "Keyboard backlight brightness",
					   COREBOOT_CFR_APPLY_METHOD_APM_CNT,
					   0x3,
					   0x2,
					   0x0,
					   0x3,
					   0x1,
					   NULL,
					   0x0);

	g_assert_true(
	    fu_memwrite_uint32_safe(buf->data, buf->len, 0x8, buf->len, G_LITTLE_ENDIAN, &error));
	g_assert_no_error(error);

	return g_steal_pointer(&buf);
}

static gboolean
fu_coreboot_cfr_test_set_uint32(FuEfivars *efivars,
				const gchar *name,
				guint32 value,
				GError **error)
{
	guint8 buf[sizeof(guint32)] = {0};

	if (!fu_memwrite_uint32_safe(buf, sizeof(buf), 0x0, value, G_LITTLE_ENDIAN, error))
		return FALSE;
	return fu_efivars_set_data(efivars,
				   COREBOOT_CFR_GUID,
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
	gboolean ret;
	FuEfivars *efivars = NULL;
	FwupdBiosSetting *setting = NULL;
	GPtrArray *possible_values = NULL;
	g_autoptr(FuBiosSettings) bios_settings = NULL;
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS | FU_CONTEXT_FLAG_DUMMY_EFIVARS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GByteArray) metadata = fu_coreboot_cfr_test_build_metadata();
	g_autoptr(GError) error = NULL;

	efivars = fu_context_get_efivars(ctx);
	ret = fu_efivars_set_data(efivars,
				  COREBOOT_CFR_GUID,
				  COREBOOT_CFR_SETTINGS_VARIABLE,
				  metadata->data,
				  metadata->len,
				  FU_EFI_VARIABLE_ATTR_NON_VOLATILE |
				      FU_EFI_VARIABLE_ATTR_BOOTSERVICE_ACCESS |
				      FU_EFI_VARIABLE_ATTR_RUNTIME_ACCESS,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(fu_coreboot_cfr_test_set_uint32(efivars, "kbl_timeout", 0x1, &error));
	g_assert_no_error(error);
	g_assert_true(fu_coreboot_cfr_test_set_uint32(efivars, "fan_always_on", 0x0, &error));
	g_assert_no_error(error);
	g_assert_true(fu_coreboot_cfr_test_set_uint32(efivars, "kbd_brightness", 0x2, &error));
	g_assert_no_error(error);

	plugin = fu_plugin_new_from_gtype(fu_coreboot_cfr_plugin_get_type(), ctx);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_coldplug(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	bios_settings = fu_context_get_bios_settings(ctx);

	setting = fu_bios_settings_get_attr(bios_settings, "org.coreboot.cfr.kbl_timeout");
	g_assert_nonnull(setting);
	g_assert_cmpstr(fwupd_bios_setting_get_name(setting), ==, "kbl_timeout");
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
	g_autoptr(FuEfivars) efivars = fu_dummy_efivars_new();
	g_autoptr(FwupdBiosSetting) setting = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GHashTable) reverse_value_map =
	    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	g_autoptr(GHashTable) value_map =
	    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	g_autoptr(GPtrArray) possible_values = g_ptr_array_new_with_free_func(g_free);

	g_ptr_array_add(possible_values, g_strdup("30 seconds"));
	g_ptr_array_add(possible_values, g_strdup("1 minute"));
	g_hash_table_insert(value_map, g_strdup("30 seconds"), g_strdup("0"));
	g_hash_table_insert(value_map, g_strdup("1 minute"), g_strdup("1"));
	g_hash_table_insert(reverse_value_map, g_strdup("0"), g_strdup("30 seconds"));
	g_hash_table_insert(reverse_value_map, g_strdup("1"), g_strdup("1 minute"));

	g_assert_true(fu_coreboot_cfr_test_set_uint32(efivars, "kbl_timeout", 0x0, &error));
	g_assert_no_error(error);

	setting = fu_coreboot_cfr_setting_new(efivars,
					      "kbl_timeout",
					      "Keyboard Backlight Timeout",
					      "Time before the keyboard backlight turns off",
					      FWUPD_BIOS_SETTING_KIND_ENUMERATION,
					      FU_EFI_VARIABLE_ATTR_NON_VOLATILE |
						  FU_EFI_VARIABLE_ATTR_BOOTSERVICE_ACCESS |
						  FU_EFI_VARIABLE_ATTR_RUNTIME_ACCESS,
					      COREBOOT_CFR_APPLY_METHOD_NONE,
					      0x0,
					      0x0,
					      0x1,
					      0x1,
					      possible_values,
					      value_map,
					      reverse_value_map,
					      &error);
	g_assert_no_error(error);
	g_assert_nonnull(setting);
	g_assert_cmpstr(fwupd_bios_setting_get_current_value(setting), ==, "30 seconds");

	ret = fwupd_bios_setting_write_value(setting, "1 minute", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fwupd_bios_setting_get_current_value(setting), ==, "1 minute");

	ret = fu_efivars_get_data(efivars,
				  COREBOOT_CFR_GUID,
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
