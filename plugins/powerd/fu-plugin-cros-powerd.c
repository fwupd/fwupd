/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#define MINIMUM_BATTERY_PERCENTAGE_FALLBACK	10

struct FuPluginData {
	GDBusProxy		*proxy;		/* nullable */
};

void
fu_plugin_init (FuPlugin *plugin)
{
	//fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	//fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
        
        return;
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	if (data->proxy != NULL)
		g_object_unref (data->proxy);
}

static void
fu_plugin_powerd_rescan (FuPlugin *plugin)
{
	FuContext *ctx = fu_plugin_get_context (plugin);
	FuPluginData *data = fu_plugin_get_data (plugin);
	
      
	state_val = g_dbus_proxy_get_cached_property (data->proxy, "State");
	
	fu_context_set_battery_state (ctx, g_variant_get_uint32 (state_val));

	/* get percentage */
	percentage_val = g_dbus_proxy_get_cached_property (data->proxy, "Percentage");
    
	fu_context_set_battery_level (ctx, g_variant_get_double (percentage_val));
}

static void
fu_plugin_upower_proxy_changed_cb (GDBusProxy *proxy,
				   GVariant *changed_properties,
				   GStrv invalidated_properties,
				   FuPlugin *plugin)
{
	//fu_plugin_upower_rescan (plugin);
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	
	g_autofree gchar *name_owner = NULL;
	g_autofree gchar *battery_str = NULL;

	minimum_battery = MINIMUM_BATTERY_PERCENTAGE_FALLBACK;
	
	
        /* success */
	return TRUE;
}

gboolean
fu_plugin_update_prepare (FuPlugin *plugin,
			  FwupdInstallFlags flags,
			  FuDevice *device,
			  GError **error)
{

	return TRUE;
}
