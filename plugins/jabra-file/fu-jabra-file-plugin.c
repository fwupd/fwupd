/*
 * Copyright 2023 GN Audio A/S
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-jabra-file-device.h"
#include "fu-jabra-file-firmware.h"
#include "fu-jabra-file-plugin.h"

struct _FuJabraFilePlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuJabraFilePlugin, fu_jabra_file_plugin, FU_TYPE_PLUGIN)

static void
fu_jabra_file_plugin_init(FuJabraFilePlugin *self)
{
}

static void
fu_jabra_file_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_JABRA_FILE_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_JABRA_FILE_FIRMWARE);
}

static void
fu_jabra_file_plugin_class_init(FuJabraFilePluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_jabra_file_plugin_constructed;
}
