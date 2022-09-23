/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-intel-spi-device.h"
#include "fu-intel-spi-plugin.h"

struct _FuIntelSpiPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuIntelSpiPlugin, fu_intel_spi_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_intel_spi_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	if (fu_kernel_locked_down()) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "not supported when kernel locked down");
		return FALSE;
	}
	return TRUE;
}

static void
fu_intel_spi_plugin_init(FuIntelSpiPlugin *self)
{
}

static void
fu_intel_spi_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "IntelSpiKind");
	fu_context_add_quirk_key(ctx, "IntelSpiBar");
	fu_context_add_quirk_key(ctx, "IntelSpiBarProxy");
	fu_context_add_quirk_key(ctx, "IntelSpiBiosCntl");
	fu_plugin_add_udev_subsystem(plugin, "pci");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_INTEL_SPI_DEVICE);
}

static void
fu_intel_spi_plugin_class_init(FuIntelSpiPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->constructed = fu_intel_spi_plugin_constructed;
	plugin_class->startup = fu_intel_spi_plugin_startup;
}
