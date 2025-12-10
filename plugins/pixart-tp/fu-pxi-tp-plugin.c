/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-pxi-tp-device.h"
#include "fu-pxi-tp-firmware.h"
#include "fu-pxi-tp-plugin.h"

struct _FuPxiTpPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuPxiTpPlugin, fu_pxi_tp_plugin, FU_TYPE_PLUGIN)

static void
fu_pxi_tp_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);

	/* register quirk keys used by FuPxiTpDevice */
	fu_context_add_quirk_key(ctx, "PxiTpHidVersionBank");
	fu_context_add_quirk_key(ctx, "PxiTpHidVersionAddr");
	fu_context_add_quirk_key(ctx, "PxiTpSramSelect");

	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_PXI_TP_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_PXI_TP_FIRMWARE);
}

static void
fu_pxi_tp_plugin_class_init(FuPxiTpPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_pxi_tp_plugin_constructed;
}

static void
fu_pxi_tp_plugin_init(FuPxiTpPlugin *self)
{
}
