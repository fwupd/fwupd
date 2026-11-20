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

typedef struct {
	const gchar *appstream_id;
	const gchar *plugin;
	FwupdSecurityAttrResult result;
} FuSecurityAttrsTestEvidence;

#define TEST_EVIDENCE(id, plugin, result)                                                          \
	{FWUPD_SECURITY_ATTR_ID_##id, plugin, FWUPD_SECURITY_ATTR_RESULT_##result}

static void
fu_security_attrs_test_add_evidence(FuContext *ctx,
				    FuSecurityAttrs *attrs,
				    const FuSecurityAttrsTestEvidence *evidence,
				    gsize evidence_len)
{
	for (guint i = 0; i < evidence_len; i++) {
		fu_security_attrs_test_add_attr(ctx,
						attrs,
						evidence[i].appstream_id,
						evidence[i].plugin,
						evidence[i].result,
						TRUE);
	}
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
fu_security_attrs_test_weighted(FuSecurityAttrs *attrs,
				FuCpuVendor cpu_vendor,
				guint expected_score,
				guint expected_ceiling)
{
	g_autoptr(GVariant) report = NULL;
	g_autoptr(GError) error = NULL;
	guint ceiling;
	guint score;

	report = fu_security_attrs_calculate_weighted(attrs, 1, cpu_vendor, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_true(g_variant_lookup(report, "Ceiling", "u", &ceiling));
	g_assert_cmpuint(score, ==, expected_score);
	g_assert_cmpuint(ceiling, ==, expected_ceiling);
}

static void
fu_security_attrs_test_platform(FuSecurityAttrs *attrs,
				FuCpuVendor cpu_vendor,
				guint expected_score,
				guint expected_level,
				guint expected_ceiling)
{
	g_autoptr(FuSecurityAttrs) legacy = fu_security_attrs_copy(attrs);
	g_autoptr(GVariant) report = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *hsi = NULL;
	guint ceiling;
	guint level;
	guint score;

	fu_security_attrs_depsolve(legacy);
	hsi = fu_security_attrs_calculate_hsi(legacy, NULL, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi, ==, "HSI:4");
	report = fu_security_attrs_calculate_weighted(attrs, 1, cpu_vendor, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_true(g_variant_lookup(report, "Level", "u", &level));
	g_assert_true(g_variant_lookup(report, "Ceiling", "u", &ceiling));
	g_assert_cmpuint(score, ==, expected_score);
	g_assert_cmpuint(level, ==, expected_level);
	g_assert_cmpuint(ceiling, ==, expected_ceiling);
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
	g_clear_pointer(&before, g_variant_unref);
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
	guint ceiling;
	guint score;

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
	fu_security_attrs_test_weighted(attrs, FU_CPU_VENDOR_UNKNOWN, 100, 4);
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
	fu_security_attrs_test_weighted(attrs, FU_CPU_VENDOR_UNKNOWN, 100, 4);
	/* conflicting providers retain the failure */
	fu_security_attr_add_flag(esrt, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fu_security_attr_set_result(esrt, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	fu_security_attr_add_obsolete(esrt, FWUPD_SECURITY_ATTR_ID_BIOS_ROLLBACK_PROTECTION);
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
	fu_security_attrs_test_add_attr(ctx,
					attrs,
					FWUPD_SECURITY_ATTR_ID_UEFI_PK,
					"uefi_pk",
					FWUPD_SECURITY_ATTR_RESULT_VALID,
					TRUE);
	fu_security_attrs_test_add_attr(ctx,
					attrs,
					FWUPD_SECURITY_ATTR_ID_UEFI_DB_PRODUCTION,
					"uefi_db",
					FWUPD_SECURITY_ATTR_RESULT_VALID,
					TRUE);
	fu_security_attrs_test_add_attr(ctx,
					attrs,
					FWUPD_SECURITY_ATTR_ID_UEFI_SECUREBOOT,
					"uefi_capsule",
					FWUPD_SECURITY_ATTR_RESULT_ENABLED,
					TRUE);
	fu_security_attrs_test_weighted(attrs, FU_CPU_VENDOR_UNKNOWN, 200, 3);
	/* a second successful report does not add points */
	secureboot = fu_security_attrs_test_add_attr(ctx,
						     attrs,
						     FWUPD_SECURITY_ATTR_ID_UEFI_SECUREBOOT,
						     "uefi-second",
						     FWUPD_SECURITY_ATTR_RESULT_ENABLED,
						     TRUE);
	fu_security_attrs_test_weighted(attrs, FU_CPU_VENDOR_UNKNOWN, 200, 3);
	/* missing data on either report prevents the property from scoring */
	fu_security_attr_set_flags(secureboot,
				   FWUPD_SECURITY_ATTR_FLAG_SUCCESS |
				       FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA);
	fu_security_attrs_test_weighted(attrs, FU_CPU_VENDOR_UNKNOWN, 50, 2);
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
	g_autoptr(FuSecurityAttr) manufacturing = NULL;
	g_autoptr(GVariant) failed = NULL;
	g_autoptr(GVariant) report = NULL;
	g_autoptr(GError) error = NULL;
	FuSecurityAttr *otp = NULL;
	const FwupdSecurityAttrResult fatal_results[] = {
	    FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED,
	    FWUPD_SECURITY_ATTR_RESULT_NOT_VALID,
	};
	const gchar *failed_id;
	guint ceiling;
	guint level;
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
	fu_security_attrs_test_weighted(attrs, FU_CPU_VENDOR_UNKNOWN, 150, 4);

	/* unsupported OTP locking removes the combined award */
	fu_security_attr_set_result(otp, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
	g_clear_pointer(&report, g_variant_unref);
	g_clear_pointer(&failed, g_variant_unref);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_cmpuint(score, ==, 30);
	failed = g_variant_lookup_value(report, "Failed", G_VARIANT_TYPE_STRING_ARRAY);
	g_assert_cmpuint(g_variant_n_children(failed), ==, 1);
	g_variant_get_child(failed, 0, "&s", &failed_id);
	g_assert_cmpstr(failed_id, ==, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_OTP);

	/* unrelated vboot points must not hide a manufacturing-mode failure */
	fu_security_attr_set_result(otp, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	fu_security_attr_add_flag(otp, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
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
	manufacturing =
	    fu_security_attrs_get_by_appstream_id(attrs,
						  FWUPD_SECURITY_ATTR_ID_MEI_MANUFACTURING_MODE,
						  &error);
	g_assert_no_error(error);
	g_assert_nonnull(manufacturing);

	/* unverified evidence is conservative, but not a confirmed fatal state */
	fu_security_attr_set_result(manufacturing, FWUPD_SECURITY_ATTR_RESULT_UNKNOWN);
	fu_security_attr_set_flags(manufacturing,
				   FWUPD_SECURITY_ATTR_FLAG_SUCCESS |
				       FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA);
	fu_security_attrs_test_weighted(attrs, FU_CPU_VENDOR_UNKNOWN, 100, 2);

	for (guint i = 0; i < G_N_ELEMENTS(fatal_results); i++) {
		fu_security_attr_set_flags(manufacturing, FWUPD_SECURITY_ATTR_FLAG_NONE);
		fu_security_attr_set_result(manufacturing, fatal_results[i]);
		g_clear_pointer(&report, g_variant_unref);
		g_clear_pointer(&failed, g_variant_unref);
		report =
		    fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
		g_assert_no_error(error);
		g_assert_true(g_variant_lookup(report, "Score", "u", &score));
		g_assert_true(g_variant_lookup(report, "Level", "u", &level));
		g_assert_true(g_variant_lookup(report, "Ceiling", "u", &ceiling));
		g_assert_cmpuint(score, ==, 0);
		g_assert_cmpuint(level, ==, 0);
		g_assert_cmpuint(ceiling, ==, 0);
		failed = g_variant_lookup_value(report, "Failed", G_VARIANT_TYPE_STRING_ARRAY);
		g_assert_cmpuint(g_variant_n_children(failed), ==, 1);
		g_variant_get_child(failed, 0, "&s", &failed_id);
		g_assert_cmpstr(failed_id, ==, FWUPD_SECURITY_ATTR_ID_MEI_MANUFACTURING_MODE);
	}
}

static void
fu_security_attrs_weighted_verification_routes_func(void)
{
	const FuSecurityAttrsTestEvidence intel_baseline[] = {
	    TEST_EVIDENCE(ENCRYPTED_RAM, "cpu", ENABLED),
	    TEST_EVIDENCE(INTEL_GDS, "msr", ENABLED),
	    TEST_EVIDENCE(IOMMU, "iommu", ENABLED),
	    TEST_EVIDENCE(MEI_KEY_MANIFEST, "intel_mchi", VALID),
	    TEST_EVIDENCE(MEI_OVERRIDE_STRAP, "intel_me", LOCKED),
	    TEST_EVIDENCE(MEI_VERSION, "intel_me", VALID),
	    TEST_EVIDENCE(PREBOOT_DMA_PROTECTION, "acpi_dmar", ENABLED),
	    TEST_EVIDENCE(SMAP, "cpu", ENABLED),
	    TEST_EVIDENCE(SPI_BIOSWE, "pci_bcr", NOT_ENABLED),
	    TEST_EVIDENCE(SPI_BLE, "pci_bcr", ENABLED),
	    TEST_EVIDENCE(SPI_DESCRIPTOR, "mtd", LOCKED),
	    TEST_EVIDENCE(SPI_SMM_BWP, "pci_bcr", LOCKED),
	    TEST_EVIDENCE(SUSPEND_TO_RAM, "linux_sleep", NOT_ENABLED),
	    TEST_EVIDENCE(TPM_EMPTY_PCR, "tpm", VALID),
	    TEST_EVIDENCE(TPM_RECONSTRUCTION_PCR0, "tpm", VALID),
	    TEST_EVIDENCE(TPM_VERSION_20, "tpm", FOUND),
	    TEST_EVIDENCE(UEFI_BOOTSERVICE_VARS, "uefi_capsule", LOCKED),
	    TEST_EVIDENCE(UEFI_SECUREBOOT, "uefi_capsule", ENABLED),
	    TEST_EVIDENCE(UEFI_PK, "uefi_pk", VALID),
	    TEST_EVIDENCE(UEFI_DB_PRODUCTION, "uefi_db", VALID),
	    TEST_EVIDENCE(CET_ENABLED, "cpu", SUPPORTED),
	    TEST_EVIDENCE(CET_ACTIVE, "cpu", SUPPORTED),
	    TEST_EVIDENCE(UEFI_MEMORY_PROTECTION, "uefi_mok", LOCKED),
	    TEST_EVIDENCE(UEFI_NX_COMPAT, "uefi_mok", ENABLED),
	    TEST_EVIDENCE(HW_DISK_ENCRYPTION, "nvme", ENABLED),
	};
	const FuSecurityAttrsTestEvidence amd_baseline[] = {
	    TEST_EVIDENCE(AMD_ENTRY_SIGN, "pci_psp", LOCKED),
	    TEST_EVIDENCE(AMD_ROLLBACK_PROTECTION, "pci_psp", ENABLED),
	    TEST_EVIDENCE(AMD_SMM_LOCKED, "msr", LOCKED),
	    TEST_EVIDENCE(AMD_SPI_REPLAY_PROTECTION, "pci_psp", ENABLED),
	    TEST_EVIDENCE(AMD_SPI_WRITE_PROTECTION, "pci_psp", ENABLED),
	    TEST_EVIDENCE(ENCRYPTED_RAM, "pci_psp", ENCRYPTED),
	    TEST_EVIDENCE(IOMMU, "iommu", ENABLED),
	    TEST_EVIDENCE(PLATFORM_FUSED, "pci_psp", LOCKED),
	    TEST_EVIDENCE(PREBOOT_DMA_PROTECTION, "acpi_ivrs", ENABLED),
	    TEST_EVIDENCE(SMAP, "cpu", ENABLED),
	    TEST_EVIDENCE(HW_DISK_ENCRYPTION, "nvme", ENABLED),
	    TEST_EVIDENCE(TPM_EMPTY_PCR, "tpm", VALID),
	    TEST_EVIDENCE(TPM_RECONSTRUCTION_PCR0, "tpm", VALID),
	    TEST_EVIDENCE(TPM_VERSION_20, "tpm", FOUND),
	    TEST_EVIDENCE(UEFI_BOOTSERVICE_VARS, "uefi_capsule", LOCKED),
	    TEST_EVIDENCE(UEFI_SECUREBOOT, "uefi_capsule", ENABLED),
	    TEST_EVIDENCE(UEFI_PK, "uefi_pk", VALID),
	    TEST_EVIDENCE(UEFI_DB_PRODUCTION, "uefi_db", VALID),
	    TEST_EVIDENCE(SUSPEND_TO_RAM, "linux_sleep", NOT_ENABLED),
	    TEST_EVIDENCE(UEFI_MEMORY_PROTECTION, "uefi_mok", LOCKED),
	    TEST_EVIDENCE(UEFI_NX_COMPAT, "uefi_mok", ENABLED),
	    TEST_EVIDENCE(CET_ENABLED, "cpu", SUPPORTED),
	    TEST_EVIDENCE(CET_ACTIVE, "cpu", SUPPORTED),
	};
	const FuSecurityAttrsTestEvidence intel_boot_guard[] = {
	    TEST_EVIDENCE(INTEL_BOOTGUARD_ENABLED, "intel_me", ENABLED),
	    TEST_EVIDENCE(INTEL_BOOTGUARD_VERIFIED, "intel_me", VALID),
	    TEST_EVIDENCE(INTEL_BOOTGUARD_ACM, "intel_me", VALID),
	    TEST_EVIDENCE(INTEL_BOOTGUARD_POLICY, "intel_me", VALID),
	    TEST_EVIDENCE(INTEL_BOOTGUARD_OTP, "intel_me", VALID),
	    TEST_EVIDENCE(MEI_MANUFACTURING_MODE, "intel_me", LOCKED),
	};
	const FuSecurityAttrsTestEvidence intel_vboot[] = {
	    TEST_EVIDENCE(MEI_MANUFACTURING_MODE, "intel_me", LOCKED),
	    TEST_EVIDENCE(COREBOOT_VBOOT, "tpm", ENABLED),
	    TEST_EVIDENCE(MTD_LOCKED, "mtd", LOCKED),
	};
	const gchar *boot_guard[] = {
	    FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ENABLED,
	    FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_VERIFIED,
	    FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ACM,
	    FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_POLICY,
	    FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_OTP,
	    FWUPD_SECURITY_ATTR_ID_MEI_MANUFACTURING_MODE,
	};
	const struct {
		const gchar *ids[2];
		guint points;
	} paired_awards[] = {
	    {{FWUPD_SECURITY_ATTR_ID_CET_ENABLED, FWUPD_SECURITY_ATTR_ID_CET_ACTIVE}, 15},
	    {{FWUPD_SECURITY_ATTR_ID_UEFI_MEMORY_PROTECTION, FWUPD_SECURITY_ATTR_ID_UEFI_NX_COMPAT},
	     30},
	};
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FuSecurityAttr) otp = NULL;
	g_autoptr(GVariant) failed = NULL;
	g_autoptr(GVariant) report = NULL;
	g_autoptr(GError) error = NULL;
	FuSecurityAttr *db = NULL;
	FuSecurityAttr *debug_enabled = NULL;
	FuSecurityAttr *debug_locked = NULL;
	FuSecurityAttr *mtd = NULL;
	FuSecurityAttr *prerequisite = NULL;
	const gchar *failed_id;
	guint ceiling;
	guint score;

	g_test_message("firmware verification routes");
	/* vboot requires successful MTD locking */
	fu_security_attrs_test_add_attr(ctx,
					attrs,
					FWUPD_SECURITY_ATTR_ID_COREBOOT_VBOOT,
					"test",
					FWUPD_SECURITY_ATTR_RESULT_ENABLED,
					TRUE);
	fu_security_attrs_test_weighted(attrs, FU_CPU_VENDOR_UNKNOWN, 0, 4);
	mtd = fu_security_attrs_test_add_attr(ctx,
					      attrs,
					      FWUPD_SECURITY_ATTR_ID_MTD_LOCKED,
					      "test",
					      FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED,
					      FALSE);
	fu_security_attrs_test_weighted(attrs, FU_CPU_VENDOR_UNKNOWN, 0, 4);
	fu_security_attr_set_result(mtd, FWUPD_SECURITY_ATTR_RESULT_LOCKED);
	fu_security_attr_add_flag(mtd, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fu_security_attrs_test_weighted(attrs, FU_CPU_VENDOR_UNKNOWN, 100, 4);

	/* independent firmware verification protections contribute separately */
	fu_security_attrs_test_add_successful(ctx, attrs, boot_guard, G_N_ELEMENTS(boot_guard));
	fu_security_attrs_test_weighted(attrs, FU_CPU_VENDOR_UNKNOWN, 250, 4);

	/* a failed Boot Guard route leaves the successful vboot route */
	otp = fu_security_attrs_get_by_appstream_id(attrs,
						    FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_OTP,
						    &error);
	g_assert_no_error(error);
	g_assert_nonnull(otp);
	fu_security_attr_remove_flag(otp, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fu_security_attr_set_result(otp, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
	fu_security_attrs_test_weighted(attrs, FU_CPU_VENDOR_UNKNOWN, 130, 4);

	g_test_message("synthetic platform examples");
	/* synthetic Intel Boot Guard fixture; debug checks are conditional bonuses */
	g_clear_object(&otp);
	g_clear_object(&attrs);
	attrs = fu_security_attrs_new();
	fu_security_attrs_test_add_evidence(ctx,
					    attrs,
					    intel_baseline,
					    G_N_ELEMENTS(intel_baseline));
	debug_enabled =
	    fu_security_attrs_test_add_attr(ctx,
					    attrs,
					    FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_ENABLED,
					    "msr",
					    FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED,
					    TRUE);
	debug_locked = fu_security_attrs_test_add_attr(ctx,
						       attrs,
						       FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_LOCKED,
						       "msr",
						       FWUPD_SECURITY_ATTR_RESULT_LOCKED,
						       TRUE);
	fu_security_attrs_test_add_evidence(ctx,
					    attrs,
					    intel_boot_guard,
					    G_N_ELEMENTS(intel_boot_guard));
	fu_security_attrs_test_platform(attrs, FU_CPU_VENDOR_INTEL, 701, 4, 4);
	fu_security_attr_set_result(debug_enabled, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
	fu_security_attr_set_result(debug_locked, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
	fu_security_attrs_test_platform(attrs, FU_CPU_VENDOR_INTEL, 681, 4, 4);

	/* synthetic coreboot vboot fixture; debug checks are conditional bonuses */
	g_clear_object(&attrs);
	attrs = fu_security_attrs_new();
	fu_security_attrs_test_add_evidence(ctx,
					    attrs,
					    intel_baseline,
					    G_N_ELEMENTS(intel_baseline));
	debug_enabled =
	    fu_security_attrs_test_add_attr(ctx,
					    attrs,
					    FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_ENABLED,
					    "msr",
					    FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED,
					    TRUE);
	debug_locked = fu_security_attrs_test_add_attr(ctx,
						       attrs,
						       FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_LOCKED,
						       "msr",
						       FWUPD_SECURITY_ATTR_RESULT_LOCKED,
						       TRUE);
	fu_security_attrs_test_add_evidence(ctx, attrs, intel_vboot, G_N_ELEMENTS(intel_vboot));
	fu_security_attrs_test_platform(attrs, FU_CPU_VENDOR_INTEL, 681, 4, 4);
	fu_security_attr_set_result(debug_enabled, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
	fu_security_attr_set_result(debug_locked, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
	fu_security_attrs_test_platform(attrs, FU_CPU_VENDOR_INTEL, 661, 4, 4);

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
	fu_security_attrs_test_weighted(attrs, FU_CPU_VENDOR_UNKNOWN, 220, 4);

	/* synthetic fixture using source-derived AMD Platform Secure Boot evidence */
	g_clear_object(&attrs);
	attrs = fu_security_attrs_new();
	fu_security_attrs_test_add_evidence(ctx, attrs, amd_baseline, G_N_ELEMENTS(amd_baseline));
	/* without PSB this fixture also crosses the shared level 4 threshold */
	fu_security_attrs_test_platform(attrs, FU_CPU_VENDOR_AMD, 681, 4, 4);
	fu_security_attrs_test_add_attr(ctx,
					attrs,
					FWUPD_SECURITY_ATTR_ID_AMD_PLATFORM_SECURE_BOOT,
					"pci_psp",
					FWUPD_SECURITY_ATTR_RESULT_ENABLED,
					TRUE);
	fu_security_attrs_test_platform(attrs, FU_CPU_VENDOR_AMD, 801, 4, 4);

	g_test_message("compound awards");
	for (guint i = 0; i < G_N_ELEMENTS(paired_awards); i++) {
		g_clear_object(&attrs);
		attrs = fu_security_attrs_new();
		fu_security_attrs_test_add_successful(ctx, attrs, paired_awards[i].ids, 1);
		fu_security_attrs_test_weighted(attrs, FU_CPU_VENDOR_UNKNOWN, 0, 4);
		fu_security_attrs_test_add_successful(ctx, attrs, &paired_awards[i].ids[1], 1);
		fu_security_attrs_test_weighted(attrs,
						FU_CPU_VENDOR_UNKNOWN,
						paired_awards[i].points,
						4);
	}

	/* the runtime SPI barrier requires all three BCR checks */
	g_clear_object(&attrs);
	attrs = fu_security_attrs_new();
	fu_security_attrs_test_add_attr(ctx,
					attrs,
					FWUPD_SECURITY_ATTR_ID_SPI_BIOSWE,
					"pci_bcr",
					FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED,
					TRUE);
	prerequisite = fu_security_attrs_test_add_attr(ctx,
						       attrs,
						       FWUPD_SECURITY_ATTR_ID_SPI_BLE,
						       "pci_bcr",
						       FWUPD_SECURITY_ATTR_RESULT_ENABLED,
						       TRUE);
	fu_security_attrs_test_add_attr(ctx,
					attrs,
					FWUPD_SECURITY_ATTR_ID_SPI_SMM_BWP,
					"pci_bcr",
					FWUPD_SECURITY_ATTR_RESULT_LOCKED,
					TRUE);
	fu_security_attrs_test_add_attr(ctx,
					attrs,
					FWUPD_SECURITY_ATTR_ID_SPI_DESCRIPTOR,
					"mtd",
					FWUPD_SECURITY_ATTR_RESULT_LOCKED,
					TRUE);
	fu_security_attrs_test_weighted(attrs, FU_CPU_VENDOR_UNKNOWN, 80, 4);
	fu_security_attr_remove_flag(prerequisite, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fu_security_attr_set_result(prerequisite, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
	fu_security_attrs_test_weighted(attrs, FU_CPU_VENDOR_UNKNOWN, 20, 3);

	/* secure boot requires a valid platform key and no known test keys */
	g_clear_object(&attrs);
	attrs = fu_security_attrs_new();
	fu_security_attrs_test_add_attr(ctx,
					attrs,
					FWUPD_SECURITY_ATTR_ID_UEFI_SECUREBOOT,
					"uefi_capsule",
					FWUPD_SECURITY_ATTR_RESULT_ENABLED,
					TRUE);
	prerequisite = fu_security_attrs_test_add_attr(ctx,
						       attrs,
						       FWUPD_SECURITY_ATTR_ID_UEFI_PK,
						       "uefi_pk",
						       FWUPD_SECURITY_ATTR_RESULT_VALID,
						       TRUE);
	db = fu_security_attrs_test_add_attr(ctx,
					     attrs,
					     FWUPD_SECURITY_ATTR_ID_UEFI_DB_PRODUCTION,
					     "uefi_db",
					     FWUPD_SECURITY_ATTR_RESULT_VALID,
					     TRUE);
	fu_security_attrs_test_weighted(attrs, FU_CPU_VENDOR_UNKNOWN, 150, 4);
	fu_security_attr_remove_flag(prerequisite, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fu_security_attr_set_result(prerequisite, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
	fu_security_attrs_test_weighted(attrs, FU_CPU_VENDOR_UNKNOWN, 0, 2);
	fu_security_attr_set_result(prerequisite, FWUPD_SECURITY_ATTR_RESULT_VALID);
	fu_security_attr_add_flag(prerequisite, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fu_security_attr_remove_flag(db, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fu_security_attr_set_result(db, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
	fu_security_attrs_test_weighted(attrs, FU_CPU_VENDOR_UNKNOWN, 0, 2);

	g_test_message("not-applicable evidence");
	/* TPM presence alone only records the capability */
	g_clear_object(&attrs);
	attrs = fu_security_attrs_new();
	fu_security_attrs_test_add_attr(ctx,
					attrs,
					FWUPD_SECURITY_ATTR_ID_TPM_VERSION_20,
					"test",
					FWUPD_SECURITY_ATTR_RESULT_FOUND,
					TRUE);
	fu_security_attrs_test_weighted(attrs, FU_CPU_VENDOR_UNKNOWN, 1, 4);

	/* one unsupported debug check does not establish absence */
	g_clear_object(&attrs);
	attrs = fu_security_attrs_new();
	fu_security_attrs_test_add_attr(ctx,
					attrs,
					FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_LOCKED,
					"msr",
					FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED,
					TRUE);
	fu_security_attrs_test_weighted(attrs, FU_CPU_VENDOR_UNKNOWN, 0, 3);

	/* the MSR producer reports both checks when the interface is absent */
	fu_security_attrs_test_add_attr(ctx,
					attrs,
					FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_ENABLED,
					"msr",
					FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED,
					TRUE);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_true(g_variant_lookup(report, "Ceiling", "u", &ceiling));
	g_assert_cmpuint(score, ==, 0);
	g_assert_cmpuint(ceiling, ==, 4);
	failed = g_variant_lookup_value(report, "Failed", G_VARIANT_TYPE_STRING_ARRAY);
	g_assert_cmpuint(g_variant_n_children(failed), ==, 0);

	/* not-applicable evidence does not conceal another provider's failure */
	fu_security_attrs_test_add_attr(ctx,
					attrs,
					FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_LOCKED,
					"other",
					FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED,
					FALSE);
	g_clear_pointer(&report, g_variant_unref);
	g_clear_pointer(&failed, g_variant_unref);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_true(g_variant_lookup(report, "Ceiling", "u", &ceiling));
	g_assert_cmpuint(score, ==, 0);
	g_assert_cmpuint(ceiling, ==, 3);
	failed = g_variant_lookup_value(report, "Failed", G_VARIANT_TYPE_STRING_ARRAY);
	g_assert_cmpuint(g_variant_n_children(failed), ==, 1);
	g_variant_get_child(failed, 0, "&s", &failed_id);
	g_assert_cmpstr(failed_id, ==, FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_LOCKED);
}

static void
fu_security_attrs_weighted_required_func(void)
{
	const gchar *intel_failed[] = {
	    FWUPD_SECURITY_ATTR_ID_SPI_BIOSWE,
	    FWUPD_SECURITY_ATTR_ID_SPI_BLE,
	    FWUPD_SECURITY_ATTR_ID_SPI_DESCRIPTOR,
	    FWUPD_SECURITY_ATTR_ID_SPI_SMM_BWP,
	};
	const gchar *amd_failed[] = {
	    FWUPD_SECURITY_ATTR_ID_AMD_SPI_REPLAY_PROTECTION,
	    FWUPD_SECURITY_ATTR_ID_AMD_SPI_WRITE_PROTECTION,
	};
	const struct {
		FuCpuVendor vendor;
		const gchar *const *failed_ids;
		gsize failed_ids_len;
		guint ceiling;
	} cases[] = {
	    {FU_CPU_VENDOR_UNKNOWN, NULL, 0, 4},
	    {FU_CPU_VENDOR_INTEL, intel_failed, G_N_ELEMENTS(intel_failed), 3},
	    {FU_CPU_VENDOR_AMD, amd_failed, G_N_ELEMENTS(amd_failed), 3},
	};
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();

	for (guint i = 0; i < G_N_ELEMENTS(cases); i++) {
		g_autoptr(GVariant) report = NULL;
		g_autoptr(GVariant) failed = NULL;
		g_autoptr(GError) error = NULL;
		g_autofree const gchar **failed_ids = NULL;
		guint ceiling;

		report = fu_security_attrs_calculate_weighted(attrs, 1, cases[i].vendor, &error);
		g_assert_no_error(error);
		g_assert_true(g_variant_lookup(report, "Ceiling", "u", &ceiling));
		g_assert_cmpuint(ceiling, ==, cases[i].ceiling);
		failed = g_variant_lookup_value(report, "Failed", G_VARIANT_TYPE_STRING_ARRAY);
		g_assert_cmpuint(g_variant_n_children(failed), ==, cases[i].failed_ids_len);
		failed_ids = fwupd_variant_get_strv(failed);
		for (guint j = 0; j < cases[i].failed_ids_len; j++)
			g_assert_true(g_strv_contains(failed_ids, cases[i].failed_ids[j]));
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
	    FWUPD_SECURITY_ATTR_ID_UEFI_PK,
	    FWUPD_SECURITY_ATTR_ID_UEFI_DB_PRODUCTION,
	    FWUPD_SECURITY_ATTR_ID_TPM_RECONSTRUCTION_PCR0,
	    FWUPD_SECURITY_ATTR_ID_MEI_VERSION,
	};
	const gchar *level3[] = {
	    FWUPD_SECURITY_ATTR_ID_CET_ENABLED,
	    FWUPD_SECURITY_ATTR_ID_CET_ACTIVE,
	    FWUPD_SECURITY_ATTR_ID_MEI_MANUFACTURING_MODE,
	    FWUPD_SECURITY_ATTR_ID_PREBOOT_DMA_PROTECTION,
	};
	const gchar *level4[] = {
	    FWUPD_SECURITY_ATTR_ID_AMD_ENTRY_SIGN,
	    FWUPD_SECURITY_ATTR_ID_PLATFORM_FUSED,
	    FWUPD_SECURITY_ATTR_ID_TPM_EMPTY_PCR,
	};
	const struct {
		const gchar *const *ids;
		gsize ids_len;
		guint score;
		guint level;
	} levels[] = {
	    {level1, G_N_ELEMENTS(level1), 300, 1},
	    {level2, G_N_ELEMENTS(level2), 500, 2},
	    {level3, G_N_ELEMENTS(level3), 575, 3},
	    {level4, G_N_ELEMENTS(level4), 650, 4},
	};
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(GVariant) report = NULL;
	g_autoptr(GError) error = NULL;
	guint ceiling;
	guint level;
	guint score;

	for (guint i = 0; i < G_N_ELEMENTS(levels); i++) {
		fu_security_attrs_test_add_successful(ctx, attrs, levels[i].ids, levels[i].ids_len);
		g_clear_pointer(&report, g_variant_unref);
		report =
		    fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
		g_assert_no_error(error);
		g_assert_true(g_variant_lookup(report, "Score", "u", &score));
		g_assert_true(g_variant_lookup(report, "Level", "u", &level));
		g_assert_cmpuint(score, ==, levels[i].score);
		g_assert_cmpuint(level, ==, levels[i].level);
	}

	/* a failed critical property caps a level already earned by other evidence */
	fu_security_attrs_test_add_attr(ctx,
					attrs,
					FWUPD_SECURITY_ATTR_ID_MEI_KEY_MANIFEST,
					"test",
					FWUPD_SECURITY_ATTR_RESULT_NOT_VALID,
					FALSE);
	g_clear_pointer(&report, g_variant_unref);
	report = fu_security_attrs_calculate_weighted(attrs, 1, FU_CPU_VENDOR_UNKNOWN, &error);
	g_assert_no_error(error);
	g_assert_true(g_variant_lookup(report, "Score", "u", &score));
	g_assert_true(g_variant_lookup(report, "Level", "u", &level));
	g_assert_true(g_variant_lookup(report, "Ceiling", "u", &ceiling));
	g_assert_cmpuint(score, ==, 650);
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
