/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-elantp-firmware.h"
#include "fu-elantp-hid-device.h"
#include "fu-elantp-i2c-device.h"
#include "fu-elantp-plugin.h"

struct _FuElantpPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuElantpPlugin, fu_elantp_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_elantp_plugin_device_created(FuPlugin *plugin, FuDevice *dev, GError **error)
{
	if (fu_device_get_specialized_gtype(dev) == FU_TYPE_ELANTP_I2C_DEVICE &&
	    !fu_context_has_hwid_flag(fu_plugin_get_context(plugin), "elantp-recovery") &&
	    !fu_device_has_private_flag(dev, FU_ELANTP_I2C_DEVICE_ABSOLUTE)) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "not required");
		return FALSE;
	}
	return TRUE;
}

static void
fu_elantp_plugin_init(FuElantpPlugin *self)
{
}

static void
fu_elantp_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "ElantpI2cTargetAddress");
	fu_context_add_quirk_key(ctx, "ElantpIapPassword");
	fu_context_add_quirk_key(ctx, "ElantpIcPageCount");
	fu_plugin_add_udev_subsystem(plugin, "i2c");
	fu_plugin_add_udev_subsystem(plugin, "i2c-dev");
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_ELANTP_FIRMWARE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_ELANTP_I2C_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_ELANTP_HID_DEVICE);
}

static void
fu_elantp_plugin_class_init(FuElantpPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_elantp_plugin_constructed;
	plugin_class->device_created = fu_elantp_plugin_device_created;
}
