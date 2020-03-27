/*
 * Copyright (C) 2017 Christian J. Kellner <christian@kellner.me>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <sys/utsname.h>

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"
#include "fu-thunderbolt-device.h"
#include "fu-thunderbolt-firmware.h"

static gboolean
fu_plugin_thunderbolt_safe_kernel (FuPlugin *plugin, GError **error)
{
	g_autofree gchar *minimum_kernel = NULL;
	struct utsname name_tmp;

	memset (&name_tmp, 0, sizeof(struct utsname));
	if (uname (&name_tmp) < 0) {
		g_debug ("Failed to read current kernel version");
		return TRUE;
	}

	minimum_kernel = fu_plugin_get_config_value (plugin, "MinimumKernelVersion");
	if (minimum_kernel == NULL) {
		g_debug ("Ignoring kernel safety checks");
		return TRUE;
	}

	if (fu_common_vercmp_full (name_tmp.release,
				   minimum_kernel,
				   FWUPD_VERSION_FORMAT_TRIPLET) < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "kernel %s may not have full Thunderbolt support",
			     name_tmp.release);
		return FALSE;
	}
	g_debug ("Using kernel %s (minimum %s)", name_tmp.release, minimum_kernel);

	return TRUE;
}

gboolean
fu_plugin_device_created (FuPlugin *plugin, FuDevice *dev, GError **error)
{
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_INHIBITS_IDLE,
			    "thunderbolt requires device wakeup");
	fu_device_set_quirks (dev, fu_plugin_get_quirks (plugin));
	return TRUE;
}

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_udev_subsystem (plugin, "thunderbolt");
	fu_plugin_set_device_gtype (plugin, FU_TYPE_THUNDERBOLT_DEVICE);
	fu_plugin_add_firmware_gtype (plugin, "thunderbolt", FU_TYPE_THUNDERBOLT_FIRMWARE);
	/* dell-dock plugin uses a slower bus for flashing */
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_BETTER_THAN, "dell_dock");
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	return fu_plugin_thunderbolt_safe_kernel (plugin, error);
}
