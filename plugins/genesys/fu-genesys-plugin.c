/*
 * Copyright (C) 2021 Ricardo Cañuelo <ricardo.canuelo@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-genesys-hubhid-device.h"
#include "fu-genesys-plugin.h"
#include "fu-genesys-scaler-firmware.h"
#include "fu-genesys-usbhub-device.h"
#include "fu-genesys-usbhub-firmware.h"

struct _FuGenesysPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuGenesysPlugin, fu_genesys_plugin, FU_TYPE_PLUGIN)

static void
fu_genesys_plugin_init(FuGenesysPlugin *self)
{
}

static void
fu_genesys_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "GenesysScalerCfiFlashId");
	fu_context_add_quirk_key(ctx, "GenesysScalerGpioOutputRegister");
	fu_context_add_quirk_key(ctx, "GenesysScalerGpioEnableRegister");
	fu_context_add_quirk_key(ctx, "GenesysScalerGpioValue");
	fu_context_add_quirk_key(ctx, "GenesysUsbhubReadRequest");
	fu_context_add_quirk_key(ctx, "GenesysUsbhubSwitchRequest");
	fu_context_add_quirk_key(ctx, "GenesysUsbhubWriteRequest");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_GENESYS_USBHUB_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_GENESYS_HUBHID_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_GENESYS_USBHUB_FIRMWARE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_GENESYS_SCALER_FIRMWARE);
}

static FuDevice *
fu_genesys_plugin_get_device_by_gusb_device(FuPlugin *self, GUsbDevice *gusb_device)
{
	GPtrArray *devices = fu_plugin_get_devices(self);

	for (guint i = 0; i < devices->len; i++) {
		FuDevice *dev = g_ptr_array_index(devices, i);

		if (!FU_IS_GENESYS_USBHUB_DEVICE(dev))
			continue;

		if (fu_usb_device_get_dev(FU_USB_DEVICE(dev)) == gusb_device)
			return dev;
	}

	return NULL;
}

static void
fu_genesys_plugin_device_added(FuPlugin *self, FuDevice *device)
{
	GUsbDevice *gusb_parent = NULL;
	FuDevice *parent = NULL;

	/* link hid to parent hub */
	if (!FU_IS_GENESYS_HUBHID_DEVICE(device))
		return;

	gusb_parent = g_usb_device_get_parent(fu_usb_device_get_dev(FU_USB_DEVICE(device)));
	g_return_if_fail(gusb_parent);
	parent = fu_genesys_plugin_get_device_by_gusb_device(self, gusb_parent);
	if (parent == NULL) {
		g_warning("hubhid cannot find parent, platform_id(%s)",
			  g_usb_device_get_platform_id(gusb_parent));
		fu_plugin_device_remove(self, device);
	} else {
		fu_genesys_usbhub_device_set_hid_channel(parent, device);
		fu_device_add_child(parent, device);
	}
}

static void
fu_genesys_plugin_class_init(FuGenesysPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_genesys_plugin_constructed;
	plugin_class->device_added = fu_genesys_plugin_device_added;
}
