/*
 * Copyright (c) 1999-2021 Logitech, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-logitech-bulkcontroller-device.h"
#include "fu-logitech-bulkcontroller-plugin.h"

struct _FuLogitechBulkcontrollerPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuLogitechBulkcontrollerPlugin, fu_logitech_bulkcontroller_plugin, FU_TYPE_PLUGIN)

static void
fu_logitech_bulkcontroller_plugin_init(FuLogitechBulkcontrollerPlugin *self)
{
}

static void
fu_logitech_bulkcontroller_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_LOGITECH_BULKCONTROLLER_DEVICE);
}

static void
fu_logitech_bulkcontroller_plugin_class_init(FuLogitechBulkcontrollerPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_logitech_bulkcontroller_plugin_constructed;
}
