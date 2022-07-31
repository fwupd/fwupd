/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-acpi-dmar.h"

static void
fu_plugin_acpi_dmar_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	g_autofree gchar *fn = NULL;
	g_autofree gchar *path = NULL;
	g_autoptr(FuAcpiDmar) dmar = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error_local = NULL;

	/* only Intel */
	if (fu_cpu_get_vendor() != FU_CPU_VENDOR_INTEL)
		return;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_PREBOOT_DMA_PROTECTION);
	fu_security_attrs_append(attrs, attr);

	/* load DMAR table */
	path = fu_path_from_kind(FU_PATH_KIND_ACPI_TABLES);
	fn = g_build_filename(path, "DMAR", NULL);
	blob = fu_bytes_get_contents(fn, &error_local);
	if (blob == NULL) {
		g_debug("failed to load %s: %s", fn, error_local->message);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}
	dmar = fu_acpi_dmar_new(blob, &error_local);
	if (dmar == NULL) {
		g_warning("failed to parse %s: %s", fn, error_local->message);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}
	if (!fu_acpi_dmar_get_opt_in(dmar)) {
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}

	/* success */
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->add_security_attrs = fu_plugin_acpi_dmar_add_security_attrs;
}
