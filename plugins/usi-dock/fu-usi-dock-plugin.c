/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 * Copyright 2021 Victor Cheng <victor_cheng@usiglobal.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-usi-dock-child-device.h"
#include "fu-usi-dock-dmc-device.h"
#include "fu-usi-dock-mcu-device.h"
#include "fu-usi-dock-plugin.h"

#define USI_DOCK_TBT_INSTANCE_ID "THUNDERBOLT\\VEN_0108&DEV_2031"

struct _FuUsiDockPlugin {
	FuPlugin parent_instance;
	FuDevice *device_tbt;
	FuDevice *device_usb2;
};

G_DEFINE_TYPE(FuUsiDockPlugin, fu_usi_dock_plugin, FU_TYPE_PLUGIN)

static FuUsiDockMcuDevice *
fu_usi_dock_plugin_find_mcu_device(FuPlugin *plugin)
{
	GPtrArray *devices = fu_plugin_get_devices(plugin);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device_tmp = g_ptr_array_index(devices, i);
		if (FU_IS_USI_DOCK_MCU_DEVICE(device_tmp))
			return FU_USI_DOCK_MCU_DEVICE(g_object_ref(device_tmp));
	}
	return NULL;
}

static void
fu_usi_dock_plugin_ensure_tbt4(FuPlugin *plugin)
{
	FuUsiDockPlugin *self = FU_USI_DOCK_PLUGIN(plugin);
	g_autoptr(FuDevice) device_usi = NULL;
	g_autoptr(FuUsiDockMcuDevice) device_mcu = NULL;

	if (self->device_tbt == NULL)
		return;
	device_mcu = fu_usi_dock_plugin_find_mcu_device(plugin);
	if (device_mcu == NULL)
		return;
	device_usi = fu_usi_dock_mcu_device_find_child(device_mcu, FU_USI_DOCK_FIRMWARE_IDX_TBT4);
	if (device_usi == NULL)
		return;
	fu_device_set_priority(device_usi, fu_device_get_priority(self->device_tbt) + 1);
	fu_device_set_equivalent_id(device_usi, fu_device_get_id(self->device_tbt));
	fu_device_set_equivalent_id(self->device_tbt, fu_device_get_id(device_usi));
}

static void
fu_usi_dock_plugin_ensure_usb2(FuPlugin *plugin)
{
	FuUsiDockPlugin *self = FU_USI_DOCK_PLUGIN(plugin);
	g_autoptr(FuDevice) device_usi = NULL;
	g_autoptr(FuUsiDockMcuDevice) device_mcu = NULL;

	if (self->device_usb2 == NULL)
		return;
	device_mcu = fu_usi_dock_plugin_find_mcu_device(plugin);
	if (device_mcu == NULL)
		return;
	device_usi = fu_usi_dock_mcu_device_find_child(device_mcu, FU_USI_DOCK_FIRMWARE_IDX_USB2);
	if (device_usi == NULL)
		return;
	fu_device_set_proxy(device_usi, self->device_usb2);
}

static void
fu_usi_dock_plugin_device_added(FuPlugin *plugin, FuDevice *device)
{
	fu_usi_dock_plugin_ensure_tbt4(plugin);
	fu_usi_dock_plugin_ensure_usb2(plugin);
}

static void
fu_usi_dock_plugin_device_registered(FuPlugin *plugin, FuDevice *device)
{
	FuUsiDockPlugin *self = FU_USI_DOCK_PLUGIN(plugin);

	/* usb device from thunderbolt plugin */
	if (g_strcmp0(fu_device_get_plugin(device), "thunderbolt") == 0 &&
	    fu_device_has_guid(device, USI_DOCK_TBT_INSTANCE_ID)) {
		g_set_object(&self->device_tbt, device);
		fu_usi_dock_plugin_ensure_tbt4(plugin);
	}

	/* we may need to reset this manually */
	if (fu_device_get_vid(device) == 0x17EF && fu_device_get_pid(device) == 0x30BA) {
		g_set_object(&self->device_usb2, device);
		fu_usi_dock_plugin_ensure_usb2(plugin);
	}
}

static void
fu_usi_dock_plugin_init(FuUsiDockPlugin *self)
{
	fu_plugin_add_flag(FU_PLUGIN(self), FWUPD_PLUGIN_FLAG_MUTABLE_ENUMERATION);
}

static void
fu_usi_dock_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "usb");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_USI_DOCK_MCU_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_USI_DOCK_DMC_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_USI_DOCK_CHILD_DEVICE); /* coverage */
}

static void
fu_usi_dock_plugin_finalize(GObject *obj)
{
	FuUsiDockPlugin *self = FU_USI_DOCK_PLUGIN(obj);
	g_clear_object(&self->device_tbt);
	g_clear_object(&self->device_usb2);
	G_OBJECT_CLASS(fu_usi_dock_plugin_parent_class)->finalize(obj);
}

static void
fu_usi_dock_plugin_class_init(FuUsiDockPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_usi_dock_plugin_finalize;
	plugin_class->constructed = fu_usi_dock_plugin_constructed;
	plugin_class->device_added = fu_usi_dock_plugin_device_added;
	plugin_class->device_registered = fu_usi_dock_plugin_device_registered;
}
