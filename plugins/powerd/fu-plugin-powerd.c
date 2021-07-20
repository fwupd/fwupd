/*
 * Copyright (C) 2021 Twain Byrnes <binarynewts@google.com>
 * Copyright (C) 2021 George Popoola <gpopoola@google.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#define MINIMUM_BATTERY_PERCENTAGE_FALLBACK 10

struct FuPluginData {
	GDBusProxy	*proxy; /* nullable */
};

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_alloc_data (plugin, sizeof(FuPluginData));
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	if (data->proxy != NULL)
		g_object_unref (data->proxy);
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_autofree gchar *name_owner = NULL;

	/* establish proxy for method call to powerd */
	data->proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
						     G_DBUS_PROXY_FLAGS_NONE,
						     NULL,
						     "org.chromium.PowerManager",
						     "/org/chromium/PowerManager",
						     "org.chromium.PowerManager",
						     NULL,
						     error);

	if (data->proxy == NULL) {
		g_prefix_error (error, "failed to establish proxy: ");
		return FALSE;
	}
	name_owner = g_dbus_proxy_get_name_owner (data->proxy);
	if (name_owner == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "no service that owns the name for %s",
			     g_dbus_proxy_get_name (data->proxy));
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_plugin_update_prepare (FuPlugin *plugin,
			  FwupdInstallFlags flags,
			  FuDevice *device,
			  GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	guint32 power_type = 0;
	guint32 current_state = 0;
	gdouble current_level = 0;
	g_autoptr(GVariant) powerd_response = NULL;

	/* making method call to "GetBatteryState" through the proxy */
	powerd_response = g_dbus_proxy_call_sync (data->proxy,
						  "GetBatteryState",
						  NULL,
						  G_DBUS_CALL_FLAGS_NONE,
						  -1,
						  NULL,
						  error);
	if (powerd_response == NULL) {
		g_prefix_error (error, "battery information was not loaded: ");
		return FALSE;
	}

	/* parse and use for battery-check conditions */
	g_variant_get (powerd_response,
		       "(uud)",
		       &power_type,
		       &current_state,
		       &current_level);

	/* blocking updates if there is no AC power or if battery
	 * percentage is too low */
	if (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_REQUIRE_AC) &&
	    current_state == FU_BATTERY_STATE_DISCHARGING) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_AC_POWER_REQUIRED,
			     "Cannot install update without external power "
			     "unless forced ");
		return FALSE;
	}
	if (current_level < MINIMUM_BATTERY_PERCENTAGE_FALLBACK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_BATTERY_LEVEL_TOO_LOW,
			     "Cannot install update when system battery "
			     "is not at least %i%% unless forced",
			     MINIMUM_BATTERY_PERCENTAGE_FALLBACK);
		return FALSE;
	}
	return TRUE;
}
