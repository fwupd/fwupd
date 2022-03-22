/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

struct FuPluginData {
	GDBusProxy *proxy;	   /* nullable */
	GDBusProxy *proxy_manager; /* nullable */
};

typedef enum {
	UP_DEVICE_STATE_UNKNOWN,
	UP_DEVICE_STATE_CHARGING,
	UP_DEVICE_STATE_DISCHARGING,
	UP_DEVICE_STATE_EMPTY,
	UP_DEVICE_STATE_FULLY_CHARGED,
	UP_DEVICE_STATE_PENDING_CHARGE,
	UP_DEVICE_STATE_PENDING_DISCHARGE,
	UP_DEVICE_STATE_LAST
} UpDeviceState;

static void
fu_plugin_upower_init(FuPlugin *plugin)
{
	fu_plugin_alloc_data(plugin, sizeof(FuPluginData));
}

static void
fu_plugin_upower_destroy(FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data(plugin);
	if (data->proxy != NULL)
		g_object_unref(data->proxy);
	if (data->proxy_manager != NULL)
		g_object_unref(data->proxy_manager);
}

static void
fu_plugin_upower_rescan_devices(FuPlugin *plugin)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	FuPluginData *data = fu_plugin_get_data(plugin);
	g_autoptr(GVariant) percentage_val = NULL;
	g_autoptr(GVariant) type_val = NULL;
	g_autoptr(GVariant) state_val = NULL;

	/* check that we "have" a battery */
	type_val = g_dbus_proxy_get_cached_property(data->proxy, "Type");
	if (type_val == NULL || g_variant_get_uint32(type_val) == 0) {
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

	/* map from UpDeviceState to FuBatteryState */
	switch (g_variant_get_uint32(state_val)) {
	case UP_DEVICE_STATE_CHARGING:
	case UP_DEVICE_STATE_PENDING_CHARGE:
		fu_context_set_battery_state(ctx, FU_BATTERY_STATE_CHARGING);
		break;
	case UP_DEVICE_STATE_DISCHARGING:
	case UP_DEVICE_STATE_PENDING_DISCHARGE:
		fu_context_set_battery_state(ctx, FU_BATTERY_STATE_DISCHARGING);
		break;
	case UP_DEVICE_STATE_EMPTY:
		fu_context_set_battery_state(ctx, FU_BATTERY_STATE_EMPTY);
		break;
	case UP_DEVICE_STATE_FULLY_CHARGED:
		fu_context_set_battery_state(ctx, FU_BATTERY_STATE_FULLY_CHARGED);
		break;
	default:
		fu_context_set_battery_state(ctx, FU_BATTERY_STATE_UNKNOWN);
		break;
	}

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
fu_plugin_upower_rescan_manager(FuPlugin *plugin)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	FuPluginData *data = fu_plugin_get_data(plugin);
	g_autoptr(GVariant) lid_is_closed = NULL;
	g_autoptr(GVariant) lid_is_present = NULL;

	/* check that we "have" a lid */
	lid_is_present = g_dbus_proxy_get_cached_property(data->proxy_manager, "LidIsPresent");
	lid_is_closed = g_dbus_proxy_get_cached_property(data->proxy_manager, "LidIsClosed");
	if (lid_is_present == NULL || lid_is_closed == NULL) {
		g_warning("failed to query lid state");
		fu_context_set_lid_state(ctx, FU_LID_STATE_UNKNOWN);
		return;
	}
	if (!g_variant_get_boolean(lid_is_present)) {
		fu_context_set_lid_state(ctx, FU_LID_STATE_UNKNOWN);
		return;
	}
	if (g_variant_get_boolean(lid_is_closed)) {
		fu_context_set_lid_state(ctx, FU_LID_STATE_CLOSED);
		return;
	}
	fu_context_set_lid_state(ctx, FU_LID_STATE_OPEN);
}

static void
fu_plugin_upower_proxy_changed_cb(GDBusProxy *proxy,
				  GVariant *changed_properties,
				  const GStrv invalidated_properties,
				  FuPlugin *plugin)
{
	fu_plugin_upower_rescan_manager(plugin);
	fu_plugin_upower_rescan_devices(plugin);
}

static gboolean
fu_plugin_upower_startup(FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data(plugin);
	g_autofree gchar *name_owner = NULL;

	data->proxy_manager = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
							    G_DBUS_PROXY_FLAGS_NONE,
							    NULL,
							    "org.freedesktop.UPower",
							    "/org/freedesktop/UPower",
							    "org.freedesktop.UPower",
							    NULL,
							    error);
	if (data->proxy_manager == NULL) {
		g_prefix_error(error, "failed to connect to upower: ");
		return FALSE;
	}
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
	g_signal_connect(G_DBUS_PROXY(data->proxy),
			 "g-properties-changed",
			 G_CALLBACK(fu_plugin_upower_proxy_changed_cb),
			 plugin);
	g_signal_connect(G_DBUS_PROXY(data->proxy_manager),
			 "g-properties-changed",
			 G_CALLBACK(fu_plugin_upower_proxy_changed_cb),
			 plugin);

	fu_plugin_upower_rescan_devices(plugin);
	fu_plugin_upower_rescan_manager(plugin);

	/* success */
	return TRUE;
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_upower_init;
	vfuncs->startup = fu_plugin_upower_startup;
	vfuncs->destroy = fu_plugin_upower_destroy;
}
