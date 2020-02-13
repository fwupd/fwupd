/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"

#define MINIMUM_BATTERY_PERCENTAGE_FALLBACK	10

struct FuPluginData {
	GDBusProxy		*upower_proxy;
	GDBusProxy		*display_proxy;
	guint64			 minimum_battery;
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
	if (data->upower_proxy != NULL)
		g_object_unref (data->upower_proxy);
	if (data->display_proxy != NULL)
		g_object_unref (data->display_proxy);
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_autofree gchar *name_owner = NULL;
	g_autofree gchar *battery_str = NULL;
	data->upower_proxy =
		g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
					       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
					       NULL,
					       "org.freedesktop.UPower",
					       "/org/freedesktop/UPower",
					       "org.freedesktop.UPower",
					       NULL,
					       error);
	if (data->upower_proxy == NULL) {
		g_prefix_error (error, "failed to connect to upower: ");
		return FALSE;
	}
	name_owner = g_dbus_proxy_get_name_owner (data->upower_proxy);
	if (name_owner == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "no owner for %s",
			     g_dbus_proxy_get_name (data->upower_proxy));
		return FALSE;
	}
	data->display_proxy =
		g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
					       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
					       NULL,
					       "org.freedesktop.UPower",
					       "/org/freedesktop/UPower/devices/DisplayDevice",
					       "org.freedesktop.UPower.Device",
					       NULL,
					       error);
	if (data->display_proxy == NULL) {
		g_prefix_error (error, "failed to connect to upower: ");
		return FALSE;
	}

	battery_str = fu_plugin_get_config_value (plugin, "BatteryThreshold");
	if (battery_str == NULL)
		data->minimum_battery = MINIMUM_BATTERY_PERCENTAGE_FALLBACK;
	else
		data->minimum_battery = fu_common_strtoull (battery_str);
	if (data->minimum_battery > 100) {
		g_warning ("Invalid minimum battery level specified: %" G_GUINT64_FORMAT,
			   data->minimum_battery);
		data->minimum_battery = MINIMUM_BATTERY_PERCENTAGE_FALLBACK;
	}

	return TRUE;
}

static gboolean
fu_plugin_upower_check_percentage_level (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	gdouble level;
	guint power_type;
	g_autoptr(GVariant) percentage_val = NULL;
	g_autoptr(GVariant) type_val = NULL;

	/* check that we "have" a battery */
	type_val = g_dbus_proxy_get_cached_property (data->display_proxy, "Type");
	if (type_val == NULL) {
		g_warning ("Failed to query power type, assume AC power");
		return TRUE;
	}
	power_type = g_variant_get_uint32 (type_val);
	if (power_type != 2) {
		g_debug ("Not running on battery (Type: %u)", power_type);
		return TRUE;
	}

	/* check percentage high enough */
	percentage_val = g_dbus_proxy_get_cached_property (data->display_proxy, "Percentage");
	if (percentage_val == NULL) {
		g_warning ("Failed to query power percentage level, assume enough charge");
		return TRUE;
	}
	level = g_variant_get_double (percentage_val);
	g_debug ("System power source is %.1f%%", level);

	return level >= data->minimum_battery;
}

static gboolean
fu_plugin_upower_check_on_battery (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_autoptr(GVariant) value = NULL;

	value = g_dbus_proxy_get_cached_property (data->upower_proxy, "OnBattery");
	if (value == NULL) {
		g_warning ("failed to get OnBattery value, assume on AC power");
		return FALSE;
	}
	return g_variant_get_boolean (value);
}

gboolean
fu_plugin_update_prepare (FuPlugin *plugin,
			  FwupdInstallFlags flags,
			  FuDevice *device,
			  GError **error)
{
	/* not all devices need this */
	if (!fu_device_has_flag (device, FWUPD_DEVICE_FLAG_REQUIRE_AC))
		return TRUE;

	/* determine if operating on AC or battery */
	if (fu_plugin_upower_check_on_battery (plugin) &&
	    (flags & FWUPD_INSTALL_FLAG_FORCE) == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_AC_POWER_REQUIRED,
				     "Cannot install update "
				     "when not on AC power unless forced");
		return FALSE;
	}

	/* deteremine if battery high enough */
	if (!fu_plugin_upower_check_percentage_level (plugin) &&
	   (flags & FWUPD_INSTALL_FLAG_FORCE) == 0) {
		FuPluginData *data = fu_plugin_get_data (plugin);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_BATTERY_LEVEL_TOO_LOW,
			     "Cannot install update when battery "
			     "is not at least %" G_GUINT64_FORMAT "%% unless forced",
			      data->minimum_battery);
		return FALSE;
	}
	return TRUE;
}
