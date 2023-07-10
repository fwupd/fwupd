/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2022 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-acpi-ivrs-plugin.h"
#include "fu-acpi-ivrs.h"

struct _FuAcpiIvrsPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuAcpiIvrsPlugin, fu_acpi_ivrs_plugin, FU_TYPE_PLUGIN)

static void
fu_acpi_ivrs_plugin_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	g_autofree gchar *fn = NULL;
	g_autofree gchar *path = NULL;
	g_autoptr(FuAcpiIvrs) ivrs = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error_local = NULL;

	/* only AMD */
	if (fu_cpu_get_vendor() != FU_CPU_VENDOR_AMD)
		return;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_PREBOOT_DMA_PROTECTION);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	fu_security_attrs_append(attrs, attr);

	/* load IVRS table */
	path = fu_path_from_kind(FU_PATH_KIND_ACPI_TABLES);
	fn = g_build_filename(path, "IVRS", NULL);
	blob = fu_bytes_get_contents(fn, &error_local);
	if (blob == NULL) {
		g_debug("failed to load %s: %s", fn, error_local->message);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}
	ivrs = fu_acpi_ivrs_new(blob, &error_local);
	if (ivrs == NULL) {
		g_warning("failed to parse %s: %s", fn, error_local->message);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}
	if (!fu_acpi_ivrs_get_dma_remap(ivrs)) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_acpi_ivrs_plugin_init(FuAcpiIvrsPlugin *self)
{
}

static void
fu_acpi_ivrs_plugin_class_init(FuAcpiIvrsPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->add_security_attrs = fu_acpi_ivrs_plugin_add_security_attrs;
}
