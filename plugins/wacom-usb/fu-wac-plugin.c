/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-wac-android-device.h"
#include "fu-wac-device.h"
#include "fu-wac-firmware.h"
#include "fu-wac-plugin.h"

struct _FuWacPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuWacPlugin, fu_wac_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_wac_plugin_write_firmware(FuPlugin *plugin,
			     FuDevice *device,
			     GBytes *blob_fw,
			     FuProgress *progress,
			     FwupdInstallFlags flags,
			     GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);
	g_autoptr(FuDeviceLocker) locker = NULL;
	locker = fu_device_locker_new(parent != NULL ? parent : device, error);
	if (locker == NULL)
		return FALSE;
	return fu_device_write_firmware(device, blob_fw, progress, flags, error);
}

static gboolean
fu_wac_plugin_composite_prepare(FuPlugin *self, GPtrArray *devices, GError **error)
{
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		if (FU_IS_WAC_DEVICE(device)) {
			g_autoptr(FuDeviceLocker) locker = fu_device_locker_new(device, error);
			if (locker == NULL)
				return FALSE;
			g_info("switching main device to flash loader");
			if (!fu_wac_device_switch_to_flash_loader(FU_WAC_DEVICE(device), error))
				return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_wac_plugin_composite_cleanup(FuPlugin *self, GPtrArray *devices, GError **error)
{
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		if (FU_IS_WAC_DEVICE(device)) {
			g_autoptr(FuDeviceLocker) locker = fu_device_locker_new(device, error);
			if (locker == NULL)
				return FALSE;
			g_info("resetting main device");
			fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
			if (!fu_wac_device_update_reset(FU_WAC_DEVICE(device), error))
				return FALSE;
		}
	}
	return TRUE;
}

static void
fu_wac_plugin_init(FuWacPlugin *self)
{
}

static void
fu_wac_plugin_object_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_set_name(plugin, "wacom_usb");
}

static void
fu_wac_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_WAC_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_WAC_ANDROID_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, "wacom", FU_TYPE_WAC_FIRMWARE);
}

static void
fu_wac_plugin_class_init(FuWacPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_wac_plugin_object_constructed;
	plugin_class->constructed = fu_wac_plugin_constructed;
	plugin_class->write_firmware = fu_wac_plugin_write_firmware;
	plugin_class->composite_prepare = fu_wac_plugin_composite_prepare;
	plugin_class->composite_cleanup = fu_wac_plugin_composite_cleanup;
}
