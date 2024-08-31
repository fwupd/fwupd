/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-iommu-plugin.h"

struct _FuIommuPlugin {
	FuPlugin parent_instance;
	gboolean has_iommu;
};

G_DEFINE_TYPE(FuIommuPlugin, fu_iommu_plugin, FU_TYPE_PLUGIN)

static void
fu_iommu_plugin_to_string(FuPlugin *plugin, guint idt, GString *str)
{
	FuIommuPlugin *self = FU_IOMMU_PLUGIN(plugin);
	fwupd_codec_string_append_bool(str, idt, "HasIommu", self->has_iommu);
}

static gboolean
fu_iommu_plugin_backend_device_added(FuPlugin *plugin,
				     FuDevice *device,
				     FuProgress *progress,
				     GError **error)
{
	FuIommuPlugin *self = FU_IOMMU_PLUGIN(plugin);

	/* interesting device? */
	if (!FU_IS_UDEV_DEVICE(device))
		return TRUE;
	if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), "iommu") != 0)
		return TRUE;
	self->has_iommu = TRUE;

	return TRUE;
}

static void
fu_iommu_plugin_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuIommuPlugin *self = FU_IOMMU_PLUGIN(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GHashTable) cmdline = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_IOMMU);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	fu_security_attrs_append(attrs, attr);

	/* we might be able to fix this */
	cmdline = fu_kernel_get_cmdline(&error_local);
	if (cmdline == NULL) {
		g_warning("failed to get kernel cmdline: %s", error_local->message);
	} else if (fu_kernel_check_cmdline_mutable(NULL)) {
		const gchar *value = g_hash_table_lookup(cmdline, "iommu");
		fwupd_security_attr_set_kernel_current_value(attr, value);
		if (!g_hash_table_contains(cmdline, "iommu") &&
		    !g_hash_table_contains(cmdline, "intel_iommu") &&
		    !g_hash_table_contains(cmdline, "amd_iommu")) {
			fwupd_security_attr_set_kernel_target_value(attr, "iommu=force");
			fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_CAN_FIX);
		}
		if (g_strcmp0(value, "force") == 0) {
			fwupd_security_attr_set_kernel_target_value(attr, NULL);
			fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_CAN_UNDO);
		}
	}

	fu_security_attr_add_bios_target_value(attr, "AmdVt", "enable");
	fu_security_attr_add_bios_target_value(attr, "IOMMU", "enable");
	fu_security_attr_add_bios_target_value(attr, "VtForDirectIo", "enable");
	/**
	 * Lenovo systems that offer a BIOS setting for ThunderboltAccess will
	 * use this option to control whether the IOMMU is enabled by default
	 * or not.
	 *
	 * It may be counter-intuitive; but as there are other more physically
	 * difficult to attack PCIe devices it's better to have the IOMMU
	 * enabled pre-boot even if it enables access to Thunderbolt/USB4.
	 */
	fu_security_attr_add_bios_target_value(attr, "com.thinklmi.ThunderboltAccess", "enable");

	if (!self->has_iommu) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_OS);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_iommu_plugin_init(FuIommuPlugin *self)
{
}

static void
fu_iommu_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_udev_subsystem(plugin, "iommu");
}

static gboolean
fu_iommu_plugin_fix_host_security_attr(FuPlugin *self, FwupdSecurityAttr *attr, GError **error)
{
	return fu_kernel_add_cmdline_arg("iommu=force", error);
}

static gboolean
fu_iommu_plugin_undo_host_security_attr(FuPlugin *self, FwupdSecurityAttr *attr, GError **error)
{
	return fu_kernel_remove_cmdline_arg("iommu=force", error);
}

static void
fu_iommu_plugin_class_init(FuIommuPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_iommu_plugin_constructed;
	plugin_class->to_string = fu_iommu_plugin_to_string;
	plugin_class->backend_device_added = fu_iommu_plugin_backend_device_added;
	plugin_class->add_security_attrs = fu_iommu_plugin_add_security_attrs;
	plugin_class->fix_host_security_attr = fu_iommu_plugin_fix_host_security_attr;
	plugin_class->undo_host_security_attr = fu_iommu_plugin_undo_host_security_attr;
}
