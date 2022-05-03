/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-elantp-firmware.h"
#include "fu-elantp-hid-device.h"
#include "fu-elantp-i2c-device.h"

static gboolean
fu_plugin_elantp_device_created(FuPlugin *plugin, FuDevice *dev, GError **error)
{
	if (fu_device_get_specialized_gtype(dev) == FU_TYPE_ELANTP_I2C_DEVICE &&
	    !fu_context_has_hwid_flag(fu_plugin_get_context(plugin), "elantp-recovery")) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "not required");
		return FALSE;
	}
	return TRUE;
}

static void
fu_plugin_elantp_load(FuContext *ctx)
{
	fu_context_add_quirk_key(ctx, "ElantpI2cTargetAddress");
	fu_context_add_quirk_key(ctx, "ElantpIapPassword");
	fu_context_add_quirk_key(ctx, "ElantpIcPageCount");
}

static void
fu_plugin_elantp_init(FuPlugin *plugin)
{
	fu_plugin_add_udev_subsystem(plugin, "i2c-dev");
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_ELANTP_FIRMWARE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_ELANTP_I2C_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_ELANTP_HID_DEVICE);
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->load = fu_plugin_elantp_load;
	vfuncs->init = fu_plugin_elantp_init;
	vfuncs->device_created = fu_plugin_elantp_device_created;
}
