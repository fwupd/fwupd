/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-intel-spi-device.h"

static void
fu_plugin_intel_spi_load(FuContext *ctx)
{
	fu_context_add_quirk_key(ctx, "IntelSpiKind");
	fu_context_add_quirk_key(ctx, "IntelSpiBar");
	fu_context_add_quirk_key(ctx, "IntelSpiBarProxy");
	fu_context_add_quirk_key(ctx, "IntelSpiBiosCntl");
}

static void
fu_plugin_intel_spi_init(FuPlugin *plugin)
{
	fu_plugin_add_udev_subsystem(plugin, "pci");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_INTEL_SPI_DEVICE);
}

static gboolean
fu_plugin_intel_spi_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
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

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->load = fu_plugin_intel_spi_load;
	vfuncs->init = fu_plugin_intel_spi_init;
	vfuncs->startup = fu_plugin_intel_spi_startup;
}
