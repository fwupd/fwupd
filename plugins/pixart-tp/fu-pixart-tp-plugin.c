/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-pixart-tp-device.h"
#include "fu-pixart-tp-firmware.h"
#include "fu-pixart-tp-haptic-device.h"
#include "fu-pixart-tp-plugin.h"

struct _FuPixartTpPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuPixartTpPlugin, fu_pixart_tp_plugin, FU_TYPE_PLUGIN)

static void
fu_pixart_tp_plugin_init(FuPixartTpPlugin *self)
{
}

static void
fu_pixart_tp_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);

	fu_context_add_quirk_key(ctx, "PixartTpHidVersionBank");
	fu_context_add_quirk_key(ctx, "PixartTpHidVersionAddr");
	fu_context_add_quirk_key(ctx, "PixartTpSramSelect");
	fu_context_add_quirk_key(ctx, "PixartTpHasHaptic");

	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_set_device_gtype_default(plugin, FU_TYPE_PIXART_TP_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_PIXART_TP_HAPTIC_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, FU_TYPE_PIXART_TP_FIRMWARE);
}

static void
fu_pixart_tp_plugin_class_init(FuPixartTpPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_pixart_tp_plugin_constructed;
}
