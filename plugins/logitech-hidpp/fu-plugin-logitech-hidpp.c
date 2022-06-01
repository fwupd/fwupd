/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-logitech-hidpp-bootloader-nordic.h"
#include "fu-logitech-hidpp-bootloader-texas.h"
#include "fu-logitech-hidpp-common.h"
#include "fu-logitech-hidpp-device.h"
#include "fu-logitech-hidpp-runtime-bolt.h"
#include "fu-logitech-hidpp-runtime-unifying.h"

static gboolean
fu_plugin_logitech_hidpp_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	/* check the kernel has CONFIG_HIDRAW */
	if (!g_file_test("/sys/class/hidraw", G_FILE_TEST_IS_DIR)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no kernel support for CONFIG_HIDRAW");
		return FALSE;
	}
	return TRUE;
}

static void
fu_plugin_logitech_hidpp_init(FuPlugin *plugin)
{
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_CONFLICTS, "unifying");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_UNIFYING_BOOTLOADER_NORDIC);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_UNIFYING_BOOTLOADER_TEXAS);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_HIDPP_RUNTIME_UNIFYING);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_HIDPP_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_HIDPP_RUNTIME_BOLT);
}

static void
fu_plugin_logitech_hidpp_load(FuContext *ctx)
{
	fu_context_add_quirk_key(ctx, "LogitechHidppModelId");
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->load = fu_plugin_logitech_hidpp_load;
	vfuncs->init = fu_plugin_logitech_hidpp_init;
	vfuncs->startup = fu_plugin_logitech_hidpp_startup;
}
