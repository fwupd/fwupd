/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-mtd-device.h"

static void
fu_plugin_mtd_init(FuPlugin *plugin)
{
	fu_plugin_add_udev_subsystem(plugin, "mtd");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_MTD_DEVICE);
}

static gboolean
fu_plugin_mtd_startup(FuPlugin *plugin, GError **error)
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

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_mtd_init;
	vfuncs->startup = fu_plugin_mtd_startup;
}
