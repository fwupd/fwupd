/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-lenovo-ldc-device.h"
#include "fu-lenovo-ldc-firmware.h"
#include "fu-lenovo-ldc-plugin.h"

struct _FuLenovoLdcPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuLenovoLdcPlugin, fu_lenovo_ldc_plugin, FU_TYPE_PLUGIN)

static void
fu_lenovo_ldc_plugin_init(FuLenovoLdcPlugin *self)
{
	fu_plugin_add_flag(FU_PLUGIN(self), FWUPD_PLUGIN_FLAG_MUTABLE_ENUMERATION);
}

static void
fu_lenovo_ldc_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "LenovoLdcStartAddr");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_LENOVO_LDC_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, FU_TYPE_LENOVO_LDC_FIRMWARE);
}

static void
fu_lenovo_ldc_plugin_class_init(FuLenovoLdcPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_lenovo_ldc_plugin_constructed;
}
