/*
 * Copyright 2026 Sean Rhodes <sean@starlabs.systems>
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-starlabs-coreboot-device.h"
#include "fu-starlabs-coreboot-plugin.h"

struct _FuStarlabsCorebootPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuStarlabsCorebootPlugin, fu_starlabs_coreboot_plugin, FU_TYPE_PLUGIN)

#define FU_STARLABS_COREBOOT_PLUGIN_VERSION_MIN "26.02"

static void
fu_starlabs_coreboot_plugin_init(FuStarlabsCorebootPlugin *self)
{
	fu_plugin_add_flag(FU_PLUGIN(self), FWUPD_PLUGIN_FLAG_REQUIRE_HWID);
}

static void
fu_starlabs_coreboot_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_STARLABS_COREBOOT_DEVICE);

	/* chain up to parent */
	G_OBJECT_CLASS(fu_starlabs_coreboot_plugin_parent_class)->constructed(obj);
}

static gboolean
fu_starlabs_coreboot_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	g_autoptr(FuDevice) device = NULL;

	device = g_object_new(FU_TYPE_STARLABS_COREBOOT_DEVICE, "context", ctx, NULL);
	if (fu_version_compare(fu_device_get_version(device),
			       FU_STARLABS_COREBOOT_PLUGIN_VERSION_MIN,
			       FWUPD_VERSION_FORMAT_PAIR) >= 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "device is new enough to support mtd");
		return FALSE;
	}
	fu_plugin_add_device(plugin, device);
	return TRUE;
}

static void
fu_starlabs_coreboot_plugin_class_init(FuStarlabsCorebootPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_starlabs_coreboot_plugin_constructed;
	plugin_class->coldplug = fu_starlabs_coreboot_plugin_coldplug;
}
