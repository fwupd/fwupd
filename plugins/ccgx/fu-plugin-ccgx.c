/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-hash.h"
#include "fu-plugin-vfuncs.h"

#include "fu-ccgx-firmware.h"
#include "fu-ccgx-hid-device.h"
#include "fu-ccgx-hpi-device.h"

struct FuPluginData {
	gboolean	 seen_primary;
};

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_firmware_gtype (plugin, "ccgx", FU_TYPE_CCGX_FIRMWARE);
	fu_plugin_set_device_gtype (plugin, FU_TYPE_CCGX_HID_DEVICE);
	fu_plugin_set_device_gtype (plugin, FU_TYPE_CCGX_HPI_DEVICE);
	fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
}

static void
fu_plugin_ccgx_device_flags_cb (FuDevice *device, GParamSpec *pspec, FuPlugin *plugin)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	FuCcgxHpiDevice *self = FU_CCGX_HPI_DEVICE (device);

	/* FuDeviceList unsets this as soon as the device is disconnected */
	if (!fu_device_has_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE) &&
	    priv->seen_primary) {
		g_debug ("CCGX device went away, so assuming no primary");
		priv->seen_primary = FALSE;
		return;
	}

	/* show the *counterpart* device whenever in the primary firmware,
	 * or when we're in the backup FW and have seen the primary at
	 * least once -- otherwise we do RECOVERY -> PRIMARY -> RECOVERY
	 * on a device with initially corrupt firmware */
	if (fu_ccgx_hpi_device_get_fw_mode (self) == FW_MODE_FW2 &&
	    !priv->seen_primary) {
		g_debug ("seen CCGX primary");
		priv->seen_primary = TRUE;
	}
	if (priv->seen_primary) {
		fu_device_add_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN);
//		GPtrArray *children = fu_device_get_children (device);
//		for (guint i = 0; i < children->len; i++) {
//			FuDevice *child = g_ptr_array_index (children, i);
//			fu_device_add_flag (child, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN);
//		}
	}
}

gboolean
fu_plugin_device_created (FuPlugin *plugin, FuDevice *dev, GError **error)
{
	if (FU_IS_CCGX_HPI_DEVICE (dev)) {
		g_signal_connect (dev, "notify::flags",
				  G_CALLBACK (fu_plugin_ccgx_device_flags_cb),
				  plugin);
	}
	return TRUE;
}
