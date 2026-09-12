/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <glib/gstdio.h>

#include "fu-amd-afc-common.h"
#include "fu-amd-afc-struct.h"
#include "fu-bios-settings-private.h"
#include "fu-context-private.h"

static void
fu_test_array_u32(GByteArray *array, gsize offset, guint32 value)
{
	fu_memwrite_uint32(array->data + offset, value, G_LITTLE_ENDIAN);
}

static guint
fu_amd_afc_test_count_config_entry(GPtrArray *entries,
				   const gchar *const *path,
				   gsize pathsz,
				   const gchar *value)
{
	guint count = 0;

	for (guint i = 0; i < entries->len; i++) {
		FuAmdAfcConfigEntry *entry = g_ptr_array_index(entries, i);
		gboolean matches = entry->path->len == pathsz;

		for (guint j = 0; matches && j < entry->path->len; j++)
			matches = g_strcmp0(g_ptr_array_index(entry->path, j), path[j]) == 0;
		if (matches && g_strcmp0(entry->value, value) == 0)
			count++;
	}
	return count;
}

static void
fu_amd_afc_config_id_overflow_func(void)
{
	g_autoptr(GByteArray) strings = g_byte_array_new();
	guint16 count = G_MAXUINT16;
	guint16 id = 0;

	g_assert_false(fu_amd_afc_config_id(strings, &count, "overflow", &id, NULL));
	g_assert_cmpuint(strings->len, ==, 0);
}

static GBytes *
fu_amd_afc_test_build_table(void)
{
	g_autoptr(GByteArray) strings = g_byte_array_sized_new(128);
	g_autoptr(GByteArray) forms = g_byte_array_sized_new(128);
	g_autoptr(GByteArray) varstores = g_byte_array_sized_new(32);
	g_autoptr(GByteArray) blob = g_byte_array_sized_new(512);
	g_autoptr(GByteArray) table = g_byte_array_sized_new(512);
	g_autoptr(FuStructAmdAfcEieHeader) st_eie = fu_struct_amd_afc_eie_header_new();
	g_autoptr(FuStructAmdAfcVarstoreHeader) st_varstore =
	    fu_struct_amd_afc_varstore_header_new();
	const gchar *values[] =
	    {" Setup ", " Main ", "  Feature  ", "Disabled", "Enabled", "   ", "Sibling"};
	guint8 strings_end = 0;
	guint8 op_form_set[23] = {0x0e, 23 | 0x80, [18] = 1};
	guint8 op_form[6] = {0x01, 6 | 0x80, 0x10, 0, 2, 0};
	guint8 op_one_of[17] = {0x05, 17 | 0x80, 3, 0, 0, 0, 0x20, 0, 7, 0};
	guint8 op_form_sibling[6] = {0x01, 6 | 0x80, 0x11, 0, 6, 0};
	guint8 op_one_of_sibling[17] = {0x05, 17 | 0x80, 7, 0, 0, 0, 0x21, 0, 7, 0};
	guint8 op_disabled[7] = {0x09, 7, 4, 0, 0x10, 0, 0};
	guint8 op_enabled[7] = {0x09, 7, 5, 0, 0, 0, 1};
	guint8 op_end[] = {0x29, 2};
	guint8 checksum;

	/* english HII string package */
	g_byte_array_set_size(strings, 50);
	memset(strings->data, 0, strings->len);
	strings->data[46] = 'e';
	strings->data[47] = 'n';
	for (guint i = 0; i < G_N_ELEMENTS(values); i++) {
		guint8 block = i == G_N_ELEMENTS(values) - 1 ? 0x14 : 0x10;
		g_byte_array_append(strings, &block, 1);
		if (block == 0x14) {
			g_autoptr(GByteArray) value =
			    fu_utf8_to_utf16_byte_array(values[i],
							G_LITTLE_ENDIAN,
							FU_UTF_CONVERT_FLAG_APPEND_NUL,
							NULL);
			g_assert_nonnull(value);
			g_byte_array_append(strings, value->data, value->len);
		} else {
			g_byte_array_append(strings,
					    (const guint8 *)values[i],
					    strlen(values[i]) + 1);
		}
	}
	g_byte_array_append(strings, &strings_end, 1);
	fu_test_array_u32(strings, 0, (0x04U << 24) | strings->len);
	fu_test_array_u32(strings, 4, 50);

	/* FormSet(Setup) -> Form(Main) -> OneOf(Feature) */
	g_byte_array_set_size(forms, 4);
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
	fu_test_array_u32(forms, 0, (0x02U << 24) | forms->len);

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
	fu_struct_amd_afc_eie_header_set_revision(st_eie, 0x0100);
	fu_struct_amd_afc_eie_header_set_forms_offset(st_eie, 0);
	fu_struct_amd_afc_eie_header_set_forms_size(st_eie, forms->len);
	fu_struct_amd_afc_eie_header_set_strings_offset(st_eie, forms->len);
	fu_struct_amd_afc_eie_header_set_strings_size(st_eie, strings->len);
	fu_struct_amd_afc_eie_header_set_varstores_offset(st_eie, forms->len + strings->len);
	fu_struct_amd_afc_eie_header_set_varstores_size(st_eie, varstores->len);
	checksum = fu_sum8(st_eie->buf->data, st_eie->buf->len) + fu_sum8(forms->data, forms->len) +
		   fu_sum8(strings->data, strings->len) + fu_sum8(varstores->data, varstores->len);
	fu_struct_amd_afc_eie_header_set_checksum(st_eie, (guint8)(0U - checksum));
	g_byte_array_append(blob, st_eie->buf->data, st_eie->buf->len);
	g_byte_array_append(blob, forms->data, forms->len);
	g_byte_array_append(blob, strings->data, strings->len);
	g_byte_array_append(blob, varstores->data, varstores->len);

	/* ACPI header; only its size matters to the parser */
	g_byte_array_set_size(table, 36);
	memset(table->data, 0, table->len);
	for (guint i = 0; i < 4; i++)
		table->data[i] = "SSDT"[i];
	for (guint i = 0; i < 8; i++)
		table->data[16 + i] = "AmdFwCfg"[i];
	g_byte_array_append(table, blob->data, blob->len);
	fu_test_array_u32(table, 4, table->len);
	table->data[9] = (guint8)(0U - fu_sum8(table->data, table->len));
	return g_byte_array_free_to_bytes(g_steal_pointer(&table));
}

static void
fu_amd_afc_invalid_ucs2_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS | FU_CONTEXT_FLAG_DUMMY_EFIVARS);
	g_autoptr(FuAmdAfcState) state = fu_amd_afc_state_new(ctx);
	g_autoptr(GByteArray) strings = g_byte_array_sized_new(56);
	g_autoptr(GError) error = NULL;
	const guint8 invalid_ucs2[] = {0x14, 0x00, 0xd8, 0x00, 0x00, 0x00};

	g_byte_array_set_size(strings, 50);
	memset(strings->data, 0, strings->len);
	strings->data[46] = 'e';
	strings->data[47] = 'n';
	g_byte_array_append(strings, invalid_ucs2, sizeof(invalid_ucs2));
	fu_test_array_u32(strings, 0, (0x04U << 24) | strings->len);
	fu_test_array_u32(strings, 4, 50);
	ret = fu_amd_afc_hii_strings_parse_package(state, strings->data, strings->len, &error);
	g_assert_false(ret);
	g_assert_error(error, G_CONVERT_ERROR, G_CONVERT_ERROR_PARTIAL_INPUT);
}

static void
fu_amd_afc_short_strings_func(void)
{
	guint8 strings[46] = {0};
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS | FU_CONTEXT_FLAG_DUMMY_EFIVARS);
	g_autoptr(FuAmdAfcState) state = fu_amd_afc_state_new(ctx);
	g_autoptr(GError) error = NULL;

	g_assert_false(
	    fu_amd_afc_hii_strings_parse_package(state, strings, sizeof(strings), &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
}

static void
fu_amd_afc_parse_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS | FU_CONTEXT_FLAG_DUMMY_EFIVARS);
	g_autoptr(FuAmdAfcState) state = fu_amd_afc_state_new(ctx);
	g_autoptr(FuBiosSettings) settings = fu_bios_settings_new(ctx);
	g_autoptr(GBytes) table = fu_amd_afc_test_build_table();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *sysfs_dir = NULL;
	FwupdBiosSetting *setting;

	ret = fu_amd_afc_state_parse_table(state, table, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpuint(fu_amd_afc_state_get_setting_count(state), ==, 2);
	ret = fu_amd_afc_state_add_bios_settings(state, settings, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	setting = fu_bios_settings_get_attr(settings, "com.amd-afc.Setup.Main.Feature.0020");
	g_assert_nonnull(setting);
	g_assert_cmpstr(fwupd_bios_setting_get_name(setting), ==, "Setup|Main|Feature");
	g_assert_cmpint(fwupd_bios_setting_get_kind(setting),
			==,
			FWUPD_BIOS_SETTING_KIND_ENUMERATION);
	g_assert_null(fwupd_bios_setting_get_description(setting));
	g_assert_cmpstr(fwupd_bios_setting_get_current_value(setting), ==, "Disabled");
	g_assert_true(fwupd_bios_setting_has_possible_value(setting, "Enabled"));
	/* whitespace-only form names do not create empty hierarchy components */
	setting = fu_bios_settings_get_attr(settings, "com.amd-afc.Setup.Sibling.0021");
	g_assert_nonnull(setting);
	g_assert_cmpstr(fwupd_bios_setting_get_name(setting), ==, "Setup|Sibling");

	/* native settings survive a refresh of the sysfs-backed providers */
	sysfs_dir = g_dir_make_tmp("fwupd-amd-afc-XXXXXX", &error);
	g_assert_no_error(error);
	g_assert_nonnull(sysfs_dir);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR_FW_ATTRIB, sysfs_dir);
	g_assert_true(fu_bios_settings_setup(settings, &error));
	g_assert_no_error(error);
	g_assert_nonnull(
	    fu_bios_settings_get_attr(settings, "com.amd-afc.Setup.Main.Feature.0020"));
	g_assert_cmpint(g_rmdir(sysfs_dir), ==, 0);
}

static void
fu_amd_afc_write_func(void)
{
	gboolean ret;
	gsize bufsz = 0;
	const guint8 *buf;
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS | FU_CONTEXT_FLAG_DUMMY_EFIVARS);
	g_autoptr(FuAmdAfcState) state1 = fu_amd_afc_state_new(ctx);
	g_autoptr(FuAmdAfcState) state2 = fu_amd_afc_state_new(ctx);
	g_autoptr(FuAmdAfcState) state3 = fu_amd_afc_state_new(ctx);
	g_autoptr(FuBiosSettings) settings1 = fu_bios_settings_new(ctx);
	g_autoptr(FuBiosSettings) settings2 = fu_bios_settings_new(ctx);
	g_autoptr(FuBiosSettings) settings3 = fu_bios_settings_new(ctx);
	g_autoptr(GBytes) table = fu_amd_afc_test_build_table();
	g_autoptr(GBytes) variable = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuStructAmdAfcConfigHeader) st_hdr = NULL;
	g_autoptr(GPtrArray) entries = NULL;
	const gchar *feature_path[] = {" Setup ", " Main ", "  Feature  "};
	const gchar *sibling_path[] = {" Setup ", "   ", "Sibling"};
	gboolean pending_reboot = FALSE;
	FwupdBiosSetting *setting;

	g_assert_true(fu_amd_afc_state_parse_table(state1, table, &error));
	g_assert_no_error(error);
	g_assert_true(fu_amd_afc_state_add_bios_settings(state1, settings1, &error));
	g_assert_no_error(error);
	setting = fu_bios_settings_get_attr(settings1, "com.amd-afc.Setup.Main.Feature.0020");
	ret = fwupd_bios_setting_write_value(setting, "Enabled", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	variable = fu_efivars_get_data_bytes(fu_context_get_efivars(ctx),
					     "f5c0066d-dd67-4186-bcca-55d7e73ecd56",
					     "AmdFwConfig",
					     NULL,
					     &error);
	g_assert_no_error(error);
	g_assert_nonnull(variable);
	buf = g_bytes_get_data(variable, &bufsz);
	st_hdr = fu_struct_amd_afc_config_header_parse(buf, bufsz, 0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(st_hdr);
	g_assert_cmpuint(fu_struct_amd_afc_config_header_get_entry_count(st_hdr), ==, 1);
	g_assert_cmpuint(fu_sum8(buf, bufsz), ==, 0);
	g_assert_cmpstr((const gchar *)buf + FU_STRUCT_AMD_AFC_CONFIG_HEADER_SIZE,
			==,
			"  Feature  ");
	entries = fu_amd_afc_config_parse(variable, &error);
	g_assert_no_error(error);
	g_assert_nonnull(entries);
	g_assert_cmpuint(fu_amd_afc_test_count_config_entry(entries,
							    feature_path,
							    G_N_ELEMENTS(feature_path),
							    "Enabled"),
			 ==,
			 1);
	setting = fu_bios_settings_get_attr(settings1, FWUPD_BIOS_SETTING_PENDING_REBOOT);
	g_assert_null(setting);
	g_assert_true(fu_bios_settings_get_pending_reboot(settings1, &pending_reboot, &error));
	g_assert_no_error(error);
	g_assert_true(pending_reboot);

	/* a separate process changing another setting preserves the first entry */
	g_assert_true(fu_amd_afc_state_parse_table(state2, table, &error));
	g_assert_no_error(error);
	g_assert_true(fu_amd_afc_state_add_bios_settings(state2, settings2, &error));
	g_assert_no_error(error);
	setting = fu_bios_settings_get_attr(settings2, "com.amd-afc.Setup.Sibling.0021");
	ret = fwupd_bios_setting_write_value(setting, "Enabled", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_clear_pointer(&variable, g_bytes_unref);
	variable = fu_efivars_get_data_bytes(fu_context_get_efivars(ctx),
					     "f5c0066d-dd67-4186-bcca-55d7e73ecd56",
					     "AmdFwConfig",
					     NULL,
					     &error);
	g_assert_no_error(error);
	buf = g_bytes_get_data(variable, &bufsz);
	g_clear_pointer(&st_hdr, fu_struct_amd_afc_config_header_unref);
	st_hdr = fu_struct_amd_afc_config_header_parse(buf, bufsz, 0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(st_hdr);
	g_assert_cmpuint(fu_struct_amd_afc_config_header_get_entry_count(st_hdr), ==, 2);
	g_assert_cmpuint(fu_sum8(buf, bufsz), ==, 0);
	g_clear_pointer(&entries, g_ptr_array_unref);
	entries = fu_amd_afc_config_parse(variable, &error);
	g_assert_no_error(error);
	g_assert_nonnull(entries);
	g_assert_cmpuint(fu_amd_afc_test_count_config_entry(entries,
							    feature_path,
							    G_N_ELEMENTS(feature_path),
							    "Enabled"),
			 ==,
			 1);
	g_assert_cmpuint(fu_amd_afc_test_count_config_entry(entries,
							    sibling_path,
							    G_N_ELEMENTS(sibling_path),
							    "Enabled"),
			 ==,
			 1);

	/* re-queuing an entry from another process does not duplicate it */
	g_assert_true(fu_amd_afc_state_parse_table(state3, table, &error));
	g_assert_no_error(error);
	g_assert_true(fu_amd_afc_state_add_bios_settings(state3, settings3, &error));
	g_assert_no_error(error);
	setting = fu_bios_settings_get_attr(settings3, "com.amd-afc.Setup.Main.Feature.0020");
	ret = fwupd_bios_setting_write_value(setting, "Enabled", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_clear_pointer(&variable, g_bytes_unref);
	variable = fu_efivars_get_data_bytes(fu_context_get_efivars(ctx),
					     "f5c0066d-dd67-4186-bcca-55d7e73ecd56",
					     "AmdFwConfig",
					     NULL,
					     &error);
	g_assert_no_error(error);
	buf = g_bytes_get_data(variable, &bufsz);
	g_clear_pointer(&st_hdr, fu_struct_amd_afc_config_header_unref);
	st_hdr = fu_struct_amd_afc_config_header_parse(buf, bufsz, 0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(st_hdr);
	g_assert_cmpuint(fu_struct_amd_afc_config_header_get_entry_count(st_hdr), ==, 2);
	g_assert_cmpuint(fu_sum8(buf, bufsz), ==, 0);
	g_clear_pointer(&entries, g_ptr_array_unref);
	entries = fu_amd_afc_config_parse(variable, &error);
	g_assert_no_error(error);
	g_assert_nonnull(entries);
	g_assert_cmpuint(fu_amd_afc_test_count_config_entry(entries,
							    feature_path,
							    G_N_ELEMENTS(feature_path),
							    "Enabled"),
			 ==,
			 1);
	g_assert_cmpuint(fu_amd_afc_test_count_config_entry(entries,
							    sibling_path,
							    G_N_ELEMENTS(sibling_path),
							    "Enabled"),
			 ==,
			 1);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/amd-afc/parse", fu_amd_afc_parse_func);
	g_test_add_func("/amd-afc/parse/short-strings", fu_amd_afc_short_strings_func);
	g_test_add_func("/amd-afc/parse/invalid-ucs2", fu_amd_afc_invalid_ucs2_func);
	g_test_add_func("/amd-afc/config-id/overflow", fu_amd_afc_config_id_overflow_func);
	g_test_add_func("/amd-afc/write", fu_amd_afc_write_func);
	return g_test_run();
}
