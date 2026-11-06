/*
 * Copyright 2026 Arno Dubois <a.dubois@criteo.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-dell-psb-device.h"
#include "fu-dell-psb-plugin.h"

struct _FuDellPsbPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuDellPsbPlugin, fu_dell_psb_plugin, FU_TYPE_PLUGIN)

static void
fu_dell_psb_plugin_init(FuDellPsbPlugin *self)
{
}

static void
fu_dell_psb_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);

	/* the device checks this key against the PCI parent's instance ID */
	fu_context_add_quirk_key(ctx, "DellPsbDevice");
	fu_plugin_add_device_udev_subsystem(plugin, "scsi_generic");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_DELL_PSB_DEVICE);

	/* chain up to parent */
	G_OBJECT_CLASS(fu_dell_psb_plugin_parent_class)->constructed(obj);
}

static void
fu_dell_psb_plugin_class_init(FuDellPsbPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_dell_psb_plugin_constructed;
}
