/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-qc-firehose-plugin.h"
#include "fu-qc-firehose-raw-device.h"
#include "fu-qc-firehose-usb-device.h"

struct _FuQcFirehosePlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuQcFirehosePlugin, fu_qc_firehose_plugin, FU_TYPE_PLUGIN)

static void
fu_qc_firehose_plugin_init(FuQcFirehosePlugin *self)
{
}

static void
fu_qc_firehose_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_QC_FIREHOSE_USB_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_QC_FIREHOSE_RAW_DEVICE);
	fu_plugin_add_udev_subsystem(plugin, "wwan");
}

static void
fu_qc_firehose_plugin_class_init(FuQcFirehosePluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_qc_firehose_plugin_constructed;
}
