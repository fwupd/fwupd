/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-wacom-usb-android-device.h"
#include "fu-wacom-usb-device.h"
#include "fu-wacom-usb-firmware.h"
#include "fu-wacom-usb-module-bluetooth-id6.h"
#include "fu-wacom-usb-module-bluetooth-id9.h"
#include "fu-wacom-usb-module-bluetooth.h"
#include "fu-wacom-usb-module-scaler.h"
#include "fu-wacom-usb-module-sub-cpu.h"
#include "fu-wacom-usb-module-touch-id7.h"
#include "fu-wacom-usb-module-touch.h"
#include "fu-wacom-usb-plugin.h"

struct _FuWacomUsbPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuWacomUsbPlugin, fu_wacom_usb_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_wacom_usb_plugin_write_firmware(FuPlugin *plugin,
				   FuDevice *device,
				   FuFirmware *firmware,
				   FuProgress *progress,
				   FwupdInstallFlags flags,
				   GError **error)
{
	FuDevice *parent = fu_device_get_parent(device, NULL);
	g_autoptr(FuDeviceLocker) locker = NULL;
	locker = fu_device_locker_new(parent != NULL ? parent : device, error);
	if (locker == NULL)
		return FALSE;
	return fu_device_write_firmware(device, firmware, progress, flags, error);
}

static gboolean
fu_wacom_usb_plugin_composite_prepare(FuPlugin *self, GPtrArray *devices, GError **error)
{
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		if (FU_IS_WACOM_USB_DEVICE(device)) {
			g_autoptr(FuDeviceLocker) locker = fu_device_locker_new(device, error);
			if (locker == NULL)
				return FALSE;
			g_info("switching main device to flash loader");
			if (!fu_wacom_usb_device_switch_to_flash_loader(FU_WACOM_USB_DEVICE(device),
									error))
				return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_wacom_usb_plugin_composite_cleanup(FuPlugin *self, GPtrArray *devices, GError **error)
{
	g_autoptr(FuWacomUsbDevice) main_device = NULL;

	/* find the main device in transaction (which may *be* the main device, or just a proxy) */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device_tmp = g_ptr_array_index(devices, i);
		if (FU_IS_WACOM_USB_DEVICE(device_tmp)) {
			g_set_object(&main_device, FU_WACOM_USB_DEVICE(device_tmp));
			break;
		}
		if (FU_IS_WACOM_USB_MODULE(device_tmp)) {
			FuWacomUsbDevice *proxy;
			proxy = FU_WACOM_USB_DEVICE(fu_device_get_proxy(device_tmp, error));
			if (proxy == NULL)
				return FALSE;
			g_set_object(&main_device, proxy);
			break;
		}
	}

	/* reset */
	if (main_device != NULL) {
		g_autoptr(FuDeviceLocker) locker =
		    fu_device_locker_new(FU_DEVICE(main_device), error);
		if (locker == NULL)
			return FALSE;
		g_info("resetting main device");
		fu_device_add_flag(FU_DEVICE(main_device), FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
		if (!fu_wacom_usb_device_update_reset(main_device, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_wacom_usb_plugin_init(FuWacomUsbPlugin *self)
{
	fu_plugin_add_flag(FU_PLUGIN(self), FWUPD_PLUGIN_FLAG_MUTABLE_ENUMERATION);
}

static void
fu_wacom_usb_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "usb");
	fu_plugin_set_device_gtype_default(plugin, FU_TYPE_WACOM_USB_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_WACOM_USB_ANDROID_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_WACOM_USB_MODULE_BLUETOOTH);	    /* coverage */
	fu_plugin_add_device_gtype(plugin, FU_TYPE_WACOM_USB_MODULE_BLUETOOTH_ID6); /* coverage */
	fu_plugin_add_device_gtype(plugin, FU_TYPE_WACOM_USB_MODULE_BLUETOOTH_ID9); /* coverage */
	fu_plugin_add_device_gtype(plugin, FU_TYPE_WACOM_USB_MODULE_SCALER);	    /* coverage */
	fu_plugin_add_device_gtype(plugin, FU_TYPE_WACOM_USB_MODULE_SUB_CPU);	    /* coverage */
	fu_plugin_add_device_gtype(plugin, FU_TYPE_WACOM_USB_MODULE_TOUCH);	    /* coverage */
	fu_plugin_add_device_gtype(plugin, FU_TYPE_WACOM_USB_MODULE_TOUCH_ID7);	    /* coverage */
	fu_plugin_add_firmware_gtype(plugin, "wacom", FU_TYPE_WACOM_USB_FIRMWARE);
}

static void
fu_wacom_usb_plugin_class_init(FuWacomUsbPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_wacom_usb_plugin_constructed;
	plugin_class->write_firmware = fu_wacom_usb_plugin_write_firmware;
	plugin_class->composite_prepare = fu_wacom_usb_plugin_composite_prepare;
	plugin_class->composite_cleanup = fu_wacom_usb_plugin_composite_cleanup;
}
