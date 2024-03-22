/*
 * Copyright (C) 2023 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-audio-s5gen2-device.h"
#include "fu-audio-s5gen2-firmware.h"
#include "fu-audio-s5gen2-hid-device.h"
#include "fu-audio-s5gen2-plugin.h"

struct _FuAudioS5gen2Plugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuAudioS5gen2Plugin, fu_audio_s5gen2_plugin, FU_TYPE_PLUGIN)

static void
fu_audio_s5gen2_plugin_init(FuAudioS5gen2Plugin *self)
{
}

static void
fu_audio_s5gen2_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_QC_S5GEN2_HID_DEVICE);
	fu_plugin_set_device_gtype_default(plugin, FU_TYPE_QC_S5GEN2_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_QC_S5GEN2_FIRMWARE);
}

static void
fu_audio_s5gen2_plugin_class_init(FuAudioS5gen2PluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_audio_s5gen2_plugin_constructed;
}
