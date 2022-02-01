/*
 * Copyright (C) 2022 Advanced Micro Devices Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

static void
fu_plugin_pci_psp_init(FuPlugin *plugin)
{
	fu_plugin_add_udev_subsystem(plugin, "pci");
}

static gboolean
fu_plugin_pci_psp_backend_device_added(FuPlugin *plugin, FuDevice *device, GError **error)
{
	/* interesting device? */
	if (!FU_IS_UDEV_DEVICE(device))
		return TRUE;
	if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), "pci") != 0)
		return TRUE;

	fu_plugin_cache_add(plugin, "pci-psp-device", device);

	return TRUE;
}

static gboolean
fu_plugin_pci_psp_get_attr(FwupdSecurityAttr *attr,
			   const gchar *path,
			   const gchar *file,
			   gboolean *out,
			   GError **error)
{
	g_autofree gchar *fn = g_build_filename(path, file, NULL);
	g_autofree gchar *buf = NULL;
	gsize bufsz = 0;

	if (!g_file_get_contents(fn, &buf, &bufsz, error)) {
		g_prefix_error(error, "could not open %s: ", fn);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA);
		return FALSE;
	}
	*out = fu_common_strtoull(buf) ? TRUE : FALSE;

	return TRUE;
}

static void
fu_plugin_add_security_attrs_tsme(const gchar *path, FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;
	gboolean val;

	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM);
	fwupd_security_attr_set_plugin(attr, "pci_psp");
	fwupd_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_SYSTEM_PROTECTION);
	fu_security_attrs_append(attrs, attr);

	if (!fu_plugin_pci_psp_get_attr(attr, path, "tsme_status", &val, &error_local)) {
		g_debug("%s", error_local->message);
		return;
	}

	if (!val) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENCRYPTED);
		return;
	}

	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_ENCRYPTED);
	fwupd_security_attr_add_obsolete(attr, "msr");
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_plugin_add_security_attrs_fused_part(const gchar *path, FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;
	gboolean val;

	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_PLATFORM_FUSED);
	fwupd_security_attr_set_plugin(attr, "pci_psp");
	fwupd_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fu_security_attrs_append(attrs, attr);

	if (!fu_plugin_pci_psp_get_attr(attr, path, "fused_part", &val, &error_local)) {
		g_debug("%s", error_local->message);
		return;
	}

	if (!val) {
		g_debug("part is not fused");
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
		return;
	}

	/* success */
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_LOCKED);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_plugin_add_security_attrs_debug_locked_part(const gchar *path, FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;
	gboolean val;

	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_LOCKED);
	fwupd_security_attr_set_plugin(attr, "pci_psp");
	fwupd_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT);
	fu_security_attrs_append(attrs, attr);

	if (!fu_plugin_pci_psp_get_attr(attr, path, "debug_lock_on", &val, &error_local)) {
		g_debug("%s", error_local->message);
		return;
	}

	if (!val) {
		g_debug("debug lock disabled");
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
		return;
	}

	/* success */
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_LOCKED);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_plugin_add_security_attrs_rollback_protection(const gchar *path, FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;
	gboolean val;

	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_AMD_ROLLBACK_PROTECTION);
	fwupd_security_attr_set_plugin(attr, "pci_psp");
	fwupd_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fu_security_attrs_append(attrs, attr);

	if (!fu_plugin_pci_psp_get_attr(attr, path, "anti_rollback_status", &val, &error_local)) {
		g_debug("%s", error_local->message);
		return;
	}

	if (!val) {
		g_debug("rollback protection not enforced");
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}

	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_plugin_add_security_attrs_rom_armor(const gchar *path, FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;
	gboolean val;

	/* create attr */
	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_AMD_SPI_WRITE_PROTECTION);
	fwupd_security_attr_set_plugin(attr, "pci_psp");
	fwupd_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT);
	fu_security_attrs_append(attrs, attr);

	if (!fu_plugin_pci_psp_get_attr(attr, path, "rom_armor_enforced", &val, &error_local)) {
		g_debug("%s", error_local->message);
		return;
	}

	if (!val) {
		g_debug("ROM armor not enforced");
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}

	/* success */
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_plugin_add_security_attrs_rpmc(const gchar *path, FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;
	gboolean val;

	/* create attr */
	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_AMD_SPI_REPLAY_PROTECTION);
	fwupd_security_attr_set_plugin(attr, "pci_psp");
	fwupd_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_THEORETICAL);
	fu_security_attrs_append(attrs, attr);

	if (!fu_plugin_pci_psp_get_attr(attr, path, "rpmc_spirom_available", &val, &error_local)) {
		g_debug("%s", error_local->message);
		return;
	}

	if (!val) {
		g_debug("no RPMC compatible SPI rom present");
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
		return;
	}

	if (!fu_plugin_pci_psp_get_attr(attr,
					path,
					"rpmc_production_enabled",
					&val,
					&error_local)) {
		g_debug("%s", error_local->message);
		return;
	}

	if (!val) {
		g_debug("no RPMC compatible SPI rom present");
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}

	/* success */
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_plugin_pci_psp_set_missing_data(FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_SUPPORTED_CPU);
	fwupd_security_attr_set_plugin(attr, "pci_psp");
	fwupd_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fwupd_security_attr_add_obsolete(attr, "cpu");
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA);
	fu_security_attrs_append(attrs, attr);
}

static void
fu_plugin_pci_psp_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuDevice *psp_device = fu_plugin_cache_lookup(plugin, "pci-psp-device");
	const gchar *sysfs_path = NULL;

	/* only AMD */
	if (fu_common_get_cpu_vendor() != FU_CPU_VENDOR_AMD)
		return;

	/* ccp not loaded */
	if (psp_device)
		sysfs_path = fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(psp_device));
	if (sysfs_path == NULL) {
		fu_plugin_pci_psp_set_missing_data(attrs);
		return;
	}

	fu_plugin_add_security_attrs_tsme(sysfs_path, attrs);
	fu_plugin_add_security_attrs_fused_part(sysfs_path, attrs);
	fu_plugin_add_security_attrs_debug_locked_part(sysfs_path, attrs);
	fu_plugin_add_security_attrs_rollback_protection(sysfs_path, attrs);
	fu_plugin_add_security_attrs_rpmc(sysfs_path, attrs);
	fu_plugin_add_security_attrs_rom_armor(sysfs_path, attrs);
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_pci_psp_init;
	vfuncs->add_security_attrs = fu_plugin_pci_psp_add_security_attrs;
	vfuncs->backend_device_added = fu_plugin_pci_psp_backend_device_added;
}
