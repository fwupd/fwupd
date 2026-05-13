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

static gint
fu_security_attrs_sort_cb(gconstpointer item1, gconstpointer item2)
{
	FwupdSecurityAttr *attr1 = *((FwupdSecurityAttr **)item1);
	FwupdSecurityAttr *attr2 = *((FwupdSecurityAttr **)item2);
	return g_strcmp0(fwupd_security_attr_get_appstream_id(attr1),
			 fwupd_security_attr_get_appstream_id(attr2));
}

static void
fu_security_attrs_minimize(FuSecurityAttrs *attrs)
{
	g_autoptr(GPtrArray) attrs_arr = fu_security_attrs_get_all_mutable(attrs);
	for (guint i = 0; i < attrs_arr->len; i++) {
		FwupdSecurityAttr *attr = g_ptr_array_index(attrs_arr, i);
		fwupd_security_attr_set_url(attr, NULL);
		fwupd_security_attr_set_plugin(attr, NULL);
		fwupd_security_attr_set_fwupd_version(attr, NULL);
		fwupd_security_attr_set_flags(attr, 0);
		fwupd_security_attr_set_level(attr, 0);
	}
	g_ptr_array_sort(attrs_arr, fu_security_attrs_sort_cb);
}

static void
fu_intel_me16_device_func(void)
{
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuIntelMeDevice) device = fu_intel_me_device_new(ctx);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FuStructIntelMeHfsts) st_hfsts1 = fu_struct_intel_me_hfsts_new();
	g_autoptr(FuStructIntelMeHfsts) st_hfsts6 = fu_struct_intel_me_hfsts_new();
	g_autoptr(FuStructIntelMeHfsts) st_hfsts = NULL;
	g_autoptr(GError) error = NULL;

	st_hfsts = fu_intel_me_device_get_hfsts(device, 1);
	g_assert_null(st_hfsts);

	/*
	 * ROG MAXIMUS Z790 HERO
	 * Version:              16.1.32.2473
	 * Family:               csme16
	 * Issue:                not-vulnerable
	 * Hfsts1:               0x90000245
	 * Hfsts2:               0x39850106
	 * Hfsts3:               0x20
	 * Hfsts4:               0x4000
	 * Hfsts6:               0x40200002
	 */
	fu_device_set_plugin(FU_DEVICE(device), "intel_me");
	fu_device_set_version(FU_DEVICE(device), "16.1.32.2473");
	g_assert_cmpint(fu_intel_me_device_get_family(device), ==, FU_INTEL_ME_FAMILY_CSME16);
	g_assert_cmpint(fu_intel_me_device_get_issue(device), ==, FU_INTEL_ME_ISSUE_NOT_VULNERABLE);

	fu_struct_intel_me_hfsts_set_value(st_hfsts1, 0x90000245);
	fu_struct_intel_me_hfsts_set_value(st_hfsts6, 0x40200002);
	fu_intel_me_device_set_hfsts(device, 1, st_hfsts1);
	fu_intel_me_device_set_hfsts(device, 6, st_hfsts6);

	fu_device_add_security_attrs(FU_DEVICE(device), attrs);
	fu_security_attrs_depsolve(attrs);
	fu_security_attrs_minimize(attrs);
	str = fwupd_codec_to_json_string(FWUPD_CODEC(attrs), FWUPD_CODEC_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(str);
	g_debug("%s", str);
	ret = fu_test_compare_lines(
	    str,
	    "{\n"
	    "  \"SecurityAttributes\": [\n"
	    "    {\n"
	    "      \"AppstreamId\": \"org.fwupd.hsi.IntelBootguard.Acm\",\n"
	    "      \"HsiResult\": \"not-valid\",\n"
	    "      \"HsiResultSuccess\": \"valid\"\n"
	    "    },\n"
	    "    {\n"
	    "      \"AppstreamId\": \"org.fwupd.hsi.IntelBootguard.Enabled\",\n"
	    "      \"HsiResult\": \"enabled\",\n"
	    "      \"HsiResultSuccess\": \"enabled\"\n"
	    "    },\n"
	    "    {\n"
	    "      \"AppstreamId\": \"org.fwupd.hsi.IntelBootguard.Otp\",\n"
	    "      \"HsiResult\": \"valid\",\n"
	    "      \"HsiResultSuccess\": \"valid\"\n"
	    "    },\n"
	    "    {\n"
	    "      \"AppstreamId\": \"org.fwupd.hsi.IntelBootguard.Policy\",\n"
	    "      \"HsiResult\": \"not-valid\",\n"
	    "      \"HsiResultSuccess\": \"valid\"\n"
	    "    },\n"
	    "    {\n"
	    "      \"AppstreamId\": \"org.fwupd.hsi.IntelBootguard.Verified\",\n"
	    "      \"HsiResult\": \"not-valid\",\n"
	    "      \"HsiResultSuccess\": \"valid\"\n"
	    "    },\n"
	    "    {\n"
	    "      \"AppstreamId\": \"org.fwupd.hsi.Mei.OverrideStrap\",\n"
	    "      \"HsiResult\": \"locked\",\n"
	    "      \"HsiResultSuccess\": \"locked\",\n"
	    "      \"kind\": \"csme16\"\n"
	    "    },\n"
	    "    {\n"
	    "      \"AppstreamId\": \"org.fwupd.hsi.Mei.Version\",\n"
	    "      \"HsiResult\": \"valid\",\n"
	    "      \"HsiResultSuccess\": \"valid\",\n"
	    "      \"kind\": \"csme16\",\n"
	    "      \"version\": \"16.1.32.2473\"\n"
	    "    }\n"
	    "  ]\n"
	    "}",
	    &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_intel_me18_device_func(void)
{
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuIntelMeDevice) device = fu_intel_me_device_new(ctx);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FuStructIntelMeHfsts) st_hfsts1 = fu_struct_intel_me_hfsts_new();
	g_autoptr(FuStructIntelMeHfsts) st_hfsts5 = fu_struct_intel_me_hfsts_new();
	g_autoptr(FuStructIntelMeHfsts) st_hfsts6 = fu_struct_intel_me_hfsts_new();
	g_autoptr(GError) error = NULL;

	/*
	 * Dell XPS 13 9350
	 * Version:              20.0.0.1322
	 * Family:               csme18
	 * Issue:                not-vulnerable
	 * Hfsts1:               0xA4000255
	 * Hfsts2:               0x80218500
	 * Hfsts3:               0x30
	 * Hfsts4:               0x4
	 * Hfsts5:               0x2f61f03
	 * Hfsts6:               0x0
	 */
	fu_device_set_plugin(FU_DEVICE(device), "intel_me");
	fu_device_set_version(FU_DEVICE(device), "20.0.0.1322");
	g_assert_cmpint(fu_intel_me_device_get_family(device), ==, FU_INTEL_ME_FAMILY_CSME18);
	g_assert_cmpint(fu_intel_me_device_get_issue(device), ==, FU_INTEL_ME_ISSUE_NOT_VULNERABLE);

	fu_struct_intel_me_hfsts_set_value(st_hfsts1, 0xA4000255);
	fu_struct_intel_me_hfsts_set_value(st_hfsts5, 0x40200002);
	fu_struct_intel_me_hfsts_set_value(st_hfsts6, 0x0);
	fu_intel_me_device_set_hfsts(device, 1, st_hfsts1);
	fu_intel_me_device_set_hfsts(device, 5, st_hfsts5);
	fu_intel_me_device_set_hfsts(device, 6, st_hfsts6);

	fu_device_add_security_attrs(FU_DEVICE(device), attrs);
	fu_security_attrs_depsolve(attrs);
	fu_security_attrs_minimize(attrs);
	str = fwupd_codec_to_json_string(FWUPD_CODEC(attrs), FWUPD_CODEC_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(str);
	g_debug("%s", str);
	ret = fu_test_compare_lines(
	    str,
	    "{\n"
	    "  \"SecurityAttributes\": [\n"
	    "    {\n"
	    "      \"AppstreamId\": \"org.fwupd.hsi.IntelBootguard.Acm\",\n"
	    "      \"HsiResult\": \"not-valid\",\n"
	    "      \"HsiResultSuccess\": \"valid\"\n"
	    "    },\n"
	    "    {\n"
	    "      \"AppstreamId\": \"org.fwupd.hsi.IntelBootguard.Enabled\",\n"
	    "      \"HsiResult\": \"enabled\",\n"
	    "      \"HsiResultSuccess\": \"enabled\"\n"
	    "    },\n"
	    "    {\n"
	    "      \"AppstreamId\": \"org.fwupd.hsi.IntelBootguard.Otp\",\n"
	    "      \"HsiResult\": \"not-valid\",\n"
	    "      \"HsiResultSuccess\": \"valid\"\n"
	    "    },\n"
	    "    {\n"
	    "      \"AppstreamId\": \"org.fwupd.hsi.Mei.ManufacturingMode\",\n"
	    "      \"HsiResult\": \"not-locked\",\n"
	    "      \"HsiResultSuccess\": \"locked\"\n"
	    "    },\n"
	    "    {\n"
	    "      \"AppstreamId\": \"org.fwupd.hsi.Mei.OverrideStrap\",\n"
	    "      \"HsiResult\": \"locked\",\n"
	    "      \"HsiResultSuccess\": \"locked\",\n"
	    "      \"kind\": \"csme18\"\n"
	    "    },\n"
	    "    {\n"
	    "      \"AppstreamId\": \"org.fwupd.hsi.Mei.Version\",\n"
	    "      \"HsiResult\": \"valid\",\n"
	    "      \"HsiResultSuccess\": \"valid\",\n"
	    "      \"kind\": \"csme18\",\n"
	    "      \"version\": \"20.0.0.1322\"\n"
	    "    }\n"
	    "  ]\n"
	    "}",
	    &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_intel_me16_device_hap_func(void)
{
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuIntelMeDevice) device = fu_intel_me_device_new(ctx);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FuStructIntelMeHfsts) st_hfsts1 = fu_struct_intel_me_hfsts_new();
	g_autoptr(FuStructIntelMeHfsts) st_hfsts5 = fu_struct_intel_me_hfsts_new();
	g_autoptr(FuStructIntelMeHfsts) st_hfsts6 = fu_struct_intel_me_hfsts_new();
	g_autoptr(GError) error = NULL;

	/*
	 * Alder Lake-P HAP set, Boot Guard enabled, fused (no manufacturing mode)
	 * Version:              16.0.0.0
	 * Family:               csme17
	 * Issue:                not-vulnerable
	 * Hfsts1:               0x80022054
	 * Hfsts2:               0x30284106
	 * Hfsts3:               0x00000020
	 * Hfsts4:               0x00006000
	 * Hfsts5:               0x00001f03
	 * Hfsts6:               0xc46003cf
	 */
	fu_device_set_plugin(FU_DEVICE(device), "intel_me");
	fu_device_set_version(FU_DEVICE(device), "16.0.0.0");
	g_assert_cmpint(fu_intel_me_device_get_family(device), ==, FU_INTEL_ME_FAMILY_CSME16);
	g_assert_cmpint(fu_intel_me_device_get_issue(device), ==, FU_INTEL_ME_ISSUE_NOT_VULNERABLE);

	fu_struct_intel_me_hfsts_set_value(st_hfsts1, 0x80022054);
	fu_struct_intel_me_hfsts_set_value(st_hfsts5, 0x00001f03);
	fu_struct_intel_me_hfsts_set_value(st_hfsts6, 0xc46003cf);
	fu_intel_me_device_set_hfsts(device, 1, st_hfsts1);
	fu_intel_me_device_set_hfsts(device, 5, st_hfsts5);
	fu_intel_me_device_set_hfsts(device, 6, st_hfsts6);

	fu_device_add_security_attrs(FU_DEVICE(device), attrs);
	fu_security_attrs_depsolve(attrs);
	fu_security_attrs_minimize(attrs);
	str = fwupd_codec_to_json_string(FWUPD_CODEC(attrs), FWUPD_CODEC_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(str);
	g_debug("%s", str);
	ret = fu_test_compare_lines(
	    str,
	    "{\n"
	    "  \"SecurityAttributes\": [\n"
	    "    {\n"
	    "      \"AppstreamId\": \"org.fwupd.hsi.IntelBootguard.Acm\",\n"
	    "      \"HsiResult\": \"valid\",\n"
	    "      \"HsiResultSuccess\": \"valid\"\n"
	    "    },\n"
	    "    {\n"
	    "      \"AppstreamId\": \"org.fwupd.hsi.IntelBootguard.Enabled\",\n"
	    "      \"HsiResult\": \"enabled\",\n"
	    "      \"HsiResultSuccess\": \"enabled\"\n"
	    "    },\n"
	    "    {\n"
	    "      \"AppstreamId\": \"org.fwupd.hsi.IntelBootguard.Otp\",\n"
	    "      \"HsiResult\": \"valid\",\n"
	    "      \"HsiResultSuccess\": \"valid\"\n"
	    "    },\n"
	    "    {\n"
	    "      \"AppstreamId\": \"org.fwupd.hsi.IntelBootguard.Policy\",\n"
	    "      \"HsiResult\": \"valid\",\n"
	    "      \"HsiResultSuccess\": \"valid\"\n"
	    "    },\n"
	    "    {\n"
	    "      \"AppstreamId\": \"org.fwupd.hsi.IntelBootguard.Verified\",\n"
	    "      \"HsiResult\": \"valid\",\n"
	    "      \"HsiResultSuccess\": \"valid\"\n"
	    "    },\n"
	    "    {\n"
	    "      \"AppstreamId\": \"org.fwupd.hsi.Mei.OverrideStrap\",\n"
	    "      \"HsiResult\": \"locked\",\n"
	    "      \"HsiResultSuccess\": \"locked\",\n"
	    "      \"kind\": \"csme16\"\n"
	    "    },\n"
	    "    {\n"
	    "      \"AppstreamId\": \"org.fwupd.hsi.Mei.Version\",\n"
	    "      \"HsiResult\": \"not-enabled\",\n"
	    "      \"HsiResultSuccess\": \"valid\",\n"
	    "      \"kind\": \"csme16\",\n"
	    "      \"version\": \"16.0.0.0\"\n"
	    "    }\n"
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
	g_test_add_func("/fwupd/intel-me/16-device", fu_intel_me16_device_func);
	g_test_add_func("/fwupd/intel-me/16-device/hap", fu_intel_me16_device_hap_func);
	g_test_add_func("/fwupd/intel-me/18-device", fu_intel_me18_device_func);
	return g_test_run();
}
