/*
 * Copyright (C) FIXMEFIXMEFIXMEFIXMEFIXME2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-fastboot-device.h"
#include "fu-fastboot-plugin.h"

struct _FuFastbootPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuFastbootPlugin, fu_fastboot_plugin, FU_TYPE_PLUGIN)

static void
fu_fastboot_plugin_init(FuFastbootPlugin *self)
{
}

static void
fu_fastboot_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "FastbootBlockSize");
	fu_context_add_quirk_key(ctx, "FastbootOperationDelay");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_FASTBOOT_DEVICE);
}

static void
fu_fastboot_plugin_device_added(FuPlugin *self, FuDevice *device)
{
	if (fu_device_has_vendor_id(device, "USB:0x2CB7")) {
		fu_device_set_plugin(device, "fibocom");
	}
}

static void
fu_fastboot_plugin_class_init(FuFastbootPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_fastboot_plugin_constructed;
	plugin_class->device_added = fu_fastboot_plugin_device_added;
}
