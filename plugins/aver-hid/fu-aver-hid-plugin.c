/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-aver-hid-device.h"
#include "fu-aver-hid-firmware.h"
#include "fu-aver-hid-plugin.h"
#include "fu-aver-safeisp-device.h"

struct _FuAverHidPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuAverHidPlugin, fu_aver_hid_plugin, FU_TYPE_PLUGIN)

static void
fu_aver_hid_plugin_init(FuAverHidPlugin *self)
{
	fu_plugin_add_flag(FU_PLUGIN(self), FWUPD_PLUGIN_FLAG_MUTABLE_ENUMERATION);
}

static void
fu_aver_hid_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_AVER_HID_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_AVER_SAFEISP_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_AVER_HID_FIRMWARE);
}

static void
fu_aver_hid_plugin_class_init(FuAverHidPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_aver_hid_plugin_constructed;
}
