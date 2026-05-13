/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-acpi-facp-plugin.h"
#include "fu-acpi-facp.h"

struct _FuAcpiFacpPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuAcpiFacpPlugin, fu_acpi_facp_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_acpi_facp_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	FuAcpiFadtPmProfile pm_profile;
	g_autofree gchar *fn = NULL;
	g_autoptr(FuAcpiFacp) facp = NULL;
	g_autoptr(GBytes) blob = NULL;

	(void)progress;
	(void)error;

	fn = fu_context_build_filename(ctx, error, FU_PATH_KIND_ACPI_TABLES, "FACP", NULL);
	if (fn == NULL)
		return FALSE;
	blob = fu_bytes_get_contents(fn, NULL);
	if (blob == NULL)
		return TRUE;
	facp = fu_acpi_facp_new(blob, error);
	if (facp == NULL)
		return FALSE;
	pm_profile = fu_acpi_facp_get_pm_profile(facp);

	if (pm_profile == FU_ACPI_FADT_PM_PROFILE_ENTERPRISE_SERVER ||
	    pm_profile == FU_ACPI_FADT_PM_PROFILE_SOHO_SERVER ||
	    pm_profile == FU_ACPI_FADT_PM_PROFILE_PERFORMANCE_SERVER)
		fu_context_add_flag(ctx, FU_CONTEXT_FLAG_IS_SERVER);

	return TRUE;
}

static void
fu_acpi_facp_plugin_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	g_autofree gchar *fn = NULL;
	g_autoptr(FuAcpiFacp) facp = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error_local = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_IDLE);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	fu_security_attrs_append(attrs, attr);

	/* load FACP table */
	fn = fu_context_build_filename(ctx, &error_local, FU_PATH_KIND_ACPI_TABLES, "FACP", NULL);
	if (fn == NULL) {
		g_debug("failed to build: %s", error_local->message);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}
	blob = fu_bytes_get_contents(fn, &error_local);
	if (blob == NULL) {
		g_debug("failed to load %s: %s", fn, error_local->message);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}
	facp = fu_acpi_facp_new(blob, &error_local);
	if (facp == NULL) {
		g_warning("failed to parse %s: %s", fn, error_local->message);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}

	/* options are usually "Linux" (S3) or "Windows" (s2idle) */
	fu_security_attr_add_bios_target_value(attr, "com.thinklmi.SleepState", "windows");

	if (!fu_acpi_facp_get_s2i(facp)) {
		/* on server platforms, S0ix (Suspend-to-Idle) is not supported and
		 * should not be considered a security issue */
		if (fu_context_has_flag(ctx, FU_CONTEXT_FLAG_IS_SERVER)) {
			fwupd_security_attr_set_result(attr,
						       FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
			fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
			return;
		}
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_OS);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_acpi_facp_plugin_init(FuAcpiFacpPlugin *self)
{
}

static void
fu_acpi_facp_plugin_class_init(FuAcpiFacpPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->startup = fu_acpi_facp_plugin_startup;
	plugin_class->add_security_attrs = fu_acpi_facp_plugin_add_security_attrs;
}
