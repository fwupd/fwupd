/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-aspeed-ast2x00-native-device.h"
#include "fu-aspeed-ast2x00-plugin.h"

struct _FuAspeedAst2x00Plugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuAspeedAst2x00Plugin, fu_aspeed_ast2x00_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_aspeed_ast2x00_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuDevice) device = g_object_new(FU_TYPE_ASPEED_AST2X00_NATIVE_DEVICE,
						  "context",
						  fu_plugin_get_context(plugin),
						  NULL);
	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;
	if (!fu_device_setup(device, error))
		return FALSE;
	fu_plugin_device_add(plugin, device);
	return TRUE;
}

static void
fu_aspeed_ast2x00_plugin_init(FuAspeedAst2x00Plugin *self)
{
}

static void
fu_aspeed_ast2x00_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "AspeedAst2x00Revision");
	fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_REQUIRE_HWID);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_ASPEED_AST2X00_NATIVE_DEVICE);
}

static void
fu_aspeed_ast2x00_plugin_class_init(FuAspeedAst2x00PluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_aspeed_ast2x00_plugin_constructed;
	plugin_class->coldplug = fu_aspeed_ast2x00_plugin_coldplug;
}
