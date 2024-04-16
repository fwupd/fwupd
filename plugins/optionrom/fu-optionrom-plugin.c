/*
 * Copyright 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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
	fu_plugin_add_device_udev_subsystem(plugin, "pci");
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_CONFLICTS, "udev");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_OPTIONROM_DEVICE);
}

static gboolean
fu_optionrom_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	if (fu_context_has_hwid_flag(ctx, "no-probe")) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "not supported on this platform");
		return FALSE;
	}
	return TRUE;
}

static void
fu_optionrom_plugin_class_init(FuOptionromPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_optionrom_plugin_constructed;
	plugin_class->startup = fu_optionrom_plugin_startup;
}
