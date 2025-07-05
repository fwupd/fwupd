/*
 * Copyright 1999-2021 Logitech, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-logitech-bulkcontroller-child.h"
#include "fu-logitech-bulkcontroller-device.h"
#include "fu-logitech-bulkcontroller-plugin.h"

struct _FuLogitechBulkcontrollerPlugin {
	FuPlugin parent_instance;
	gboolean post_install;
};

G_DEFINE_TYPE(FuLogitechBulkcontrollerPlugin, fu_logitech_bulkcontroller_plugin, FU_TYPE_PLUGIN)

static void
fu_logitech_bulkcontroller_plugin_to_string(FuPlugin *plugin, guint idt, GString *str)
{
	FuLogitechBulkcontrollerPlugin *self = FU_LOGITECH_BULKCONTROLLER_PLUGIN(plugin);
	fwupd_codec_string_append_bool(str, idt, "PostInstall", self->post_install);
}

static void
fu_logitech_bulkcontroller_plugin_init(FuLogitechBulkcontrollerPlugin *self)
{
}

static gboolean
fu_logitech_bulkcontroller_plugin_write_firmware(FuPlugin *plugin,
						 FuDevice *device,
						 FuFirmware *firmware,
						 FuProgress *progress,
						 FwupdInstallFlags flags,
						 GError **error)
{
	FuLogitechBulkcontrollerPlugin *self = FU_LOGITECH_BULKCONTROLLER_PLUGIN(plugin);
	g_autoptr(FuDeviceLocker) locker = NULL;

	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;
	if (!fu_device_write_firmware(device, firmware, progress, flags, error))
		return FALSE;
	self->post_install = TRUE;
	return TRUE;
}

static gboolean
fu_logitech_bulkcontroller_plugin_device_created(FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuLogitechBulkcontrollerPlugin *self = FU_LOGITECH_BULKCONTROLLER_PLUGIN(plugin);
	if (self->post_install) {
		fu_device_add_private_flag(device,
					   FU_LOGITECH_BULKCONTROLLER_DEVICE_FLAG_POST_INSTALL);
		self->post_install = FALSE;
	}
	return TRUE;
}

static void
fu_logitech_bulkcontroller_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_set_device_gtype_default(plugin, FU_TYPE_LOGITECH_BULKCONTROLLER_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_LOGITECH_BULKCONTROLLER_CHILD); /* coverage */
}

static void
fu_logitech_bulkcontroller_plugin_class_init(FuLogitechBulkcontrollerPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_logitech_bulkcontroller_plugin_constructed;
	plugin_class->write_firmware = fu_logitech_bulkcontroller_plugin_write_firmware;
	plugin_class->device_created = fu_logitech_bulkcontroller_plugin_device_created;
	plugin_class->to_string = fu_logitech_bulkcontroller_plugin_to_string;
}
