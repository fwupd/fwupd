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
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	if (data->proxy != NULL)
		g_object_unref (data->proxy);
}

static void
fu_plugin_upower_rescan (FuPlugin *plugin)
{
	FuContext *ctx = fu_plugin_get_context (plugin);
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_autoptr(GVariant) percentage_val = NULL;
	g_autoptr(GVariant) type_val = NULL;
	g_autoptr(GVariant) state_val = NULL;

	/* check that we "have" a battery */
	type_val = g_dbus_proxy_get_cached_property (data->proxy, "Type");
	if (type_val == NULL || g_variant_get_uint32 (type_val) == 0) {
		g_warning ("failed to query power type");
		fu_context_set_battery_state (ctx, FU_BATTERY_STATE_UNKNOWN);
		fu_context_set_battery_level (ctx, FU_BATTERY_VALUE_INVALID);
		return;
	}
	state_val = g_dbus_proxy_get_cached_property (data->proxy, "State");
	if (state_val == NULL || g_variant_get_uint32 (state_val) == 0) {
		g_warning ("failed to query power state");
		fu_context_set_battery_state (ctx, FU_BATTERY_STATE_UNKNOWN);
		fu_context_set_battery_level (ctx, FU_BATTERY_VALUE_INVALID);
		return;
	}
	fu_context_set_battery_state (ctx, g_variant_get_uint32 (state_val));

	/* get percentage */
	percentage_val = g_dbus_proxy_get_cached_property (data->proxy, "Percentage");
	if (percentage_val == NULL) {
		g_warning ("failed to query power percentage level");
		fu_context_set_battery_level (ctx, FU_BATTERY_VALUE_INVALID);
		return;
	}
	fu_context_set_battery_level (ctx, g_variant_get_double (percentage_val));
}

static void
fu_plugin_upower_proxy_changed_cb (GDBusProxy *proxy,
				   GVariant *changed_properties,
				   GStrv invalidated_properties,
				   FuPlugin *plugin)
{
	fu_plugin_upower_rescan (plugin);
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	FuContext *ctx = fu_plugin_get_context (plugin);
	FuPluginData *data = fu_plugin_get_data (plugin);
	guint64 minimum_battery;
	g_autofree gchar *name_owner = NULL;
	g_autofree gchar *battery_str = NULL;

	data->proxy =
		g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
					       G_DBUS_PROXY_FLAGS_NONE,
					       NULL,
					       "org.freedesktop.UPower",
					       "/org/freedesktop/UPower/devices/DisplayDevice",
					       "org.freedesktop.UPower.Device",
					       NULL,
					       error);
	if (data->proxy == NULL) {
		g_prefix_error (error, "failed to connect to upower: ");
		return FALSE;
	}
	name_owner = g_dbus_proxy_get_name_owner (data->proxy);
	if (name_owner == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "no owner for %s",
			     g_dbus_proxy_get_name (data->proxy));
		return FALSE;
	}
	g_signal_connect (data->proxy, "g-properties-changed",
			  G_CALLBACK (fu_plugin_upower_proxy_changed_cb), plugin);

	battery_str = fu_plugin_get_config_value (plugin, "BatteryThreshold");
	if (battery_str == NULL) {
		const gchar *vendor = fu_context_get_hwid_replace_value (ctx,
									 FU_HWIDS_KEY_MANUFACTURER,
									 NULL);
		if (vendor != NULL) {
			battery_str = g_strdup (fu_context_lookup_quirk_by_id (ctx,
									       vendor,
									       FU_QUIRKS_BATTERY_THRESHOLD));
		}
	}
	if (battery_str == NULL)
		minimum_battery = MINIMUM_BATTERY_PERCENTAGE_FALLBACK;
	else
		minimum_battery = fu_common_strtoull (battery_str);
	if (minimum_battery > 100) {
		g_warning ("invalid minimum battery level specified: %" G_GUINT64_FORMAT,
			   minimum_battery);
		minimum_battery = MINIMUM_BATTERY_PERCENTAGE_FALLBACK;
	}
	fu_context_set_battery_threshold (ctx, minimum_battery);
	fu_plugin_upower_rescan (plugin);

	/* success */
	return TRUE;
}

gboolean
fu_plugin_update_prepare (FuPlugin *plugin,
			  FwupdInstallFlags flags,
			  FuDevice *device,
			  GError **error)
{
	FuContext *ctx = fu_plugin_get_context (plugin);

	/* not all devices need this */
	if (!fu_device_has_flag (device, FWUPD_DEVICE_FLAG_REQUIRE_AC))
		return TRUE;
	if (flags & FWUPD_INSTALL_FLAG_IGNORE_POWER)
		return TRUE;

	/* not charging */
	if (fu_context_get_battery_state (ctx) == FU_BATTERY_STATE_DISCHARGING ||
	    fu_context_get_battery_state (ctx) == FU_BATTERY_STATE_EMPTY) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_AC_POWER_REQUIRED,
				     "Cannot install update "
				     "when not on AC power unless forced");
		return FALSE;
	}

	/* not enough just in case */
	if (fu_context_get_battery_level (ctx) < fu_context_get_battery_threshold (ctx)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_BATTERY_LEVEL_TOO_LOW,
			     "Cannot install update when system battery "
			     "is not at least %u%% unless forced",
			      fu_context_get_battery_threshold (ctx));
		return FALSE;
	}

	/* success */
	return TRUE;
}
