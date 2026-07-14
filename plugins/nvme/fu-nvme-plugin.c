/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-nvme-device.h"
#include "fu-nvme-plugin.h"

struct _FuNvmePlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuNvmePlugin, fu_nvme_plugin, FU_TYPE_PLUGIN)

static void
fu_nvme_plugin_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	GPtrArray *devices = fu_plugin_get_devices(plugin);
	gboolean valid = FALSE;
	gboolean supported = TRUE;
	gboolean enabled = TRUE;
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	for (guint i = 0; i < devices->len; i++) {
		FuNvmeDevice *device = FU_NVME_DEVICE(g_ptr_array_index(devices, i));
		FuNvmeOpalFlags opal_flags = fu_nvme_device_get_opal_flags(device);

		if (!fu_device_has_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_INTERNAL))
			continue;
		valid = TRUE;
		if ((opal_flags & FU_NVME_OPAL_FLAG_SUPPORTED) == 0 ||
		    (opal_flags & FU_NVME_OPAL_FLAG_LOCKING_SUPPORTED) == 0) {
			supported = FALSE;
		}
		if ((opal_flags & FU_NVME_OPAL_FLAG_LOCKING_ENABLED) == 0)
			enabled = FALSE;
	}
	if (!valid)
		return;

	attr = fu_security_attr_new(ctx, FWUPD_SECURITY_ATTR_ID_HW_DISK_ENCRYPTION);
	fwupd_security_attr_set_plugin(attr, fu_plugin_get_name(plugin));
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	fu_security_attrs_append(attrs, attr);

	if (!supported) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
		return;
	}
	if (!enabled) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}
	fwupd_security_attr_add_obsolete(attr, FWUPD_SECURITY_ATTR_ID_KERNEL_SWAP);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_nvme_plugin_init(FuNvmePlugin *self)
{
}

static void
fu_nvme_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "NvmeBlockSize");
	fu_context_add_quirk_key(ctx, "NvmeSerialSuffixChars");
	fu_plugin_add_device_udev_subsystem(plugin, "nvme");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_NVME_DEVICE);

	/* chain up to parent */
	G_OBJECT_CLASS(fu_nvme_plugin_parent_class)->constructed(obj);
}

static void
fu_nvme_plugin_class_init(FuNvmePluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_nvme_plugin_constructed;
	plugin_class->add_security_attrs = fu_nvme_plugin_add_security_attrs;
}
