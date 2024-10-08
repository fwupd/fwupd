/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-intel-cvs-device.h"
#include "fu-intel-cvs-firmware.h"
#include "fu-intel-cvs-plugin.h"

struct _FuIntelCvsPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuIntelCvsPlugin, fu_intel_cvs_plugin, FU_TYPE_PLUGIN)

static void
fu_intel_cvs_plugin_init(FuIntelCvsPlugin *self)
{
}

static void
fu_intel_cvs_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "IntelCvsMaxDownloadTime");
	fu_context_add_quirk_key(ctx, "IntelCvsMaxFlashTime");
	fu_context_add_quirk_key(ctx, "IntelCvsMaxRetryCount");
	fu_plugin_add_udev_subsystem(plugin, "i2c");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_INTEL_CVS_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_INTEL_CVS_FIRMWARE);
}

static void
fu_intel_cvs_plugin_class_init(FuIntelCvsPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_intel_cvs_plugin_constructed;
}
