/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-mtd-device.h"
#include "fu-mtd-plugin.h"

struct _FuMtdPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuMtdPlugin, fu_mtd_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_mtd_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
#ifndef HAVE_MTD_USER_H
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Not compiled with mtd support");
	return FALSE;
#endif
	return TRUE;
}

static void
fu_mtd_plugin_init(FuMtdPlugin *self)
{
}

static void
fu_mtd_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "MtdMetadataOffset");
	fu_context_add_quirk_key(ctx, "MtdMetadataSize");
	fu_plugin_add_device_udev_subsystem(plugin, "mtd");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_MTD_DEVICE);
}

static void
fu_mtd_plugin_class_init(FuMtdPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_mtd_plugin_constructed;
	plugin_class->startup = fu_mtd_plugin_startup;
}
