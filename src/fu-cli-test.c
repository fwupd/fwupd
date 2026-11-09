/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-cli-common.h"

static void
fu_cli_func(void)
{
	const gchar *tmp;
	g_autoptr(FwupdClient) client = fwupd_client_new();
	g_autoptr(FwupdDevice) device = fwupd_device_new();

	for (FwupdDeviceProblem i = 1; i < G_MAXUINT64; i <<= 1) {
		g_autofree gchar *str = NULL;
		tmp = fwupd_device_problem_to_string(i);
		if (tmp == NULL)
			break;
		str = fu_cli_device_problem_to_string(client, device, i);
		g_assert_nonnull(str);
	}
	for (FwupdReleaseFlags i = 1; i < G_MAXUINT64; i <<= 1) {
		tmp = fwupd_release_flag_to_string(i);
		if (tmp == NULL)
			break;
		g_assert_nonnull(fu_cli_release_flag_to_string(i));
	}
	for (FwupdRequestFlags i = 1; i < G_MAXUINT64; i <<= 1) {
		tmp = fwupd_request_flag_to_string(i);
		if (tmp == NULL)
			break;
		g_assert_nonnull(fu_cli_request_flag_to_string(i));
	}
	for (FwupdPluginFlags i = 1; i < G_MAXUINT64; i <<= 1) {
		g_autofree gchar *str = NULL;
		if (i == FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE || i == FWUPD_PLUGIN_FLAG_USER_WARNING)
			continue;
		tmp = fwupd_plugin_flag_to_string(i);
		if (tmp == NULL)
			break;
		str = fu_cli_plugin_flag_to_string(i);
		g_assert_nonnull(str);
	}
}

static void
fu_cli_interesting_device_func(void)
{
	struct {
		const gchar *name;
		FwupdDeviceFlags flags;
		const gchar *version;
		const gchar *update_error;
		FwupdDeviceFlags child_flags;
		gboolean expected;
	} map[] = {
	    {
		"updatable",
		FWUPD_DEVICE_FLAG_UPDATABLE,
		NULL,
		NULL,
		FWUPD_DEVICE_FLAG_NONE,
		TRUE,
	    },
	    {
		"update-error",
		FWUPD_DEVICE_FLAG_INTERNAL,
		NULL,
		"failed",
		FWUPD_DEVICE_FLAG_NONE,
		TRUE,
	    },
	    {
		"version",
		FWUPD_DEVICE_FLAG_INTERNAL,
		"1.2.3",
		NULL,
		FWUPD_DEVICE_FLAG_NONE,
		TRUE,
	    },
	    {
		"get-details",
		FWUPD_DEVICE_FLAG_NONE,
		NULL,
		NULL,
		FWUPD_DEVICE_FLAG_NONE,
		TRUE,
	    },
	    {
		"unrelated-flag",
		FWUPD_DEVICE_FLAG_INTERNAL,
		NULL,
		NULL,
		FWUPD_DEVICE_FLAG_NONE,
		FALSE,
	    },
	    {
		"interesting-child",
		FWUPD_DEVICE_FLAG_INTERNAL,
		NULL,
		NULL,
		FWUPD_DEVICE_FLAG_UPDATABLE,
		TRUE,
	    },
	};

	for (guint i = 0; i < G_N_ELEMENTS(map); i++) {
		g_autoptr(FwupdDevice) child = NULL;
		g_autoptr(FwupdDevice) device = fwupd_device_new();
		g_autoptr(GPtrArray) devices =
		    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);

		g_test_message("checking %s", map[i].name);
		if (map[i].flags != FWUPD_DEVICE_FLAG_NONE)
			fwupd_device_add_flag(device, map[i].flags);
		if (map[i].version != NULL)
			fwupd_device_set_version(device, map[i].version);
		if (map[i].update_error != NULL)
			fwupd_device_set_update_error(device, map[i].update_error);
		g_ptr_array_add(devices, g_object_ref(device));

		if (map[i].child_flags != FWUPD_DEVICE_FLAG_NONE) {
			child = fwupd_device_new();
			fwupd_device_add_flag(child, map[i].child_flags);
			fwupd_device_set_parent(child, device);
			g_ptr_array_add(devices, g_object_ref(child));
		}

		g_assert_cmpint(fu_cli_is_interesting_device(devices, device), ==, map[i].expected);
	}
}

static void
fu_cli_security_report_json_func(void)
{
	GVariantBuilder report;
	GVariantBuilder attrs;
	GVariantBuilder failed;
	g_autoptr(FwupdSecurityAttr) attr =
	    fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_UEFI_SECUREBOOT);
	g_autoptr(FwupdJsonArray) attrs_json = NULL;
	g_autoptr(FwupdJsonArray) failed_json = NULL;
	g_autoptr(FwupdJsonObject) attr_json = NULL;
	g_autoptr(FwupdJsonObject) json_obj = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) report_variant = NULL;
	GRefString *value_str;
	gint64 value;

	g_variant_builder_init(&attrs, G_VARIANT_TYPE("aa{sv}"));
	fwupd_security_attr_set_plugin(attr, "uefi_capsule");
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	g_variant_builder_add_value(
	    &attrs,
	    fwupd_codec_to_variant(FWUPD_CODEC(attr), FWUPD_CODEC_FLAG_NONE));
	g_variant_builder_init(&failed, G_VARIANT_TYPE("as"));
	g_variant_builder_add(&failed, "s", FWUPD_SECURITY_ATTR_ID_SPI_BLE);
	g_variant_builder_init(&report, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add(&report, "{sv}", "Schema", g_variant_new_uint32(1));
	g_variant_builder_add(&report, "{sv}", "Score", g_variant_new_uint32(500));
	g_variant_builder_add(&report, "{sv}", "Level", g_variant_new_uint32(2));
	g_variant_builder_add(&report, "{sv}", "Ceiling", g_variant_new_uint32(3));
	g_variant_builder_add(&report, "{sv}", "Attributes", g_variant_builder_end(&attrs));
	g_variant_builder_add(&report, "{sv}", "Failed", g_variant_builder_end(&failed));
	report_variant = g_variant_ref_sink(g_variant_builder_end(&report));
	json_obj = fu_cli_security_report_to_json(report_variant, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json_obj);
	g_assert_true(fwupd_json_object_get_integer(json_obj, "Schema", &value, &error));
	g_assert_no_error(error);
	g_assert_cmpint(value, ==, 1);
	g_assert_true(fwupd_json_object_get_integer(json_obj, "Score", &value, &error));
	g_assert_no_error(error);
	g_assert_cmpint(value, ==, 500);
	g_assert_true(fwupd_json_object_get_integer(json_obj, "HsiLevel", &value, &error));
	g_assert_no_error(error);
	g_assert_cmpint(value, ==, 2);
	g_assert_true(fwupd_json_object_get_integer(json_obj, "Ceiling", &value, &error));
	g_assert_no_error(error);
	g_assert_cmpint(value, ==, 3);
	attrs_json = fwupd_json_object_get_array(json_obj, "SecurityAttributes", &error);
	g_assert_no_error(error);
	g_assert_nonnull(attrs_json);
	g_assert_cmpuint(fwupd_json_array_get_size(attrs_json), ==, 1);
	attr_json = fwupd_json_array_get_object(attrs_json, 0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(attr_json);
	value_str = fwupd_json_object_get_string(attr_json, "AppstreamId", &error);
	g_assert_no_error(error);
	g_assert_cmpstr(value_str, ==, FWUPD_SECURITY_ATTR_ID_UEFI_SECUREBOOT);
	failed_json = fwupd_json_object_get_array(json_obj, "FailedAttributes", &error);
	g_assert_no_error(error);
	g_assert_nonnull(failed_json);
	g_assert_cmpuint(fwupd_json_array_get_size(failed_json), ==, 1);
	value_str = fwupd_json_array_get_string(failed_json, 0, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(value_str, ==, FWUPD_SECURITY_ATTR_ID_SPI_BLE);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/cli", fu_cli_func);
	g_test_add_func("/fwupd/cli/interesting-device", fu_cli_interesting_device_func);
	g_test_add_func("/fwupd/cli/security-report-json", fu_cli_security_report_json_func);
	return g_test_run();
}
