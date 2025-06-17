/*
 * Copyright 2023 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-qc-s5gen2-ble-device.h"
#include "fu-qc-s5gen2-device.h"
#include "fu-qc-s5gen2-firmware.h"
#include "fu-qc-s5gen2-hid-device.h"
#include "fu-qc-s5gen2-plugin.h"

struct _FuQcS5gen2Plugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuQcS5gen2Plugin, fu_qc_s5gen2_plugin, FU_TYPE_PLUGIN)

static void
fu_qc_s5gen2_plugin_init(FuQcS5gen2Plugin *self)
{
	fu_plugin_add_flag(FU_PLUGIN(self), FWUPD_PLUGIN_FLAG_MUTABLE_ENUMERATION);
}

static void
fu_qc_s5gen2_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "AudioS5gen2Gaia3VendorId");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_QC_S5GEN2_BLE_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_QC_S5GEN2_HID_DEVICE);
	fu_plugin_set_device_gtype_default(plugin, FU_TYPE_QC_S5GEN2_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_QC_S5GEN2_FIRMWARE);
}

static void
fu_qc_s5gen2_plugin_class_init(FuQcS5gen2PluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_qc_s5gen2_plugin_constructed;
}
