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
#include "fu-logitech-hidpp-peripheral.h"
#include "fu-logitech-hidpp-runtime-unifying.h"

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	/* check the kernel has CONFIG_HIDRAW */
	if (!g_file_test ("/sys/class/hidraw", G_FILE_TEST_IS_DIR)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "no kernel support for CONFIG_HIDRAW");
		return FALSE;
	}
	return TRUE;
}

void
fu_plugin_init (FuPlugin *plugin)
{
	FuContext *ctx = fu_plugin_get_context (plugin);
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_context_add_udev_subsystem (ctx, "hidraw");
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_CONFLICTS, "unifying");
	fu_plugin_add_device_gtype (plugin, FU_TYPE_UNIFYING_BOOTLOADER_NORDIC);
	fu_plugin_add_device_gtype (plugin, FU_TYPE_UNIFYING_BOOTLOADER_TEXAS);
	fu_plugin_add_device_gtype (plugin, FU_TYPE_HIDPP_RUNTIME_UNIFYING);
	fu_plugin_add_device_gtype (plugin, FU_TYPE_HIDPP_PERIPHERAL);
}
