/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-bcm57xx-device.h"
#include "fu-bcm57xx-dict-image.h"
#include "fu-bcm57xx-firmware.h"
#include "fu-bcm57xx-stage1-image.h"
#include "fu-bcm57xx-stage2-image.h"

static void
fu_plugin_bcm57xx_init(FuPlugin *plugin)
{
	fu_plugin_add_udev_subsystem(plugin, "pci");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_BCM57XX_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_BCM57XX_FIRMWARE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_BCM57XX_DICT_IMAGE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_BCM57XX_STAGE1_IMAGE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_BCM57XX_STAGE2_IMAGE);
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_BETTER_THAN, "optionrom");
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_bcm57xx_init;
}
