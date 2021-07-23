/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-acpi-facp.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
}

void
fu_plugin_add_security_attrs (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	g_autofree gchar *fn = NULL;
	g_autofree gchar *path = NULL;
	g_autoptr(FuAcpiFacp) facp = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error_local = NULL;

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_IDLE);
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_THEORETICAL);
	fu_security_attrs_append (attrs, attr);

	/* load FACP table */
	path = fu_common_get_path (FU_PATH_KIND_ACPI_TABLES);
	fn = g_build_filename (path, "FACP", NULL);
	blob = fu_common_get_contents_bytes (fn, &error_local);
	if (blob == NULL) {
		g_warning ("failed to load %s: %s", fn, error_local->message);
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}
	facp = fu_acpi_facp_new (blob, &error_local);
	if (facp == NULL) {
		g_warning ("failed to parse %s: %s", fn, error_local->message);
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}
	if (!fu_acpi_facp_get_s2i (facp)) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}

	/* success */
	fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}
