/*
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-optionrom-device.h"
#include "fu-optionrom-plugin.h"

struct _FuOptionromPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuOptionromPlugin, fu_optionrom_plugin, FU_TYPE_PLUGIN)

static void
fu_optionrom_plugin_init(FuOptionromPlugin *self)
{
}

static void
fu_optionrom_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "pci");
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_CONFLICTS, "udev");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_OPTIONROM_DEVICE);
}

static void
fu_optionrom_plugin_class_init(FuOptionromPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_optionrom_plugin_constructed;
}
