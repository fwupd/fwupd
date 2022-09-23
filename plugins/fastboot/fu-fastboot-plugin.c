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
fu_fastboot_plugin_class_init(FuFastbootPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_fastboot_plugin_constructed;
}
