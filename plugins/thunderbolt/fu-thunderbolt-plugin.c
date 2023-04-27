/*
 * Copyright (C) 2017 Christian J. Kellner <christian@kellner.me>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-thunderbolt-common.h"
#include "fu-thunderbolt-controller.h"
#include "fu-thunderbolt-plugin.h"
#include "fu-thunderbolt-retimer.h"

struct _FuThunderboltPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuThunderboltPlugin, fu_thunderbolt_plugin, FU_TYPE_PLUGIN)

/* 5 seconds sleep until retimer is available after nvm update */
#define FU_THUNDERBOLT_RETIMER_CLEANUP_DELAY 5000000

/* defaults changed here will also be reflected in the fwupd.conf man page */
#define FU_THUNDERBOLT_CONFIG_DEFAULT_MINIMUM_KERNEL_VERSION "4.13.0"
#define FU_THUNDERBOLT_CONFIG_DEFAULT_DELAYED_ACTIVATION     FALSE

static gboolean
fu_thunderbolt_plugin_safe_kernel(FuPlugin *plugin, GError **error)
{
	g_autofree gchar *min =
	    fu_plugin_get_config_value(plugin,
				       "MinimumKernelVersion",
				       FU_THUNDERBOLT_CONFIG_DEFAULT_MINIMUM_KERNEL_VERSION);
	return fu_kernel_check_version(min, error);
}

static gboolean
fu_thunderbolt_plugin_device_created(FuPlugin *plugin, FuDevice *dev, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_plugin_add_rule(plugin,
			   FU_PLUGIN_RULE_INHIBITS_IDLE,
			   "thunderbolt requires device wakeup");
	if (fu_context_has_hwid_flag(ctx, "retimer-offline-mode"))
		fu_device_add_private_flag(dev, FU_THUNDERBOLT_DEVICE_FLAG_FORCE_ENUMERATION);
	return TRUE;
}

static void
fu_thunderbolt_plugin_device_registered(FuPlugin *plugin, FuDevice *device)
{
	if (g_strcmp0(fu_device_get_plugin(device), "thunderbolt") != 0)
		return;

	/* Operating system will handle finishing updates later */
	if (fu_plugin_get_config_value_boolean(plugin,
					       "DelayedActivation",
					       FU_THUNDERBOLT_CONFIG_DEFAULT_DELAYED_ACTIVATION) &&
	    !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE)) {
		g_info("turning on delayed activation for %s", fu_device_get_name(device));
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_SKIPS_RESTART);
		fu_device_remove_internal_flag(device, FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID);
	}
}

static gboolean
fu_thunderbolt_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	return fu_thunderbolt_plugin_safe_kernel(plugin, error);
}

static gboolean
fu_thunderbolt_plugin_composite_prepare(FuPlugin *plugin, GPtrArray *devices, GError **error)
{
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *dev = g_ptr_array_index(devices, i);
		if ((g_strcmp0(fu_device_get_plugin(dev), "thunderbolt") == 0) &&
		    fu_device_has_private_flag(dev, FU_THUNDERBOLT_DEVICE_FLAG_FORCE_ENUMERATION) &&
		    fu_device_has_internal_flag(dev, FU_DEVICE_INTERNAL_FLAG_NO_AUTO_REMOVE)) {
			return fu_thunderbolt_retimer_set_parent_port_offline(dev, error);
		}
	}
	return TRUE;
}

static gboolean
fu_thunderbolt_plugin_composite_cleanup(FuPlugin *plugin, GPtrArray *devices, GError **error)
{
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *dev = g_ptr_array_index(devices, i);
		if ((g_strcmp0(fu_device_get_plugin(dev), "thunderbolt") == 0) &&
		    fu_device_has_private_flag(dev, FU_THUNDERBOLT_DEVICE_FLAG_FORCE_ENUMERATION) &&
		    fu_device_has_internal_flag(dev, FU_DEVICE_INTERNAL_FLAG_NO_AUTO_REMOVE)) {
			g_usleep(FU_THUNDERBOLT_RETIMER_CLEANUP_DELAY);
			return fu_thunderbolt_retimer_set_parent_port_online(dev, error);
		}
	}
	return TRUE;
}

static void
fu_thunderbolt_plugin_init(FuThunderboltPlugin *self)
{
}

static void
fu_thunderbolt_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "thunderbolt");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_THUNDERBOLT_CONTROLLER);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_THUNDERBOLT_RETIMER);
}

static void
fu_thunderbolt_plugin_class_init(FuThunderboltPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_thunderbolt_plugin_constructed;
	plugin_class->startup = fu_thunderbolt_plugin_startup;
	plugin_class->device_registered = fu_thunderbolt_plugin_device_registered;
	plugin_class->device_created = fu_thunderbolt_plugin_device_created;
	plugin_class->composite_prepare = fu_thunderbolt_plugin_composite_prepare;
	plugin_class->composite_cleanup = fu_thunderbolt_plugin_composite_cleanup;
}
