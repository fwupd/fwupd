/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-synaptics-cxaudio-device.h"
#include "fu-synaptics-cxaudio-firmware.h"
#include "fu-synaptics-cxaudio-plugin.h"

struct _FuSynapticsCxaudioPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuSynapticsCxaudioPlugin, fu_synaptics_cxaudio_plugin, FU_TYPE_PLUGIN)

static void
fu_synaptics_cxaudio_plugin_init(FuSynapticsCxaudioPlugin *self)
{
}

static void
fu_synaptics_cxaudio_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "CxaudioChipIdBase");
	fu_context_add_quirk_key(ctx, "CxaudioPatch1ValidAddr");
	fu_context_add_quirk_key(ctx, "CxaudioPatch2ValidAddr");
	fu_context_add_quirk_key(ctx, "CxaudioSoftwareReset");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_SYNAPTICS_CXAUDIO_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_SYNAPTICS_CXAUDIO_FIRMWARE);
}

static void
fu_synaptics_cxaudio_plugin_class_init(FuSynapticsCxaudioPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_synaptics_cxaudio_plugin_constructed;
}
