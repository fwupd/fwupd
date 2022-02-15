/*
 * Copyright (C) 2017 Christian J. Kellner <christian@kellner.me>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-thunderbolt-controller.h"
#include "fu-thunderbolt-firmware-update.h"
#include "fu-thunderbolt-firmware.h"
#include "fu-thunderbolt-retimer.h"

/*5 seconds sleep until retimer is available                                       \
				     after nvm update*/
#define FU_THUNDERBOLT_RETIMER_CLEANUP_DELAY 5000000

static gboolean
fu_plugin_thunderbolt_safe_kernel(FuPlugin *plugin, GError **error)
{
	g_autofree gchar *minimum_kernel = NULL;

	minimum_kernel = fu_plugin_get_config_value(plugin, "MinimumKernelVersion");
	if (minimum_kernel == NULL) {
		g_debug("Ignoring kernel safety checks");
		return TRUE;
	}
	return fu_common_check_kernel_version(minimum_kernel, error);
}

static gboolean
fu_plugin_thunderbolt_device_created(FuPlugin *plugin, FuDevice *dev, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_plugin_add_rule(plugin,
			   FU_PLUGIN_RULE_INHIBITS_IDLE,
			   "thunderbolt requires device wakeup");
	fu_device_set_context(dev, ctx);
	return TRUE;
}

static void
fu_plugin_thunderbolt_device_registered(FuPlugin *plugin, FuDevice *device)
{
	if (g_strcmp0(fu_device_get_plugin(device), "thunderbolt") != 0)
		return;

	/* Operating system will handle finishing updates later */
	if (fu_plugin_get_config_value_boolean(plugin, "DelayedActivation") &&
	    !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE)) {
		g_debug("Turning on delayed activation for %s", fu_device_get_name(device));
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_SKIPS_RESTART);
		fu_device_remove_internal_flag(device, FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID);
	}
}

static void
fu_plugin_thunderbolt_init(FuPlugin *plugin)
{
	fu_plugin_add_udev_subsystem(plugin, "thunderbolt");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_THUNDERBOLT_CONTROLLER);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_THUNDERBOLT_RETIMER);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_THUNDERBOLT_FIRMWARE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_THUNDERBOLT_FIRMWARE_UPDATE);
}

static gboolean
fu_plugin_thunderbolt_startup(FuPlugin *plugin, GError **error)
{
	return fu_plugin_thunderbolt_safe_kernel(plugin, error);
}

static gboolean
fu_plugin_thunderbolt_composite_prepare(FuPlugin *plugin, GPtrArray *devices, GError **error)
{
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *dev = g_ptr_array_index(devices, i);
		if ((g_strcmp0(fu_device_get_plugin(dev), "thunderbolt") == 0) &&
		    fu_device_has_internal_flag(dev, FU_DEVICE_INTERNAL_FLAG_NO_AUTO_REMOVE)) {
			return fu_thunderbolt_retimer_set_parent_port_offline(dev, error);
		}
	}
	return TRUE;
}

static gboolean
fu_plugin_thunderbolt_composite_cleanup(FuPlugin *plugin, GPtrArray *devices, GError **error)
{
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *dev = g_ptr_array_index(devices, i);
		if ((g_strcmp0(fu_device_get_plugin(dev), "thunderbolt") == 0) &&
		    fu_device_has_internal_flag(dev, FU_DEVICE_INTERNAL_FLAG_NO_AUTO_REMOVE)) {
			g_usleep(FU_THUNDERBOLT_RETIMER_CLEANUP_DELAY);
			return fu_thunderbolt_retimer_set_parent_port_online(dev, error);
		}
	}
	return TRUE;
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_thunderbolt_init;
	vfuncs->startup = fu_plugin_thunderbolt_startup;
	vfuncs->device_registered = fu_plugin_thunderbolt_device_registered;
	vfuncs->device_created = fu_plugin_thunderbolt_device_created;
	vfuncs->composite_prepare = fu_plugin_thunderbolt_composite_prepare;
	vfuncs->composite_cleanup = fu_plugin_thunderbolt_composite_cleanup;
}
