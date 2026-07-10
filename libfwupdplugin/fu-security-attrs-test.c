/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-security-attrs-private.h"
#include "fu-test.h"

static void
fu_security_attrs_func(void)
{
	gboolean ret;
	g_autofree gchar *json1 = NULL;
	g_autofree gchar *json2 = NULL;
	g_autoptr(FuSecurityAttrs) attrs1 = fu_security_attrs_new();
	g_autoptr(FuSecurityAttrs) attrs2 = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr1 = fwupd_security_attr_new("org.fwupd.hsi.foo");
	g_autoptr(FwupdSecurityAttr) attr2 = fwupd_security_attr_new("org.fwupd.hsi.bar");
	g_autoptr(GError) error = NULL;

	fwupd_security_attr_set_plugin(attr1, "foo");
	fwupd_security_attr_set_created(attr1, 0);
	fwupd_security_attr_set_level(attr1, 1);
	fwupd_security_attr_set_plugin(attr2, "bar");
	fwupd_security_attr_set_created(attr2, 0);
	fwupd_security_attr_set_level(attr2, 2);
	fu_security_attrs_append(attrs1, attr1);
	fu_security_attrs_append(attrs1, attr2);

	json1 = fwupd_codec_to_json_string(FWUPD_CODEC(attrs1), FWUPD_CODEC_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json1);
	ret = fu_test_compare_lines(
	    json1,
	    "{\n"
	    "  \"SecurityAttributes\": [\n"
	    "    {\n"
	    "      \"AppstreamId\": \"org.fwupd.hsi.foo\",\n"
	    "      \"HsiLevel\": 1,\n"
	    "      \"Plugin\": \"foo\",\n"
	    "      \"Uri\": "
	    "\"https://fwupd.github.io/libfwupdplugin/hsi.html#org.fwupd.hsi.foo\"\n"
	    "    },\n"
	    "    {\n"
	    "      \"AppstreamId\": \"org.fwupd.hsi.bar\",\n"
	    "      \"HsiLevel\": 2,\n"
	    "      \"Plugin\": \"bar\",\n"
	    "      \"Uri\": "
	    "\"https://fwupd.github.io/libfwupdplugin/hsi.html#org.fwupd.hsi.bar\"\n"
	    "    }\n"
	    "  ]\n"
	    "}",
	    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fwupd_codec_from_json_string(FWUPD_CODEC(attrs2), json1, &error);
	if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_test_skip(error->message);
		return;
	}
	g_assert_no_error(error);
	g_assert_true(ret);

	json2 = fwupd_codec_to_json_string(FWUPD_CODEC(attrs2), FWUPD_CODEC_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json2);
	ret = g_strcmp0(json2, json1) == 0;
	g_assert_true(ret);
}

static void
fu_security_attrs_hsi_func(void)
{
	g_autofree gchar *hsi1 = NULL;
	g_autofree gchar *hsi2 = NULL;
	g_autofree gchar *hsi3 = NULL;
	g_autofree gchar *hsi4 = NULL;
	g_autofree gchar *hsi5 = NULL;
	g_autofree gchar *hsi6 = NULL;
	g_autofree gchar *hsi7 = NULL;
	g_autofree gchar *hsi8 = NULL;
	g_autofree gchar *hsi9 = NULL;
	g_autofree gchar *expected_hsi9 = NULL;
	g_autoptr(FuSecurityAttrs) attrs = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* no attrs */
	attrs = fu_security_attrs_new();
	hsi1 = fu_security_attrs_calculate_hsi(attrs, NULL, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi1, ==, "HSI:0");

	/* just success from HSI:1 */
	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_SPI_BIOSWE);
	fwupd_security_attr_set_plugin(attr, "test");
	fwupd_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_url(attr, "http://test");
	fu_security_attrs_append(attrs, attr);
	hsi2 = fu_security_attrs_calculate_hsi(attrs, NULL, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi2, ==, "HSI:1");
	g_clear_object(&attr);

	/* add failed from HSI:2, so still HSI:1 */
	attr = fwupd_security_attr_new("org.fwupd.hsi.PRX");
	fwupd_security_attr_set_plugin(attr, "test");
	fwupd_security_attr_set_fwupd_version(attr, "2.0.7");
	fwupd_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT);
	fwupd_security_attr_set_url(attr, "http://test");
	fu_security_attrs_append(attrs, attr);
	hsi3 = fu_security_attrs_calculate_hsi(attrs, NULL, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi3, ==, "HSI:1");
	g_clear_object(&attr);

	/* add an implicit obsolete via duplication */
	attr = fwupd_security_attr_new("org.fwupd.hsi.PRX");
	fwupd_security_attr_set_plugin(attr, "other-plugin");
	fwupd_security_attr_set_fwupd_version(attr, "2.0.7");
	fwupd_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT);
	fwupd_security_attr_set_url(attr, "http://other-plugin");
	fu_security_attrs_append(attrs, attr);
	fu_security_attrs_depsolve(attrs);
	hsi4 = fu_security_attrs_calculate_hsi(attrs, NULL, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi4, ==, "HSI:1");
	g_assert_true(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_OBSOLETED));
	g_clear_object(&attr);

	/* add attr from HSI:3, obsoleting the failure */
	attr = fwupd_security_attr_new("org.fwupd.hsi.BIOSGuard");
	fwupd_security_attr_set_plugin(attr, "test");
	fwupd_security_attr_set_fwupd_version(attr, "2.0.7");
	fwupd_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_THEORETICAL);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_add_obsolete(attr, "org.fwupd.hsi.PRX");
	fwupd_security_attr_set_url(attr, "http://test");
	fu_security_attrs_append(attrs, attr);
	fu_security_attrs_depsolve(attrs);
	hsi5 = fu_security_attrs_calculate_hsi(attrs, NULL, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi5, ==, "HSI:3");
	g_clear_object(&attr);

	/* add taint that was fine */
	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_FWUPD_PLUGINS);
	fwupd_security_attr_set_plugin(attr, "test");
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);
	fwupd_security_attr_set_url(attr, "http://test");
	fu_security_attrs_append(attrs, attr);
	hsi6 = fu_security_attrs_calculate_hsi(attrs, NULL, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi6, ==, "HSI:3");
	g_clear_object(&attr);

	/* add updates and attestation */
	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_FWUPD_UPDATES);
	fwupd_security_attr_set_plugin(attr, "test");
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_url(attr, "http://test");
	fu_security_attrs_append(attrs, attr);
	hsi7 = fu_security_attrs_calculate_hsi(attrs, NULL, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi7, ==, "HSI:3");
	g_clear_object(&attr);

	/* add issue that was uncool */
	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_KERNEL_SWAP);
	fwupd_security_attr_set_plugin(attr, "test");
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);
	fwupd_security_attr_set_url(attr, "http://test");
	fu_security_attrs_append(attrs, attr);
	hsi8 = fu_security_attrs_calculate_hsi(attrs, NULL, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi8, ==, "HSI:3!");
	g_clear_object(&attr);

	/* show version in the attribute */
	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_KERNEL_SWAP);
	fwupd_security_attr_set_plugin(attr, "test");
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);
	fwupd_security_attr_set_url(attr, "http://test");
	fu_security_attrs_append(attrs, attr);
	hsi9 = fu_security_attrs_calculate_hsi(attrs, NULL, FU_SECURITY_ATTRS_FLAG_ADD_VERSION);
	expected_hsi9 = g_strdup_printf("HSI:3! (v%d.%d.%d)",
					FWUPD_MAJOR_VERSION,
					FWUPD_MINOR_VERSION,
					FWUPD_MICRO_VERSION);
	g_assert_cmpstr(hsi9, ==, expected_hsi9);
	g_clear_object(&attr);
}

static void
fu_security_attrs_test_add_attr(FuSecurityAttrs *attrs,
				const gchar *appstream_id,
				const gchar *plugin,
				FwupdSecurityAttrResult result,
				gboolean success)
{
	g_autoptr(FwupdSecurityAttr) attr = fwupd_security_attr_new(appstream_id);

	fwupd_security_attr_set_plugin(attr, plugin);
	fwupd_security_attr_set_result(attr, result);
	if (success)
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fu_security_attrs_append(attrs, attr);
}

static void
fu_security_attrs_hsi_suspend_to_ram_func(void)
{
	/* suspend-to-ram with encrypted RAM replaces s2idle */
	{
		g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
		g_autoptr(FwupdSecurityAttr) attr_s2idle = NULL;
		g_autoptr(FwupdSecurityAttr) attr_s3 = NULL;

		fu_security_attrs_test_add_attr(attrs,
						FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_IDLE,
						"acpi-facp",
						FWUPD_SECURITY_ATTR_RESULT_ENABLED,
						TRUE);
		fu_security_attrs_test_add_attr(attrs,
						FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_RAM,
						"linux-sleep",
						FWUPD_SECURITY_ATTR_RESULT_ENABLED,
						FALSE);
		fu_security_attrs_test_add_attr(attrs,
						FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM,
						"msr",
						FWUPD_SECURITY_ATTR_RESULT_ENCRYPTED,
						TRUE);
		attr_s2idle =
		    fu_security_attrs_get_by_appstream_id(attrs,
							  FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_IDLE,
							  NULL);
		attr_s3 =
		    fu_security_attrs_get_by_appstream_id(attrs,
							  FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_RAM,
							  NULL);
		g_assert_nonnull(attr_s2idle);
		g_assert_nonnull(attr_s3);
		fwupd_security_attr_add_flag(attr_s3, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW);
		fwupd_security_attr_add_flag(attr_s3, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_OS);
		fu_security_attrs_depsolve(attrs);
		g_assert_true(
		    fwupd_security_attr_has_flag(attr_s3, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
		g_assert_false(
		    fwupd_security_attr_has_flag(attr_s3,
						 FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW));
		g_assert_false(
		    fwupd_security_attr_has_flag(attr_s3,
						 FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_OS));
		g_assert_true(
		    fwupd_security_attr_has_flag(attr_s2idle, FWUPD_SECURITY_ATTR_FLAG_OBSOLETED));
	}

	/* RAM must be encrypted */
	{
		g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
		g_autoptr(FwupdSecurityAttr) attr_s2idle = NULL;
		g_autoptr(FwupdSecurityAttr) attr_s3 = NULL;

		fu_security_attrs_test_add_attr(attrs,
						FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_IDLE,
						"acpi-facp",
						FWUPD_SECURITY_ATTR_RESULT_ENABLED,
						TRUE);
		fu_security_attrs_test_add_attr(attrs,
						FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_RAM,
						"linux-sleep",
						FWUPD_SECURITY_ATTR_RESULT_ENABLED,
						FALSE);
		fu_security_attrs_test_add_attr(attrs,
						FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM,
						"msr",
						FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED,
						FALSE);
		attr_s2idle =
		    fu_security_attrs_get_by_appstream_id(attrs,
							  FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_IDLE,
							  NULL);
		attr_s3 =
		    fu_security_attrs_get_by_appstream_id(attrs,
							  FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_RAM,
							  NULL);
		g_assert_nonnull(attr_s2idle);
		g_assert_nonnull(attr_s3);
		fu_security_attrs_depsolve(attrs);
		g_assert_false(
		    fwupd_security_attr_has_flag(attr_s3, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
		g_assert_false(
		    fwupd_security_attr_has_flag(attr_s2idle, FWUPD_SECURITY_ATTR_FLAG_OBSOLETED));
	}

	/* encrypted RAM still requires suspend-to-ram to be configured */
	{
		g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
		g_autoptr(FwupdSecurityAttr) attr_s2idle = NULL;

		fu_security_attrs_test_add_attr(attrs,
						FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_IDLE,
						"acpi-facp",
						FWUPD_SECURITY_ATTR_RESULT_ENABLED,
						TRUE);
		fu_security_attrs_test_add_attr(attrs,
						FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_RAM,
						"linux-sleep",
						FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED,
						TRUE);
		fu_security_attrs_test_add_attr(attrs,
						FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM,
						"msr",
						FWUPD_SECURITY_ATTR_RESULT_ENCRYPTED,
						TRUE);
		attr_s2idle =
		    fu_security_attrs_get_by_appstream_id(attrs,
							  FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_IDLE,
							  NULL);
		g_assert_nonnull(attr_s2idle);
		fu_security_attrs_depsolve(attrs);
		g_assert_false(
		    fwupd_security_attr_has_flag(attr_s2idle, FWUPD_SECURITY_ATTR_FLAG_OBSOLETED));
	}
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/security-attrs", fu_security_attrs_func);
	g_test_add_func("/fwupd/security-attrs/hsi", fu_security_attrs_hsi_func);
	g_test_add_func("/fwupd/security-attrs/hsi-suspend-to-ram",
			fu_security_attrs_hsi_suspend_to_ram_func);
	return g_test_run();
}
