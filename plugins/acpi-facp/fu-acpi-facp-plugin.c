/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-acpi-facp-plugin.h"
#include "fu-acpi-facp.h"

struct _FuAcpiFacpPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuAcpiFacpPlugin, fu_acpi_facp_plugin, FU_TYPE_PLUGIN)

static void
fu_acpi_facp_plugin_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	g_autofree gchar *fn = NULL;
	g_autofree gchar *path = NULL;
	g_autoptr(FuAcpiFacp) facp = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error_local = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_IDLE);
	fu_security_attrs_append(attrs, attr);

	/* load FACP table */
	path = fu_path_from_kind(FU_PATH_KIND_ACPI_TABLES);
	fn = g_build_filename(path, "FACP", NULL);
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
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_OS);
		return;
	}

	/* success */
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
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
	plugin_class->add_security_attrs = fu_acpi_facp_plugin_add_security_attrs;
}
