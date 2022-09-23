/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-vli-pd-device.h"
#include "fu-vli-pd-firmware.h"
#include "fu-vli-plugin.h"
#include "fu-vli-usbhub-device.h"
#include "fu-vli-usbhub-firmware.h"

struct _FuVliPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuVliPlugin, fu_vli_plugin, FU_TYPE_PLUGIN)

static void
fu_vli_plugin_init(FuVliPlugin *self)
{
}

static void
fu_vli_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "VliDeviceKind");
	fu_context_add_quirk_key(ctx, "VliSpiAutoDetect");
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_VLI_USBHUB_FIRMWARE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_VLI_PD_FIRMWARE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_VLI_USBHUB_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_VLI_PD_DEVICE);
}

static void
fu_vli_plugin_class_init(FuVliPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_vli_plugin_constructed;
}
