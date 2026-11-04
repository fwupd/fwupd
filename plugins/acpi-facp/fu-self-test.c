/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-acpi-facp-plugin.h"
#include "fu-acpi-facp.h"
#include "fu-context-private.h"
#include "fu-plugin-private.h"
#include "fu-security-attrs-private.h"

static void
fu_acpi_facp_synthetic_func(void)
{
	struct {
		gboolean s2i;
		FuAcpiFadtPmProfile pm_profile;
	} map[] = {
	    {FALSE, FU_ACPI_FADT_PM_PROFILE_MOBILE},
	    {TRUE, FU_ACPI_FADT_PM_PROFILE_MOBILE},
	    {FALSE, FU_ACPI_FADT_PM_PROFILE_ENTERPRISE_SERVER},
	};

	for (guint i = 0; i < G_N_ELEMENTS(map); i++) {
		gboolean ret;
		gsize bufsz = 0;
		guint32 flags = 0;
		guint8 pm_profile = 0;
		const guint8 *buf;
		g_autoptr(FuAcpiFacp) facp = fu_acpi_facp_new();
		g_autoptr(FuAcpiFacp) facp2 = fu_acpi_facp_new();
		g_autoptr(FwupdMemoryInputStream) stream = NULL;
		g_autoptr(GBytes) blob = NULL;
		g_autoptr(GError) error = NULL;

		fu_acpi_facp_set_s2i(facp, map[i].s2i);
		fu_acpi_facp_set_pm_profile(facp, map[i].pm_profile);
		blob = fu_firmware_write(FU_FIRMWARE(facp), &error);
		g_assert_no_error(error);
		g_assert_nonnull(blob);

		/* the offsets defined by the ACPI specification */
		buf = g_bytes_get_data(blob, &bufsz);
		ret = fu_memread_uint8_safe(buf, bufsz, 0x2D, &pm_profile, &error);
		g_assert_no_error(error);
		g_assert_true(ret);
		g_assert_cmpuint(pm_profile, ==, map[i].pm_profile);
		ret = fu_memread_uint32_safe(buf, bufsz, 0x70, &flags, G_LITTLE_ENDIAN, &error);
		g_assert_no_error(error);
		g_assert_true(ret);
		g_assert_cmpuint(flags, ==, map[i].s2i ? 1u << 21 : 0u);

		/* parse it back */
		stream = fwupd_memory_input_stream_new_from_bytes(blob);
		ret = fu_firmware_parse_stream(FU_FIRMWARE(facp2),
					       FWUPD_INPUT_STREAM(stream),
					       0x0,
					       FU_FIRMWARE_PARSE_FLAG_NONE,
					       &error);
		g_assert_no_error(error);
		g_assert_true(ret);
		g_assert_cmpint(fu_acpi_facp_get_s2i(facp2), ==, map[i].s2i);
		g_assert_cmpuint(fu_acpi_facp_get_pm_profile(facp2), ==, map[i].pm_profile);
	}
}

static void
fu_acpi_facp_truncated_func(void)
{
	gboolean ret;
	guint8 buf[0x10] = {0x0};
	g_autoptr(FuAcpiFacp) facp = fu_acpi_facp_new();
	g_autoptr(FuFirmware) table = fu_acpi_table_new();
	g_autoptr(FwupdMemoryInputStream) stream = NULL;
	g_autoptr(FwupdMemoryInputStream) stream_payload = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) blob_payload = g_bytes_new(buf, sizeof(buf));
	g_autoptr(GError) error = NULL;

	/* a valid ACPI header, but the payload is too small to hold the FACP fields */
	fu_firmware_set_id(table, "FACP");
	stream_payload = fwupd_memory_input_stream_new_from_bytes(blob_payload);
	fu_acpi_table_set_payload(FU_ACPI_TABLE(table), FWUPD_INPUT_STREAM(stream_payload));
	blob = fu_firmware_write(table, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);

	stream = fwupd_memory_input_stream_new_from_bytes(blob);
	ret = fu_firmware_parse_stream(FU_FIRMWARE(facp),
				       FWUPD_INPUT_STREAM(stream),
				       0x0,
				       FU_FIRMWARE_PARSE_FLAG_NONE,
				       &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);
}

static void
fu_acpi_facp_wrong_signature_func(void)
{
	gboolean ret;
	g_autoptr(FuAcpiFacp) facp = fu_acpi_facp_new();
	g_autoptr(FuFirmware) table = fu_acpi_table_new();
	g_autoptr(FwupdMemoryInputStream) stream = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	fu_firmware_set_id(table, "DMAR");
	blob = fu_firmware_write(table, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);

	stream = fwupd_memory_input_stream_new_from_bytes(blob);
	ret = fu_firmware_parse_stream(FU_FIRMWARE(facp),
				       FWUPD_INPUT_STREAM(stream),
				       0x0,
				       FU_FIRMWARE_PARSE_FLAG_NONE,
				       &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
}

static void
fu_acpi_facp_builder_func(void)
{
	gboolean ret;
	const gchar *xml = "<firmware gtype=\"FuAcpiFacp\">\n"
			   "  <s2i>true</s2i>\n"
			   "</firmware>\n";
	g_autofree gchar *xml_out = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GError) error = NULL;

	g_type_ensure(FU_TYPE_ACPI_FACP);

	/* build */
	firmware = fu_firmware_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware);
	g_assert_true(fu_acpi_facp_get_s2i(FU_ACPI_FACP(firmware)));

	/* export */
	xml_out = fu_firmware_export_to_xml(firmware, FU_FIRMWARE_EXPORT_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(xml_out);
	g_assert_nonnull(g_strstr_len(xml_out, -1, "<s2i>true</s2i>"));

	/* write, parse and build again */
	ret = fu_firmware_roundtrip_from_xml(xml, NULL, FU_FIRMWARE_BUILDER_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

/* @facp is written to the ACPI tables directory unless %NULL, and then the plugin is run */
static FuSecurityAttr *
fu_acpi_facp_plugin_get_attr(FuContext *ctx, FuAcpiFacp *facp)
{
	gboolean ret;
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;

	/* set up test harness */
	tmpdir = fu_temporary_directory_new("acpi-facp", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_ACPI_TABLES, tmpdir);

	/* write a FACP table */
	if (facp != NULL) {
		g_autofree gchar *fn = fu_temporary_directory_build(tmpdir, "FACP", NULL);
		g_autoptr(GBytes) blob = fu_firmware_write(FU_FIRMWARE(facp), &error);
		g_assert_no_error(error);
		g_assert_nonnull(blob);
		ret = fu_bytes_set_contents(fn, blob, &error);
		g_assert_no_error(error);
		g_assert_true(ret);
	}

	plugin = fu_plugin_new_from_gtype(fu_acpi_facp_plugin_get_type(), ctx);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_plugin_runner_add_security_attrs(plugin, attrs);
	return fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_IDLE,
						     NULL);
}

static void
fu_acpi_facp_plugin_s2i_enabled_func(void)
{
	g_autoptr(FuAcpiFacp) facp = fu_acpi_facp_new();
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuSecurityAttr) attr = NULL;

	fu_acpi_facp_set_s2i(facp, TRUE);
	fu_acpi_facp_set_pm_profile(facp, FU_ACPI_FADT_PM_PROFILE_MOBILE);
	attr = fu_acpi_facp_plugin_get_attr(ctx, facp);
	g_assert_nonnull(attr);
	g_assert_false(fu_context_has_flag(ctx, FU_CONTEXT_FLAG_IS_SERVER));
	g_assert_cmpint(fu_security_attr_get_result(attr), ==, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	g_assert_true(fu_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_acpi_facp_plugin_s2i_disabled_func(void)
{
	g_autoptr(FuAcpiFacp) facp = fu_acpi_facp_new();
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuSecurityAttr) attr = NULL;

	fu_acpi_facp_set_s2i(facp, FALSE);
	fu_acpi_facp_set_pm_profile(facp, FU_ACPI_FADT_PM_PROFILE_MOBILE);
	attr = fu_acpi_facp_plugin_get_attr(ctx, facp);
	g_assert_nonnull(attr);
	g_assert_false(fu_context_has_flag(ctx, FU_CONTEXT_FLAG_IS_SERVER));
	g_assert_cmpint(fu_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
	g_assert_false(fu_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
	g_assert_true(fu_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW));
	g_assert_true(fu_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_OS));
}

static void
fu_acpi_facp_plugin_server_func(void)
{
	g_autoptr(FuAcpiFacp) facp = fu_acpi_facp_new();
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuSecurityAttr) attr = NULL;

	fu_acpi_facp_set_s2i(facp, FALSE);
	fu_acpi_facp_set_pm_profile(facp, FU_ACPI_FADT_PM_PROFILE_ENTERPRISE_SERVER);
	attr = fu_acpi_facp_plugin_get_attr(ctx, facp);
	g_assert_nonnull(attr);
	g_assert_true(fu_context_has_flag(ctx, FU_CONTEXT_FLAG_IS_SERVER));
	g_assert_cmpint(fu_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
	g_assert_true(fu_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_acpi_facp_plugin_missing_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuSecurityAttr) attr = NULL;

	attr = fu_acpi_facp_plugin_get_attr(ctx, NULL);
	g_assert_nonnull(attr);
	g_assert_false(fu_context_has_flag(ctx, FU_CONTEXT_FLAG_IS_SERVER));
	g_assert_cmpint(fu_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
	g_assert_false(fu_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/acpi-facp/synthetic", fu_acpi_facp_synthetic_func);
	g_test_add_func("/acpi-facp/truncated", fu_acpi_facp_truncated_func);
	g_test_add_func("/acpi-facp/wrong-signature", fu_acpi_facp_wrong_signature_func);
	g_test_add_func("/acpi-facp/builder", fu_acpi_facp_builder_func);
	g_test_add_func("/acpi-facp/plugin/s2i-enabled", fu_acpi_facp_plugin_s2i_enabled_func);
	g_test_add_func("/acpi-facp/plugin/s2i-disabled", fu_acpi_facp_plugin_s2i_disabled_func);
	g_test_add_func("/acpi-facp/plugin/server", fu_acpi_facp_plugin_server_func);
	g_test_add_func("/acpi-facp/plugin/missing", fu_acpi_facp_plugin_missing_func);
	return g_test_run();
}
