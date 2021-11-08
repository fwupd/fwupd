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

static gboolean
fu_plugin_mtd_device_created(FuPlugin *plugin, FuDevice *dev, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	const gchar *vendor;

	fu_device_set_summary(dev, "Memory Technology Device");
	fu_device_add_protocol(dev, "org.infradead.mtd");
	fu_device_add_flag(dev, FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(dev, FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_device_add_icon(dev, "drive-harddisk-solidstate");

	/* set vendor ID as the BIOS vendor */
	vendor = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_MANUFACTURER);
	if (vendor != NULL) {
		g_autofree gchar *vendor_id = g_strdup_printf("DMI:%s", vendor);
		fu_device_add_vendor_id(dev, vendor_id);
	}

	return TRUE;
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_mtd_init;
	vfuncs->startup = fu_plugin_mtd_startup;
	vfuncs->device_created = fu_plugin_mtd_device_created;
}
