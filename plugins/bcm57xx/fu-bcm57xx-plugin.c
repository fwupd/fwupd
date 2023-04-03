/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-bcm57xx-device.h"
#include "fu-bcm57xx-dict-image.h"
#include "fu-bcm57xx-firmware.h"
#include "fu-bcm57xx-plugin.h"
#include "fu-bcm57xx-stage1-image.h"
#include "fu-bcm57xx-stage2-image.h"

struct _FuBcm57XxPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuBcm57XxPlugin, fu_bcm57xx_plugin, FU_TYPE_PLUGIN)

static void
fu_bcm57xx_plugin_init(FuBcm57XxPlugin *self)
{
}

static void
fu_bcm57xx_plugin_object_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_set_name(plugin, "bcm57xx");
}

static void
fu_bcm57xx_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "pci");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_BCM57XX_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_BCM57XX_FIRMWARE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_BCM57XX_DICT_IMAGE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_BCM57XX_STAGE1_IMAGE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_BCM57XX_STAGE2_IMAGE);
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_BETTER_THAN, "optionrom");
}

static void
fu_bcm57xx_plugin_class_init(FuBcm57XxPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_bcm57xx_plugin_object_constructed;
	plugin_class->constructed = fu_bcm57xx_plugin_constructed;
}
