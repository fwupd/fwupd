/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"
#include "fu-security-attrs-private.h"
#include "fu-test.h"

static void
fu_security_attrs_func(void)
{
	gboolean ret;
	g_autofree gchar *json1 = NULL;
	g_autofree gchar *json2 = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuSecurityAttrs) attrs1 = fu_security_attrs_new();
	g_autoptr(FuSecurityAttrs) attrs2 = fu_security_attrs_new();
	g_autoptr(FuSecurityAttr) attr1 = fu_security_attr_new(ctx, "org.fwupd.hsi.foo");
	g_autoptr(FuSecurityAttr) attr2 = fu_security_attr_new(ctx, "org.fwupd.hsi.bar");
	g_autoptr(GError) error = NULL;

	fu_security_attr_set_plugin(attr1, "foo");
	fu_security_attr_set_created(attr1, 0);
	fu_security_attr_set_level(attr1, 1);
	fu_security_attr_set_plugin(attr2, "bar");
	fu_security_attr_set_created(attr2, 0);
	fu_security_attr_set_level(attr2, 2);
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
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuSecurityAttrs) attrs = NULL;
	g_autoptr(FuSecurityAttr) attr = NULL;

	/* no attrs */
	attrs = fu_security_attrs_new();
	hsi1 = fu_security_attrs_calculate_hsi(attrs, NULL, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi1, ==, "HSI:0");

	/* just success from HSI:1 */
	attr = fu_security_attr_new(ctx, FWUPD_SECURITY_ATTR_ID_SPI_BIOSWE);
	fu_security_attr_set_plugin(attr, "test");
	fu_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fu_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fu_security_attr_set_url(attr, "http://test");
	fu_security_attrs_append(attrs, attr);
	hsi2 = fu_security_attrs_calculate_hsi(attrs, NULL, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi2, ==, "HSI:1");
	g_clear_object(&attr);

	/* add failed from HSI:2, so still HSI:1 */
	attr = fu_security_attr_new(ctx, "org.fwupd.hsi.PRX");
	fu_security_attr_set_plugin(attr, "test");
	fu_security_attr_set_fwupd_version(attr, "2.0.7");
	fu_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT);
	fu_security_attr_set_url(attr, "http://test");
	fu_security_attrs_append(attrs, attr);
	hsi3 = fu_security_attrs_calculate_hsi(attrs, NULL, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi3, ==, "HSI:1");
	g_clear_object(&attr);

	/* add an implicit obsolete via duplication */
	attr = fu_security_attr_new(ctx, "org.fwupd.hsi.PRX");
	fu_security_attr_set_plugin(attr, "other-plugin");
	fu_security_attr_set_fwupd_version(attr, "2.0.7");
	fu_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT);
	fu_security_attr_set_url(attr, "http://other-plugin");
	fu_security_attrs_append(attrs, attr);
	fu_security_attrs_depsolve(attrs);
	hsi4 = fu_security_attrs_calculate_hsi(attrs, NULL, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi4, ==, "HSI:1");
	g_assert_true(fu_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_OBSOLETED));
	g_clear_object(&attr);

	/* add attr from HSI:3, obsoleting the failure */
	attr = fu_security_attr_new(ctx, "org.fwupd.hsi.BIOSGuard");
	fu_security_attr_set_plugin(attr, "test");
	fu_security_attr_set_fwupd_version(attr, "2.0.7");
	fu_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_THEORETICAL);
	fu_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fu_security_attr_add_obsolete(attr, "org.fwupd.hsi.PRX");
	fu_security_attr_set_url(attr, "http://test");
	fu_security_attrs_append(attrs, attr);
	fu_security_attrs_depsolve(attrs);
	hsi5 = fu_security_attrs_calculate_hsi(attrs, NULL, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi5, ==, "HSI:3");
	g_clear_object(&attr);

	/* add taint that was fine */
	attr = fu_security_attr_new(ctx, FWUPD_SECURITY_ATTR_ID_FWUPD_PLUGINS);
	fu_security_attr_set_plugin(attr, "test");
	fu_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fu_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);
	fu_security_attr_set_url(attr, "http://test");
	fu_security_attrs_append(attrs, attr);
	hsi6 = fu_security_attrs_calculate_hsi(attrs, NULL, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi6, ==, "HSI:3");
	g_clear_object(&attr);

	/* add updates and attestation */
	attr = fu_security_attr_new(ctx, FWUPD_SECURITY_ATTR_ID_FWUPD_UPDATES);
	fu_security_attr_set_plugin(attr, "test");
	fu_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fu_security_attr_set_url(attr, "http://test");
	fu_security_attrs_append(attrs, attr);
	hsi7 = fu_security_attrs_calculate_hsi(attrs, NULL, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi7, ==, "HSI:3");
	g_clear_object(&attr);

	/* add issue that was uncool */
	attr = fu_security_attr_new(ctx, FWUPD_SECURITY_ATTR_ID_KERNEL_SWAP);
	fu_security_attr_set_plugin(attr, "test");
	fu_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);
	fu_security_attr_set_url(attr, "http://test");
	fu_security_attrs_append(attrs, attr);
	hsi8 = fu_security_attrs_calculate_hsi(attrs, NULL, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi8, ==, "HSI:3!");
	g_clear_object(&attr);

	/* show version in the attribute */
	attr = fu_security_attr_new(ctx, FWUPD_SECURITY_ATTR_ID_KERNEL_SWAP);
	fu_security_attr_set_plugin(attr, "test");
	fu_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);
	fu_security_attr_set_url(attr, "http://test");
	fu_security_attrs_append(attrs, attr);
	hsi9 = fu_security_attrs_calculate_hsi(attrs, NULL, FU_SECURITY_ATTRS_FLAG_ADD_VERSION);
	expected_hsi9 = g_strdup_printf("HSI:3! (v%d.%d.%d)",
					FWUPD_MAJOR_VERSION,
					FWUPD_MINOR_VERSION,
					FWUPD_MICRO_VERSION);
	g_assert_cmpstr(hsi9, ==, expected_hsi9);
	g_clear_object(&attr);
}

/* returns a borrowed pointer that is valid while attrs owns it */
static FuSecurityAttr *
fu_security_attrs_test_add_attr(FuContext *ctx,
				FuSecurityAttrs *attrs,
				const gchar *appstream_id,
				const gchar *plugin,
				FwupdSecurityAttrResult result,
				gboolean success)
{
	g_autoptr(FuSecurityAttr) attr = fu_security_attr_new(ctx, appstream_id);

	fu_security_attr_set_plugin(attr, plugin);
	fu_security_attr_set_result(attr, result);
	if (success)
		fu_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fu_security_attrs_append(attrs, attr);
	return attr;
}

static void
fu_security_attrs_hsi_suspend_to_ram_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();

	/* suspend-to-ram with encrypted RAM replaces s2idle */
	{
		g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
		g_autoptr(FuSecurityAttr) attr_s2idle = NULL;
		g_autoptr(FuSecurityAttr) attr_s3 = NULL;

		fu_security_attrs_test_add_attr(ctx,
						attrs,
						FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_IDLE,
						"acpi-facp",
						FWUPD_SECURITY_ATTR_RESULT_ENABLED,
						TRUE);
		fu_security_attrs_test_add_attr(ctx,
						attrs,
						FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_RAM,
						"linux-sleep",
						FWUPD_SECURITY_ATTR_RESULT_ENABLED,
						FALSE);
		fu_security_attrs_test_add_attr(ctx,
						attrs,
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
		fu_security_attr_add_flag(attr_s3, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW);
		fu_security_attr_add_flag(attr_s3, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_OS);
		fu_security_attrs_depsolve(attrs);
		g_assert_true(fu_security_attr_has_flag(attr_s3, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
		g_assert_false(
		    fu_security_attr_has_flag(attr_s3, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW));
		g_assert_false(
		    fu_security_attr_has_flag(attr_s3, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_OS));
		g_assert_true(
		    fu_security_attr_has_flag(attr_s2idle, FWUPD_SECURITY_ATTR_FLAG_OBSOLETED));
	}

	/* RAM must be encrypted */
	{
		g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
		g_autoptr(FuSecurityAttr) attr_s2idle = NULL;
		g_autoptr(FuSecurityAttr) attr_s3 = NULL;

		fu_security_attrs_test_add_attr(ctx,
						attrs,
						FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_IDLE,
						"acpi-facp",
						FWUPD_SECURITY_ATTR_RESULT_ENABLED,
						TRUE);
		fu_security_attrs_test_add_attr(ctx,
						attrs,
						FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_RAM,
						"linux-sleep",
						FWUPD_SECURITY_ATTR_RESULT_ENABLED,
						FALSE);
		fu_security_attrs_test_add_attr(ctx,
						attrs,
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
		    fu_security_attr_has_flag(attr_s3, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
		g_assert_false(
		    fu_security_attr_has_flag(attr_s2idle, FWUPD_SECURITY_ATTR_FLAG_OBSOLETED));
	}

	/* encrypted RAM still requires suspend-to-ram to be configured */
	{
		g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
		g_autoptr(FuSecurityAttr) attr_s2idle = NULL;

		fu_security_attrs_test_add_attr(ctx,
						attrs,
						FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_IDLE,
						"acpi-facp",
						FWUPD_SECURITY_ATTR_RESULT_ENABLED,
						TRUE);
		fu_security_attrs_test_add_attr(ctx,
						attrs,
						FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_RAM,
						"linux-sleep",
						FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED,
						TRUE);
		fu_security_attrs_test_add_attr(ctx,
						attrs,
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
		    fu_security_attr_has_flag(attr_s2idle, FWUPD_SECURITY_ATTR_FLAG_OBSOLETED));
	}
}

static void
fu_security_attrs_weighted_original_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FuSecurityAttrs) original = NULL;
	g_autoptr(FuSecurityAttr) s3 = NULL;
	g_autoptr(GVariant) before = NULL;
	g_autoptr(GVariant) after = NULL;
	g_autoptr(GVariant) report = NULL;
	g_autoptr(GError) error = NULL;
	guint score;

	fu_security_attrs_test_add_attr(ctx,
					attrs,
					FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_RAM,
					"linux-sleep",
					FWUPD_SECURITY_ATTR_RESULT_ENABLED,
					FALSE);
	fu_security_attrs_test_add_attr(ctx,
					attrs,
					FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM,
					"msr",
					FWUPD_SECURITY_ATTR_RESULT_ENCRYPTED,
					TRUE);
	original = fu_security_attrs_copy(attrs);
	fu_security_attrs_depsolve(attrs);
	s3 = fu_security_attrs_get_by_appstream_id(attrs,
						   FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_RAM,
						   &error);
	g_assert_no_error(error);
	g_assert_true(fu_security_attr_has_flag(s3, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
	before = g_variant_ref_sink(fu_security_attrs_to_variant(original));
	report = fu_security_attrs_calculate_weighted(original, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_cmpuint(score, ==, 20);
	after = g_variant_ref_sink(fu_security_attrs_to_variant(original));
	g_assert_true(g_variant_equal(before, after));
}

static void
fu_security_attrs_weighted_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	FuSecurityAttr *esrt = NULL;
	FuSecurityAttr *oem = NULL;
	FuSecurityAttr *secureboot = NULL;
	g_autoptr(GVariant) failed = NULL;
	g_autoptr(GVariant) report = NULL;
	g_autoptr(GError) error = NULL;
	const gchar *failed_id;
	guint score, ceiling;

	esrt = fu_security_attrs_test_add_attr(ctx,
					       attrs,
					       FWUPD_SECURITY_ATTR_ID_BIOS_ROLLBACK_PROTECTION,
					       "uefi_capsule",
					       FWUPD_SECURITY_ATTR_RESULT_ENABLED,
					       TRUE);
	fu_security_attrs_test_add_attr(ctx,
					attrs,
					FWUPD_SECURITY_ATTR_ID_AMD_ROLLBACK_PROTECTION,
					"amd_psp",
					FWUPD_SECURITY_ATTR_RESULT_ENABLED,
					TRUE);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_cmpuint(score, ==, 100);
	g_clear_pointer(&report, g_variant_unref);
	/* a successful Dell result replaces a failed ESRT result */
	fu_security_attr_remove_flag(esrt, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fu_security_attr_set_result(esrt, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
	oem = fu_security_attrs_test_add_attr(ctx,
					      attrs,
					      FWUPD_SECURITY_ATTR_ID_BIOS_ROLLBACK_PROTECTION,
					      "dell",
					      FWUPD_SECURITY_ATTR_RESULT_ENABLED,
					      TRUE);
	fu_security_attr_add_obsolete(oem, FWUPD_SECURITY_ATTR_ID_BIOS_ROLLBACK_PROTECTION);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_cmpuint(score, ==, 100);
	g_clear_pointer(&report, g_variant_unref);
	/* a failed Dell result replaces a successful ESRT result */
	fu_security_attr_add_flag(esrt, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fu_security_attr_set_result(esrt, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	fu_security_attr_remove_flag(oem, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fu_security_attr_set_result(oem, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_true(g_variant_lookup(report, "Ceiling", "u", &ceiling));
	g_assert_cmpuint(score, ==, 50);
	g_assert_cmpuint(ceiling, ==, 3);
	failed = g_variant_lookup_value(report, "Failed", G_VARIANT_TYPE_STRING_ARRAY);
	g_assert_cmpuint(g_variant_n_children(failed), ==, 1);
	g_variant_get_child(failed, 0, "&s", &failed_id);
	g_assert_cmpstr(failed_id, ==, FWUPD_SECURITY_ATTR_ID_BIOS_ROLLBACK_PROTECTION);
	g_clear_pointer(&report, g_variant_unref);
	/* an unrelated result from the same plugin remains independent */
	secureboot = fu_security_attrs_test_add_attr(ctx,
						     attrs,
						     FWUPD_SECURITY_ATTR_ID_UEFI_SECUREBOOT,
						     "uefi_capsule",
						     FWUPD_SECURITY_ATTR_RESULT_ENABLED,
						     TRUE);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_cmpuint(score, ==, 150);
	g_clear_pointer(&report, g_variant_unref);
	/* a second successful report does not add points */
	secureboot = fu_security_attrs_test_add_attr(ctx,
						     attrs,
						     FWUPD_SECURITY_ATTR_ID_UEFI_SECUREBOOT,
						     "uefi-second",
						     FWUPD_SECURITY_ATTR_RESULT_ENABLED,
						     TRUE);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_cmpuint(score, ==, 150);
	g_clear_pointer(&report, g_variant_unref);
	/* a failure on either report prevents the property from scoring */
	fu_security_attr_remove_flag(secureboot, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fu_security_attr_set_result(secureboot, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_true(g_variant_lookup(report, "Ceiling", "u", &ceiling));
	g_assert_cmpuint(score, ==, 50);
	g_assert_cmpuint(ceiling, ==, 2);
	g_clear_pointer(&report, g_variant_unref);
	report = fu_security_attrs_calculate_weighted(attrs, 2, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_null(report);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
}

static void
fu_security_attrs_test_add_successful(FuContext *ctx,
				      FuSecurityAttrs *attrs,
				      const gchar *const *appstream_ids,
				      gsize appstream_ids_len)
{
	for (guint i = 0; i < appstream_ids_len; i++) {
		fu_security_attrs_test_add_attr(ctx,
						attrs,
						appstream_ids[i],
						"test",
						FWUPD_SECURITY_ATTR_RESULT_ENABLED,
						TRUE);
	}
}

static void
fu_security_attrs_weighted_boot_guard_func(void)
{
	const gchar *requirements[] = {
	    FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ENABLED,
	    FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_VERIFIED,
	    FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ACM,
	    FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_POLICY,
	    FWUPD_SECURITY_ATTR_ID_MEI_MANUFACTURING_MODE,
	};
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(GVariant) failed = NULL;
	g_autoptr(GVariant) report = NULL;
	g_autoptr(GError) error = NULL;
	FuSecurityAttr *otp = NULL;
	guint ceiling;
	guint score;

	/* missing OTP leaves only the independent manufacturing-mode award */
	fu_security_attrs_test_add_successful(ctx, attrs, requirements, G_N_ELEMENTS(requirements));
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_cmpuint(score, ==, 30);
	failed = g_variant_lookup_value(report, "Failed", G_VARIANT_TYPE_STRING_ARRAY);
	g_assert_cmpuint(g_variant_n_children(failed), ==, 0);

	/* complete Boot Guard adds 120 points */
	otp = fu_security_attrs_test_add_attr(ctx,
					      attrs,
					      FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_OTP,
					      "test",
					      FWUPD_SECURITY_ATTR_RESULT_ENABLED,
					      TRUE);
	g_clear_pointer(&report, g_variant_unref);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_cmpuint(score, ==, 150);

	/* unsupported OTP locking removes the combined award */
	fu_security_attr_remove_flag(otp, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fu_security_attr_set_result(otp, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
	g_clear_pointer(&report, g_variant_unref);
	g_clear_pointer(&failed, g_variant_unref);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_cmpuint(score, ==, 30);
	failed = g_variant_lookup_value(report, "Failed", G_VARIANT_TYPE_STRING_ARRAY);
	g_assert_cmpuint(g_variant_n_children(failed), ==, 1);
	{
		const gchar *failed_id;
		g_variant_get_child(failed, 0, "&s", &failed_id);
		g_assert_cmpstr(failed_id, ==, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_OTP);
	}

	/* manufacturing-mode failure removes both its own and the combined award */
	fu_security_attr_set_result(otp, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	fu_security_attr_add_flag(otp, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	{
		g_autoptr(FuSecurityAttr) manufacturing = fu_security_attrs_get_by_appstream_id(
		    attrs,
		    FWUPD_SECURITY_ATTR_ID_MEI_MANUFACTURING_MODE,
		    &error);
		g_assert_no_error(error);
		g_assert_nonnull(manufacturing);
		fu_security_attr_remove_flag(manufacturing, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
		fu_security_attr_set_result(manufacturing, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
	}
	g_clear_pointer(&report, g_variant_unref);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_true(g_variant_lookup(report, "Ceiling", "u", &ceiling));
	g_assert_cmpuint(score, ==, 0);
	g_assert_cmpuint(ceiling, ==, 2);
}

static void
fu_security_attrs_weighted_verification_routes_func(void)
{
	const gchar *boot_guard[] = {
	    FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ENABLED,
	    FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_VERIFIED,
	    FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ACM,
	    FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_POLICY,
	    FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_OTP,
	    FWUPD_SECURITY_ATTR_ID_MEI_MANUFACTURING_MODE,
	};
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FuSecurityAttr) otp = NULL;
	g_autoptr(GVariant) report = NULL;
	g_autoptr(GError) error = NULL;
	FuSecurityAttr *active = NULL;
	FuSecurityAttr *mtd = NULL;
	FuSecurityAttr *nx = NULL;
	guint score;

	/* vboot requires successful MTD locking */
	fu_security_attrs_test_add_attr(ctx,
					attrs,
					FWUPD_SECURITY_ATTR_ID_COREBOOT_VBOOT,
					"test",
					FWUPD_SECURITY_ATTR_RESULT_ENABLED,
					TRUE);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_cmpuint(score, ==, 0);
	mtd = fu_security_attrs_test_add_attr(ctx,
					      attrs,
					      FWUPD_SECURITY_ATTR_ID_MTD_LOCKED,
					      "test",
					      FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED,
					      FALSE);
	g_clear_pointer(&report, g_variant_unref);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_cmpuint(score, ==, 0);
	fu_security_attr_set_result(mtd, FWUPD_SECURITY_ATTR_RESULT_LOCKED);
	fu_security_attr_add_flag(mtd, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	g_clear_pointer(&report, g_variant_unref);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_cmpuint(score, ==, 100);

	/* independent firmware verification protections contribute separately */
	fu_security_attrs_test_add_successful(ctx, attrs, boot_guard, G_N_ELEMENTS(boot_guard));
	g_clear_pointer(&report, g_variant_unref);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_cmpuint(score, ==, 250);

	/* a failed Boot Guard route leaves the successful vboot route */
	otp = fu_security_attrs_get_by_appstream_id(attrs,
						    FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_OTP,
						    &error);
	g_assert_no_error(error);
	g_assert_nonnull(otp);
	fu_security_attr_remove_flag(otp, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fu_security_attr_set_result(otp, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
	g_clear_pointer(&report, g_variant_unref);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_cmpuint(score, ==, 130);

	/* AMD PSB and vboot with MTD locking both contribute */
	g_clear_object(&otp);
	g_clear_object(&attrs);
	attrs = fu_security_attrs_new();
	fu_security_attrs_test_add_attr(ctx,
					attrs,
					FWUPD_SECURITY_ATTR_ID_AMD_PLATFORM_SECURE_BOOT,
					"test",
					FWUPD_SECURITY_ATTR_RESULT_ENABLED,
					TRUE);
	fu_security_attrs_test_add_attr(ctx,
					attrs,
					FWUPD_SECURITY_ATTR_ID_COREBOOT_VBOOT,
					"test",
					FWUPD_SECURITY_ATTR_RESULT_ENABLED,
					TRUE);
	fu_security_attrs_test_add_attr(ctx,
					attrs,
					FWUPD_SECURITY_ATTR_ID_MTD_LOCKED,
					"test",
					FWUPD_SECURITY_ATTR_RESULT_LOCKED,
					TRUE);
	g_clear_pointer(&report, g_variant_unref);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_cmpuint(score, ==, 220);

	/* require platform CET support and active userspace support */
	g_clear_object(&attrs);
	attrs = fu_security_attrs_new();
	fu_security_attrs_test_add_attr(ctx,
					attrs,
					FWUPD_SECURITY_ATTR_ID_CET_ENABLED,
					"test",
					FWUPD_SECURITY_ATTR_RESULT_SUPPORTED,
					TRUE);
	g_clear_pointer(&report, g_variant_unref);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_cmpuint(score, ==, 0);
	active = fu_security_attrs_test_add_attr(ctx,
						 attrs,
						 FWUPD_SECURITY_ATTR_ID_CET_ACTIVE,
						 "test",
						 FWUPD_SECURITY_ATTR_RESULT_SUPPORTED,
						 TRUE);
	g_clear_pointer(&report, g_variant_unref);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_cmpuint(score, ==, 15);
	fu_security_attr_remove_flag(active, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fu_security_attr_set_result(active, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
	g_clear_pointer(&report, g_variant_unref);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_cmpuint(score, ==, 0);

	/* require firmware memory protection and an NX-compatible bootloader */
	g_clear_object(&attrs);
	attrs = fu_security_attrs_new();
	fu_security_attrs_test_add_attr(ctx,
					attrs,
					FWUPD_SECURITY_ATTR_ID_UEFI_MEMORY_PROTECTION,
					"test",
					FWUPD_SECURITY_ATTR_RESULT_LOCKED,
					TRUE);
	g_clear_pointer(&report, g_variant_unref);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_cmpuint(score, ==, 0);
	nx = fu_security_attrs_test_add_attr(ctx,
					     attrs,
					     FWUPD_SECURITY_ATTR_ID_UEFI_NX_COMPAT,
					     "test",
					     FWUPD_SECURITY_ATTR_RESULT_ENABLED,
					     TRUE);
	g_clear_pointer(&report, g_variant_unref);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_cmpuint(score, ==, 30);
	fu_security_attr_remove_flag(nx, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fu_security_attr_set_result(nx, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
	g_clear_pointer(&report, g_variant_unref);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_cmpuint(score, ==, 0);

	/* TPM presence alone only records the capability */
	g_clear_object(&attrs);
	attrs = fu_security_attrs_new();
	fu_security_attrs_test_add_attr(ctx,
					attrs,
					FWUPD_SECURITY_ATTR_ID_TPM_VERSION_20,
					"test",
					FWUPD_SECURITY_ATTR_RESULT_FOUND,
					TRUE);
	g_clear_pointer(&report, g_variant_unref);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_cmpuint(score, ==, 1);
}

static void
fu_security_attrs_weighted_required_func(void)
{
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(GVariant) report = NULL;
	g_autoptr(GVariant) failed = NULL;
	g_autoptr(GError) error = NULL;
	guint ceiling;

	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Ceiling", "u", &ceiling));
	g_assert_cmpuint(ceiling, ==, 4);
	failed = g_variant_lookup_value(report, "Failed", G_VARIANT_TYPE_STRING_ARRAY);
	g_assert_cmpuint(g_variant_n_children(failed), ==, 0);

	g_clear_pointer(&report, g_variant_unref);
	g_clear_pointer(&failed, g_variant_unref);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_INTEL, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Ceiling", "u", &ceiling));
	g_assert_cmpuint(ceiling, ==, 3);
	failed = g_variant_lookup_value(report, "Failed", G_VARIANT_TYPE_STRING_ARRAY);
	g_assert_cmpuint(g_variant_n_children(failed), ==, 4);
	{
		g_autofree const gchar **failed_ids = fwupd_variant_get_strv(failed);
		g_assert_nonnull(failed_ids);
		g_assert_true(g_strv_contains(failed_ids, FWUPD_SECURITY_ATTR_ID_SPI_BIOSWE));
		g_assert_true(g_strv_contains(failed_ids, FWUPD_SECURITY_ATTR_ID_SPI_BLE));
		g_assert_true(g_strv_contains(failed_ids, FWUPD_SECURITY_ATTR_ID_SPI_DESCRIPTOR));
		g_assert_true(g_strv_contains(failed_ids, FWUPD_SECURITY_ATTR_ID_SPI_SMM_BWP));
	}

	g_clear_pointer(&report, g_variant_unref);
	g_clear_pointer(&failed, g_variant_unref);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_AMD, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Ceiling", "u", &ceiling));
	g_assert_cmpuint(ceiling, ==, 3);
	failed = g_variant_lookup_value(report, "Failed", G_VARIANT_TYPE_STRING_ARRAY);
	g_assert_cmpuint(g_variant_n_children(failed), ==, 2);
	{
		g_autofree const gchar **failed_ids = fwupd_variant_get_strv(failed);
		g_assert_nonnull(failed_ids);
		g_assert_true(
		    g_strv_contains(failed_ids, FWUPD_SECURITY_ATTR_ID_AMD_SPI_REPLAY_PROTECTION));
		g_assert_true(
		    g_strv_contains(failed_ids, FWUPD_SECURITY_ATTR_ID_AMD_SPI_WRITE_PROTECTION));
	}
}

static void
fu_security_attrs_weighted_levels_func(void)
{
	const gchar *level1[] = {
	    FWUPD_SECURITY_ATTR_ID_AMD_PLATFORM_SECURE_BOOT,
	    FWUPD_SECURITY_ATTR_ID_AMD_SPI_WRITE_PROTECTION,
	    FWUPD_SECURITY_ATTR_ID_AMD_ROLLBACK_PROTECTION,
	    FWUPD_SECURITY_ATTR_ID_IOMMU,
	};
	const gchar *level2[] = {
	    FWUPD_SECURITY_ATTR_ID_UEFI_SECUREBOOT,
	    FWUPD_SECURITY_ATTR_ID_BIOS_ROLLBACK_PROTECTION,
	    FWUPD_SECURITY_ATTR_ID_TPM_RECONSTRUCTION_PCR0,
	    FWUPD_SECURITY_ATTR_ID_MEI_VERSION,
	};
	const gchar *level3[] = {
	    FWUPD_SECURITY_ATTR_ID_HW_DISK_ENCRYPTION,
	    FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_ENABLED,
	    FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_LOCKED,
	    FWUPD_SECURITY_ATTR_ID_PLATFORM_FUSED,
	    FWUPD_SECURITY_ATTR_ID_MEI_MANUFACTURING_MODE,
	    FWUPD_SECURITY_ATTR_ID_PREBOOT_DMA_PROTECTION,
	    FWUPD_SECURITY_ATTR_ID_SMAP,
	};
	const gchar *level4[] = {
	    FWUPD_SECURITY_ATTR_ID_SPI_BIOSWE,
	    FWUPD_SECURITY_ATTR_ID_SPI_BLE,
	    FWUPD_SECURITY_ATTR_ID_SPI_DESCRIPTOR,
	    FWUPD_SECURITY_ATTR_ID_SPI_SMM_BWP,
	    FWUPD_SECURITY_ATTR_ID_CET_ENABLED,
	    FWUPD_SECURITY_ATTR_ID_CET_ACTIVE,
	    FWUPD_SECURITY_ATTR_ID_UEFI_MEMORY_PROTECTION,
	    FWUPD_SECURITY_ATTR_ID_UEFI_NX_COMPAT,
	    FWUPD_SECURITY_ATTR_ID_TPM_EMPTY_PCR,
	    FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_RAM,
	};
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(GVariant) report = NULL;
	g_autoptr(GError) error = NULL;
	guint score, level, ceiling;

	fu_security_attrs_test_add_successful(ctx, attrs, level1, G_N_ELEMENTS(level1));
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_true(g_variant_lookup(report, "Level", "u", &level));
	g_assert_cmpuint(score, ==, 300);
	g_assert_cmpuint(level, ==, 1);

	fu_security_attrs_test_add_successful(ctx, attrs, level2, G_N_ELEMENTS(level2));
	g_clear_pointer(&report, g_variant_unref);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_true(g_variant_lookup(report, "Level", "u", &level));
	g_assert_cmpuint(score, ==, 500);
	g_assert_cmpuint(level, ==, 2);

	fu_security_attrs_test_add_successful(ctx, attrs, level3, G_N_ELEMENTS(level3));
	g_clear_pointer(&report, g_variant_unref);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_true(g_variant_lookup(report, "Level", "u", &level));
	g_assert_cmpuint(score, ==, 700);
	g_assert_cmpuint(level, ==, 3);

	fu_security_attrs_test_add_successful(ctx, attrs, level4, G_N_ELEMENTS(level4));
	g_clear_pointer(&report, g_variant_unref);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_true(g_variant_lookup(report, "Level", "u", &level));
	g_assert_cmpuint(score, ==, 850);
	g_assert_cmpuint(level, ==, 4);

	/* a failed critical property caps a level already earned by other evidence */
	fu_security_attrs_test_add_attr(ctx,
					attrs,
					FWUPD_SECURITY_ATTR_ID_UEFI_PK,
					"test",
					FWUPD_SECURITY_ATTR_RESULT_NOT_VALID,
					FALSE);
	g_clear_pointer(&report, g_variant_unref);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_true(g_variant_lookup(report, "Level", "u", &level));
	g_assert_true(g_variant_lookup(report, "Ceiling", "u", &ceiling));
	g_assert_cmpuint(score, ==, 850);
	g_assert_cmpuint(level, ==, 2);
	g_assert_cmpuint(ceiling, ==, 2);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/security-attrs", fu_security_attrs_func);
	g_test_add_func("/fwupd/security-attrs/hsi", fu_security_attrs_hsi_func);
	g_test_add_func("/fwupd/security-attrs/weighted", fu_security_attrs_weighted_func);
	g_test_add_func("/fwupd/security-attrs/weighted-boot-guard",
			fu_security_attrs_weighted_boot_guard_func);
	g_test_add_func("/fwupd/security-attrs/weighted-verification-routes",
			fu_security_attrs_weighted_verification_routes_func);
	g_test_add_func("/fwupd/security-attrs/weighted-levels",
			fu_security_attrs_weighted_levels_func);
	g_test_add_func("/fwupd/security-attrs/weighted-original",
			fu_security_attrs_weighted_original_func);
	g_test_add_func("/fwupd/security-attrs/weighted-required",
			fu_security_attrs_weighted_required_func);
	g_test_add_func("/fwupd/security-attrs/hsi-suspend-to-ram",
			fu_security_attrs_hsi_suspend_to_ram_func);
	return g_test_run();
}
