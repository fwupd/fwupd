/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-tcg-opal-device.h"
#include "fu-tcg-opal-plugin.h"

struct _FuTcgOpalPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuTcgOpalPlugin, fu_tcg_opal_plugin, FU_TYPE_PLUGIN)

static void
fu_tcg_opal_plugin_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	GPtrArray *devices = fu_plugin_get_devices(plugin);
	gboolean valid = FALSE;
	gboolean supported = TRUE;
	gboolean enabled = TRUE;
	g_autoptr(FuSecurityAttr) attr = NULL;

	for (guint i = 0; i < devices->len; i++) {
		FuTcgOpalDevice *device = FU_TCG_OPAL_DEVICE(g_ptr_array_index(devices, i));
		FuTcgOpalFlags opal_flags = fu_tcg_opal_device_get_opal_flags(device);

		valid = TRUE;
		if ((opal_flags & FU_TCG_OPAL_FLAG_SUPPORTED) == 0 ||
		    (opal_flags & FU_TCG_OPAL_FLAG_LOCKING_SUPPORTED) == 0) {
			supported = FALSE;
		}
		if ((opal_flags & FU_TCG_OPAL_FLAG_LOCKING_ENABLED) == 0)
			enabled = FALSE;
	}
	if (!valid)
		return;

	attr = fu_security_attr_new(ctx, FWUPD_SECURITY_ATTR_ID_HW_DISK_ENCRYPTION);
	fu_security_attr_set_plugin(attr, fu_plugin_get_name(plugin));
	fu_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW);
	fu_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	fu_security_attrs_append(attrs, attr);

	if (!supported) {
		fu_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
		return;
	}
	if (!enabled) {
		fu_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}
	fu_security_attr_add_obsolete(attr, FWUPD_SECURITY_ATTR_ID_KERNEL_SWAP);
	fu_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_tcg_opal_plugin_init(FuTcgOpalPlugin *self)
{
}

static void
fu_tcg_opal_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);

	fu_plugin_add_device_udev_subsystem(plugin, "block:disk");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_TCG_OPAL_DEVICE);

	/* chain up to parent */
	G_OBJECT_CLASS(fu_tcg_opal_plugin_parent_class)->constructed(obj);
}

static void
fu_tcg_opal_plugin_class_init(FuTcgOpalPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_tcg_opal_plugin_constructed;
	plugin_class->add_security_attrs = fu_tcg_opal_plugin_add_security_attrs;
}
