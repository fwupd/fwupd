/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"

#include "fu-vli-pd-device.h"
#include "fu-vli-pd-firmware.h"
#include "fu-vli-usbhub-device.h"
#include "fu-vli-usbhub-firmware.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_firmware_gtype (plugin, "vli-usbhub", FU_TYPE_VLI_USBHUB_FIRMWARE);
	fu_plugin_add_firmware_gtype (plugin, "vli-pd", FU_TYPE_VLI_PD_FIRMWARE);

	/* register the custom types */
	g_type_ensure (FU_TYPE_VLI_USBHUB_DEVICE);
	g_type_ensure (FU_TYPE_VLI_PD_DEVICE);
}

/* reboot the FuVliUsbhubDevice if we update the FuVliUsbhubPdDevice */
static FuDevice *
fu_plugin_vli_get_parent (GPtrArray *devices)
{
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *dev = g_ptr_array_index (devices, i);
		FuDevice *parent = fu_device_get_parent (dev);
		if (parent != NULL && FU_IS_VLI_USBHUB_DEVICE (parent))
			return g_object_ref (parent);
		if (FU_IS_VLI_USBHUB_DEVICE (dev))
			return g_object_ref (dev);
	}
	return NULL;
}

gboolean
fu_plugin_composite_cleanup (FuPlugin *plugin,
			     GPtrArray *devices,
			     GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuDevice) parent = fu_plugin_vli_get_parent (devices);
	if (parent == NULL)
		return TRUE;
	locker = fu_device_locker_new (parent, error);
	if (locker == NULL)
		return FALSE;
	return fu_device_attach (parent, error);
}
