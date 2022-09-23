/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
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
	fu_string_append_kb(str, idt, "HasIommu", self->has_iommu);
}

static gboolean
fu_iommu_plugin_backend_device_added(FuPlugin *plugin, FuDevice *device, GError **error)
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

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_IOMMU);
	fu_security_attrs_append(attrs, attr);

	fu_security_attr_add_bios_target_value(attr, "AmdVt", "enable");
	fu_security_attr_add_bios_target_value(attr, "IOMMU", "enable");
	fu_security_attr_add_bios_target_value(attr, "VtForDirectIo", "enable");

	if (!self->has_iommu) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
}

static void
fu_iommu_plugin_init(FuIommuPlugin *self)
{
}

static void
fu_iommu_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "iommu");
}

static void
fu_iommu_plugin_class_init(FuIommuPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->constructed = fu_iommu_plugin_constructed;
	plugin_class->to_string = fu_iommu_plugin_to_string;
	plugin_class->backend_device_added = fu_iommu_plugin_backend_device_added;
	plugin_class->add_security_attrs = fu_iommu_plugin_add_security_attrs;
}
