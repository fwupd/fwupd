/*
 * Copyright 1999-2021 Logitech, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-logitech-bulkcontroller-child.h"
#include "fu-logitech-bulkcontroller-device.h"
#include "fu-logitech-bulkcontroller-plugin.h"

#define FU_LOGITECH_BULKCONTROLLER_PLUGIN_FLAG_POST_INSTALL "post-install"

struct _FuLogitechBulkcontrollerPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuLogitechBulkcontrollerPlugin, fu_logitech_bulkcontroller_plugin, FU_TYPE_PLUGIN)

static void
fu_logitech_bulkcontroller_plugin_init(FuLogitechBulkcontrollerPlugin *self)
{
	fu_plugin_register_private_flag(FU_PLUGIN(self),
					FU_LOGITECH_BULKCONTROLLER_PLUGIN_FLAG_POST_INSTALL);
}

static gboolean
fu_logitech_bulkcontroller_plugin_write_firmware(FuPlugin *plugin,
						 FuDevice *device,
						 FuFirmware *firmware,
						 FuProgress *progress,
						 FwupdInstallFlags flags,
						 GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;

	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;
	if (!fu_device_write_firmware(device, firmware, progress, flags, error))
		return FALSE;
	fu_plugin_add_private_flag(plugin, FU_LOGITECH_BULKCONTROLLER_PLUGIN_FLAG_POST_INSTALL);
	return TRUE;
}

static gboolean
fu_logitech_bulkcontroller_plugin_device_created(FuPlugin *plugin, FuDevice *device, GError **error)
{
	if (fu_plugin_has_private_flag(plugin,
				       FU_LOGITECH_BULKCONTROLLER_PLUGIN_FLAG_POST_INSTALL)) {
		fu_device_add_private_flag(device,
					   FU_LOGITECH_BULKCONTROLLER_DEVICE_FLAG_POST_INSTALL);
		fu_plugin_remove_private_flag(plugin,
					      FU_LOGITECH_BULKCONTROLLER_PLUGIN_FLAG_POST_INSTALL);
	}
	return TRUE;
}

static void
fu_logitech_bulkcontroller_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "usb");
	fu_plugin_set_device_gtype_default(plugin, FU_TYPE_LOGITECH_BULKCONTROLLER_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_LOGITECH_BULKCONTROLLER_CHILD); /* coverage */

	/* chain up to parent */
	G_OBJECT_CLASS(fu_logitech_bulkcontroller_plugin_parent_class)->constructed(obj);
}

static void
fu_logitech_bulkcontroller_plugin_class_init(FuLogitechBulkcontrollerPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_logitech_bulkcontroller_plugin_constructed;
	plugin_class->write_firmware = fu_logitech_bulkcontroller_plugin_write_firmware;
	plugin_class->device_created = fu_logitech_bulkcontroller_plugin_device_created;
}
