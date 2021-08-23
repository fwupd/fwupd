/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

struct FuPluginData {
	GDBusProxy *proxy; /* nullable */
};

void
fu_plugin_init(FuPlugin *plugin)
{
	fu_plugin_set_build_hash(plugin, FU_BUILD_HASH);
	fu_plugin_alloc_data(plugin, sizeof(FuPluginData));
}

void
fu_plugin_destroy(FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data(plugin);
	if (data->proxy != NULL)
		g_object_unref(data->proxy);
}

static void
fu_plugin_upower_rescan(FuPlugin *plugin)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	FuPluginData *data = fu_plugin_get_data(plugin);
	g_autoptr(GVariant) percentage_val = NULL;
	g_autoptr(GVariant) type_val = NULL;
	g_autoptr(GVariant) state_val = NULL;

	/* check that we "have" a battery */
	type_val = g_dbus_proxy_get_cached_property(data->proxy, "Type");
	if (type_val == NULL || g_variant_get_uint32(type_val) == 0) {
		g_warning("failed to query power type");
		fu_context_set_battery_state(ctx, FU_BATTERY_STATE_UNKNOWN);
		fu_context_set_battery_level(ctx, FU_BATTERY_VALUE_INVALID);
		return;
	}
	state_val = g_dbus_proxy_get_cached_property(data->proxy, "State");
	if (state_val == NULL || g_variant_get_uint32(state_val) == 0) {
		g_warning("failed to query power state");
		fu_context_set_battery_state(ctx, FU_BATTERY_STATE_UNKNOWN);
		fu_context_set_battery_level(ctx, FU_BATTERY_VALUE_INVALID);
		return;
	}
	fu_context_set_battery_state(ctx, g_variant_get_uint32(state_val));

	/* get percentage */
	percentage_val = g_dbus_proxy_get_cached_property(data->proxy, "Percentage");
	if (percentage_val == NULL) {
		g_warning("failed to query power percentage level");
		fu_context_set_battery_level(ctx, FU_BATTERY_VALUE_INVALID);
		return;
	}
	fu_context_set_battery_level(ctx, g_variant_get_double(percentage_val));
}

static void
fu_plugin_upower_proxy_changed_cb(GDBusProxy *proxy,
				  GVariant *changed_properties,
				  GStrv invalidated_properties,
				  FuPlugin *plugin)
{
	fu_plugin_upower_rescan(plugin);
}

gboolean
fu_plugin_startup(FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data(plugin);
	g_autofree gchar *name_owner = NULL;

	data->proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
						    G_DBUS_PROXY_FLAGS_NONE,
						    NULL,
						    "org.freedesktop.UPower",
						    "/org/freedesktop/UPower/devices/DisplayDevice",
						    "org.freedesktop.UPower.Device",
						    NULL,
						    error);
	if (data->proxy == NULL) {
		g_prefix_error(error, "failed to connect to upower: ");
		return FALSE;
	}
	name_owner = g_dbus_proxy_get_name_owner(data->proxy);
	if (name_owner == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no owner for %s",
			    g_dbus_proxy_get_name(data->proxy));
		return FALSE;
	}
	g_signal_connect(data->proxy,
			 "g-properties-changed",
			 G_CALLBACK(fu_plugin_upower_proxy_changed_cb),
			 plugin);

	fu_plugin_upower_rescan(plugin);

	/* success */
	return TRUE;
}
