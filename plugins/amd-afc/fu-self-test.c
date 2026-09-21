/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <glib/gstdio.h>

#include "fu-amd-afc-config-entry.h"
#include "fu-amd-afc-config.h"
#include "fu-amd-afc-state.h"
#include "fu-amd-afc-struct.h"
#include "fu-context-private.h"

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

static void
fu_amd_afc_invalid_ucs2_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS | FU_CONTEXT_FLAG_DUMMY_EFIVARS);
	g_autoptr(FuAmdAfcState) state = fu_amd_afc_state_new(ctx);
	g_autoptr(GByteArray) strings = g_byte_array_sized_new(56);
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;
	const guint8 invalid_ucs2[] = {0x14, 0x00, 0xd8, 0x00, 0x00, 0x00};

	fu_byte_array_set_size(strings, 50, 0x0);
	strings->data[46] = 'e';
	strings->data[47] = 'n';
	g_byte_array_append(strings, invalid_ucs2, sizeof(invalid_ucs2));
	fu_memwrite_uint32(strings->data + 0, (0x04U << 24) | strings->len, G_LITTLE_ENDIAN);
	fu_memwrite_uint32(strings->data + 4, 50, G_LITTLE_ENDIAN);
	blob = g_bytes_new(strings->data, strings->len);
	ret = fu_amd_afc_state_strings_parse_package(state, blob, 0, &error);
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
	g_autoptr(GBytes) blob = NULL;

	blob = g_bytes_new(strings, sizeof(strings));
	g_assert_false(fu_amd_afc_state_strings_parse_package(state, blob, 0, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
}

static void
fu_amd_afc_invalid_extended_string_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS | FU_CONTEXT_FLAG_DUMMY_EFIVARS);
	g_autoptr(FuAmdAfcState) state = fu_amd_afc_state_new(ctx);
	g_autoptr(GByteArray) strings = g_byte_array_sized_new(53);
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) blob = NULL;
	const guint8 invalid_ext[] = {FU_AMD_AFC_HII_STRING_BLOCK_KIND_EXT1, 0x00, 0x02};

	fu_byte_array_set_size(strings, 50, 0x0);
	strings->data[46] = 'e';
	strings->data[47] = 'n';
	g_byte_array_append(strings, invalid_ext, sizeof(invalid_ext));
	fu_memwrite_uint32(strings->data + 0, (0x04U << 24) | strings->len, G_LITTLE_ENDIAN);
	fu_memwrite_uint32(strings->data + 4, 50, G_LITTLE_ENDIAN);

	blob = g_bytes_new(strings->data, strings->len);
	ret = fu_amd_afc_state_strings_parse_package(state, blob, 0, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);
}

static void
fu_amd_afc_parse_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS | FU_CONTEXT_FLAG_DUMMY_EFIVARS);
	g_autoptr(FuAmdAfcState) state = fu_amd_afc_state_new(ctx);
	g_autoptr(GBytes) table = NULL;
	g_autoptr(GError) error = NULL;
	FuBiosSetting *setting;

	table = fu_firmware_write(FU_FIRMWARE(state), &error);
	g_assert_no_error(error);
	g_assert_nonnull(table);
	ret = fu_amd_afc_state_parse_table(state, table, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpuint(fu_amd_afc_state_get_setting_count(state), ==, 2);
	ret = fu_amd_afc_state_add_bios_settings(state, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	setting = fu_context_get_bios_setting(ctx, "com.amd-afc.Setup.Main.Feature.0020");
	g_assert_nonnull(setting);
	g_assert_cmpstr(fu_bios_setting_get_name(setting), ==, "Feature");
	g_assert_cmpstr(fwupd_bios_setting_get_parent(FWUPD_BIOS_SETTING(setting)),
			==,
			"Setup/Main");
	g_assert_cmpint(fu_bios_setting_get_kind(setting), ==, FWUPD_BIOS_SETTING_KIND_ENUMERATION);
	g_assert_null(fu_bios_setting_get_description(setting));
	g_assert_cmpstr(fu_bios_setting_get_current_value(setting), ==, "Disabled");
	g_assert_true(
	    fwupd_bios_setting_has_possible_value(FWUPD_BIOS_SETTING(setting), "Enabled"));
	/* whitespace-only form names do not create empty hierarchy components */
	setting = fu_context_get_bios_setting(ctx, "com.amd-afc.Setup.Sibling.0021");
	g_assert_nonnull(setting);
	g_assert_cmpstr(fu_bios_setting_get_name(setting), ==, "Sibling");
	g_assert_cmpstr(fwupd_bios_setting_get_parent(FWUPD_BIOS_SETTING(setting)), ==, "Setup");

	g_assert_nonnull(fu_context_get_bios_setting(ctx, "com.amd-afc.Setup.Main.Feature.0020"));
}

static gboolean
fu_amd_afc_copy_variable(FuContext *ctx_src, FuContext *ctx_dst, GError **error)
{
	gsize bufsz = 0;
	const guint8 *buf;
	FuEfiVariableAttrs attrs = 0;
	g_autoptr(GBytes) variable = NULL;

	variable = fu_efivars_get_data_bytes(fu_context_get_efivars(ctx_src),
					     "f5c0066d-dd67-4186-bcca-55d7e73ecd56",
					     "AmdFwConfig",
					     &attrs,
					     error);
	if (variable == NULL)
		return FALSE;
	buf = g_bytes_get_data(variable, &bufsz);
	return fu_efivars_set_data(fu_context_get_efivars(ctx_dst),
				   "f5c0066d-dd67-4186-bcca-55d7e73ecd56",
				   "AmdFwConfig",
				   buf,
				   bufsz,
				   attrs,
				   error);
}

static void
fu_amd_afc_write_func(void)
{
	gboolean ret;
	gsize bufsz = 0;
	const guint8 *buf;
	g_autoptr(FuContext) ctx1 =
	    fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS | FU_CONTEXT_FLAG_DUMMY_EFIVARS);
	g_autoptr(FuContext) ctx2 =
	    fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS | FU_CONTEXT_FLAG_DUMMY_EFIVARS);
	g_autoptr(FuContext) ctx3 =
	    fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS | FU_CONTEXT_FLAG_DUMMY_EFIVARS);
	g_autoptr(FuAmdAfcState) state1 = fu_amd_afc_state_new(ctx1);
	g_autoptr(FuAmdAfcState) state2 = fu_amd_afc_state_new(ctx2);
	g_autoptr(FuAmdAfcState) state3 = fu_amd_afc_state_new(ctx3);
	g_autoptr(GBytes) table = NULL;
	g_autoptr(GBytes) variable = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuStructAmdAfcConfigHeader) st_hdr = NULL;
	g_autoptr(GPtrArray) entries = NULL;
	const gchar *feature_path[] = {" Setup ", " Main ", "  Feature  "};
	const gchar *sibling_path[] = {" Setup ", "   ", "Sibling"};
	FuBiosSetting *setting;

	table = fu_firmware_write(FU_FIRMWARE(state1), &error);
	g_assert_no_error(error);
	g_assert_nonnull(table);
	g_assert_true(fu_amd_afc_state_parse_table(state1, table, &error));
	g_assert_no_error(error);
	g_assert_true(fu_amd_afc_state_add_bios_settings(state1, &error));
	g_assert_no_error(error);
	setting = fu_context_get_bios_setting(ctx1, "com.amd-afc.Setup.Main.Feature.0020");
	g_assert_nonnull(setting);
	ret = fu_bios_setting_write_value(setting, "Enabled", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	variable = fu_efivars_get_data_bytes(fu_context_get_efivars(ctx1),
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
	g_assert_cmpuint(fu_struct_amd_afc_config_header_get_revision(st_hdr), ==, 0x1060);
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
	setting = fu_context_get_bios_setting(ctx1, FWUPD_BIOS_SETTING_PENDING_REBOOT);
	g_assert_null(setting);
	g_assert_true(fu_context_has_flag(ctx1, FU_CONTEXT_FLAG_PENDING_REBOOT));

	/* a separate process changing another setting preserves the first entry */
	g_assert_true(fu_amd_afc_copy_variable(ctx1, ctx2, &error));
	g_assert_no_error(error);
	g_assert_true(fu_amd_afc_state_parse_table(state2, table, &error));
	g_assert_no_error(error);
	g_assert_true(fu_amd_afc_state_add_bios_settings(state2, &error));
	g_assert_no_error(error);
	setting = fu_context_get_bios_setting(ctx2, "com.amd-afc.Setup.Sibling.0021");
	g_assert_nonnull(setting);
	ret = fu_bios_setting_write_value(setting, "Enabled", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_clear_pointer(&variable, g_bytes_unref);
	variable = fu_efivars_get_data_bytes(fu_context_get_efivars(ctx2),
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
	g_assert_true(fu_amd_afc_copy_variable(ctx2, ctx3, &error));
	g_assert_no_error(error);
	g_assert_true(fu_amd_afc_state_parse_table(state3, table, &error));
	g_assert_no_error(error);
	g_assert_true(fu_amd_afc_state_add_bios_settings(state3, &error));
	g_assert_no_error(error);
	setting = fu_context_get_bios_setting(ctx3, "com.amd-afc.Setup.Main.Feature.0020");
	g_assert_nonnull(setting);
	ret = fu_bios_setting_write_value(setting, "Enabled", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_clear_pointer(&variable, g_bytes_unref);
	variable = fu_efivars_get_data_bytes(fu_context_get_efivars(ctx3),
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
	g_test_add_func("/amd-afc/parse/invalid-extended-string",
			fu_amd_afc_invalid_extended_string_func);
	g_test_add_func("/amd-afc/config-id/overflow", fu_amd_afc_config_id_overflow_func);
	g_test_add_func("/amd-afc/write", fu_amd_afc_write_func);
	return g_test_run();
}
