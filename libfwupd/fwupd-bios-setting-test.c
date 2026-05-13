/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-bios-setting.h"
#include "fwupd-codec.h"
#include "fwupd-error.h"
#include "fwupd-test.h"

static void
fwupd_bios_settings_func(void)
{
	gboolean ret;
	g_autofree gchar *str1 = NULL;
	g_autofree gchar *str2 = NULL;
	g_autofree gchar *str3 = NULL;
	g_autofree gchar *str4 = NULL;
	g_autofree gchar *json1 = NULL;
	g_autofree gchar *json2 = NULL;
	g_autoptr(FwupdBiosSetting) attr1 = fwupd_bios_setting_new("foo", "/path/to/bar");
	g_autoptr(FwupdBiosSetting) attr2 = fwupd_bios_setting_new(NULL, NULL);
	g_autoptr(FwupdBiosSetting) attr3 = fwupd_bios_setting_new(NULL, NULL);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) data1 = NULL;
	g_autoptr(GVariant) data2 = NULL;

	g_assert_cmpstr(fwupd_bios_setting_get_name(attr1), ==, "foo");
	fwupd_bios_setting_set_name(attr1, "UEFISecureBoot");
	g_assert_cmpstr(fwupd_bios_setting_get_name(attr1), ==, "UEFISecureBoot");

	fwupd_bios_setting_set_kind(attr1, FWUPD_BIOS_SETTING_KIND_ENUMERATION);
	g_assert_cmpint(fwupd_bios_setting_get_kind(attr1),
			==,
			FWUPD_BIOS_SETTING_KIND_ENUMERATION);

	fwupd_bios_setting_set_description(attr1, "Controls Secure boot");
	g_assert_cmpstr(fwupd_bios_setting_get_description(attr1), ==, "Controls Secure boot");
	fwupd_bios_setting_set_current_value(attr1, "Disabled");
	g_assert_cmpstr(fwupd_bios_setting_get_current_value(attr1), ==, "Disabled");

	fwupd_bios_setting_add_possible_value(attr1, "Disabled");
	fwupd_bios_setting_add_possible_value(attr1, "Enabled");
	g_assert_true(fwupd_bios_setting_has_possible_value(attr1, "Disabled"));
	g_assert_false(fwupd_bios_setting_has_possible_value(attr1, "NOT_GOING_TO_EXIST"));

	fwupd_bios_setting_set_filename(attr1, "current_value");
	g_assert_cmpstr(fwupd_bios_setting_get_filename(attr1), ==, "current_value");

	str1 = fwupd_codec_to_string(FWUPD_CODEC(attr1));
	ret = fu_test_compare_lines(str1,
				    "FwupdBiosSetting:\n"
				    "  Name:                 UEFISecureBoot\n"
				    "  Description:          Controls Secure boot\n"
				    "  Filename:             /path/to/bar\n"
				    "  BiosSettingType:      1\n"
				    "  BiosSettingCurrentValue: Disabled\n"
				    "  BiosSettingFilename:  current_value\n"
				    "  BiosSettingReadOnly:  False\n"
				    "  BiosSettingPossibleValues: Disabled\n"
				    "  BiosSettingPossibleValues: Enabled\n",
				    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* roundtrip GVariant */
	data1 = fwupd_codec_to_variant(FWUPD_CODEC(attr1), FWUPD_CODEC_FLAG_TRUSTED);
	ret = fwupd_codec_from_variant(FWUPD_CODEC(attr2), data1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	str2 = fwupd_codec_to_string(FWUPD_CODEC(attr2));
	ret = fu_test_compare_lines(str2,
				    "FwupdBiosSetting:\n"
				    "  Name:                 UEFISecureBoot\n"
				    "  Description:          Controls Secure boot\n"
				    "  Filename:             /path/to/bar\n"
				    "  BiosSettingType:      1\n"
				    "  BiosSettingCurrentValue: Disabled\n"
				    "  BiosSettingFilename:  current_value\n"
				    "  BiosSettingReadOnly:  False\n"
				    "  BiosSettingPossibleValues: Disabled\n"
				    "  BiosSettingPossibleValues: Enabled\n",
				    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* to JSON */
	json1 = fwupd_codec_to_json_string(FWUPD_CODEC(attr1), FWUPD_CODEC_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json1);
	ret = fu_test_compare_lines(json1,
				    "{\n"
				    "  \"Name\": \"UEFISecureBoot\",\n"
				    "  \"Description\": \"Controls Secure boot\",\n"
				    "  \"Filename\": \"/path/to/bar\",\n"
				    "  \"BiosSettingCurrentValue\": \"Disabled\",\n"
				    "  \"BiosSettingFilename\": \"current_value\",\n"
				    "  \"BiosSettingReadOnly\": false,\n"
				    "  \"BiosSettingType\": 1,\n"
				    "  \"BiosSettingPossibleValues\": [\n"
				    "    \"Disabled\",\n"
				    "    \"Enabled\"\n"
				    "  ]\n"
				    "}",
				    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* from JSON */
	ret = fwupd_codec_from_json_string(FWUPD_CODEC(attr2), json1, &error);
	if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_test_skip(error->message);
		return;
	}
	g_assert_no_error(error);
	g_assert_true(ret);

	str3 = fwupd_codec_to_string(FWUPD_CODEC(attr2));
	ret = fu_test_compare_lines(str3, str1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* make sure we filter CurrentValue if not trusted */
	data2 = fwupd_codec_to_variant(FWUPD_CODEC(attr1), FWUPD_CODEC_FLAG_NONE);
	ret = fwupd_codec_from_variant(FWUPD_CODEC(attr3), data2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	str4 = fwupd_codec_to_string(FWUPD_CODEC(attr3));
	ret = fu_test_compare_lines(str4,
				    "FwupdBiosSetting:\n"
				    "  Name:                 UEFISecureBoot\n"
				    "  Description:          Controls Secure boot\n"
				    "  Filename:             /path/to/bar\n"
				    "  BiosSettingType:      1\n"
				    "  BiosSettingFilename:  current_value\n"
				    "  BiosSettingReadOnly:  False\n"
				    "  BiosSettingPossibleValues: Disabled\n"
				    "  BiosSettingPossibleValues: Enabled\n",
				    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* convert to JSON */
	json2 = fwupd_codec_to_json_string(FWUPD_CODEC(attr1), FWUPD_CODEC_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json2);
	ret = fu_test_compare_lines(json2,
				    "{\n"
				    "  \"Name\": \"UEFISecureBoot\",\n"
				    "  \"Description\": \"Controls Secure boot\",\n"
				    "  \"Filename\": \"/path/to/bar\",\n"
				    "  \"BiosSettingCurrentValue\": \"Disabled\",\n"
				    "  \"BiosSettingFilename\": \"current_value\",\n"
				    "  \"BiosSettingReadOnly\": false,\n"
				    "  \"BiosSettingType\": 1,\n"
				    "  \"BiosSettingPossibleValues\": [\n"
				    "    \"Disabled\",\n"
				    "    \"Enabled\"\n"
				    "  ]\n"
				    "}",
				    &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/bios-settings", fwupd_bios_settings_func);
	return g_test_run();
}
