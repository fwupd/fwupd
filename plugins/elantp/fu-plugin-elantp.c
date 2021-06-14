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

gboolean
fu_plugin_device_created (FuPlugin *plugin, FuDevice *dev, GError **error)
{
	if (fu_device_get_specialized_gtype (dev) == FU_TYPE_ELANTP_I2C_DEVICE &&
	    !fu_plugin_has_custom_flag (plugin, "elantp-recovery")) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "not required");
		return FALSE;
	}
	return TRUE;
}

void
fu_plugin_init (FuPlugin *plugin)
{
	FuContext *ctx = fu_plugin_get_context (plugin);
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_context_add_udev_subsystem (ctx, "i2c-dev");
	fu_context_add_udev_subsystem (ctx, "hidraw");
	fu_plugin_add_firmware_gtype (plugin, NULL, FU_TYPE_ELANTP_FIRMWARE);
	fu_context_add_quirk_key (ctx, "ElantpI2cTargetAddress");
	fu_context_add_quirk_key (ctx, "ElantpIapPassword");
	fu_context_add_quirk_key (ctx, "ElantpIcPageCount");
	fu_plugin_add_device_gtype (plugin, FU_TYPE_ELANTP_I2C_DEVICE);
	fu_plugin_add_device_gtype (plugin, FU_TYPE_ELANTP_HID_DEVICE);
}
