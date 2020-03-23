/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-hash.h"
#include "fu-plugin-vfuncs.h"

#include "fu-ccgx-cyacd-firmware.h"
#include "fu-ccgx-dock-bb.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_firmware_gtype (plugin, "ccgx-cyacd", FU_TYPE_CCGX_CYACD_FIRMWARE);
	fu_plugin_set_device_gtype (plugin, FU_TYPE_CCGX_DOCK_BB);
}

static FuDevice *
fu_plugin_ccgx_dock_get_bb (GPtrArray *devices)
{
	FuDevice *parent = NULL;
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *dev = g_ptr_array_index (devices, i);
		if (FU_IS_CCGX_DOCK_BB (dev))
			return dev;
		parent = fu_device_get_parent (dev);
		if (parent != NULL && FU_IS_CCGX_DOCK_BB (parent))
			return parent;
	}
	return NULL;
}

gboolean
fu_plugin_composite_cleanup (FuPlugin *plugin,
			     GPtrArray *devices,
			     GError **error)
{
	FuDevice *device = fu_plugin_ccgx_dock_get_bb (devices);
	if (device != NULL) {
		g_autoptr(FuDeviceLocker) locker = fu_device_locker_new (device, error);
		if (locker == NULL)
			return FALSE;
		return fu_ccgx_dock_bb_reboot (device, error);
	}
	return TRUE;
}
