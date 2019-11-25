/*
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"

#include "fu-logitech-hidpp-bootloader-nordic.h"
#include "fu-logitech-hidpp-bootloader-texas.h"
#include "fu-logitech-hidpp-common.h"
#include "fu-logitech-hidpp-peripheral.h"
#include "fu-logitech-hidpp-runtime.h"

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
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_udev_subsystem (plugin, "hidraw");
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_CONFLICTS, "unifying");

	/* register the custom types */
	g_type_ensure (FU_TYPE_UNIFYING_BOOTLOADER_NORDIC);
	g_type_ensure (FU_TYPE_UNIFYING_BOOTLOADER_TEXAS);
	g_type_ensure (FU_TYPE_UNIFYING_PERIPHERAL);
	g_type_ensure (FU_TYPE_UNIFYING_RUNTIME);
}
