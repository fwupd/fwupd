/*
 * Copyright (C) 2021 Twain Byrnes <binarynewts@google.com>
 * Copyright (C) 2021 George Popoola <gpopoola@google.com>
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

static gboolean
fu_plugin_powerd_create_suspend_file(GError **error)
{
	g_autofree gchar *lockdir = NULL;
	g_autofree gchar *inhibitsuspend_filename = NULL;
	g_autofree gchar *getpid_str = NULL;

	lockdir = fu_common_get_path(FU_PATH_KIND_LOCKDIR);
	inhibitsuspend_filename = g_build_filename(lockdir, "power_override", "fwupd.lock", NULL);
	getpid_str = g_strdup_printf("%d", getpid());
	if (!g_file_set_contents(inhibitsuspend_filename, getpid_str, -1, error)) {
		g_prefix_error(error, "lock file unable to be created");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_plugin_powerd_delete_suspend_file(GError **error)
{
	g_autoptr(GError) local_error = NULL;
	g_autofree gchar *lockdir = NULL;
	g_autoptr(GFile) inhibitsuspend_file = NULL;

	lockdir = fu_common_get_path(FU_PATH_KIND_LOCKDIR);
	inhibitsuspend_file =
	    g_file_new_build_filename(lockdir, "power_override", "fwupd.lock", NULL);
	if (!g_file_delete(inhibitsuspend_file, NULL, &local_error) &&
	    !g_error_matches(local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
		g_propagate_prefixed_error(error,
					   g_steal_pointer(&local_error),
					   "lock file unable to be deleted");
		return FALSE;
	}
	return TRUE;
}

void
fu_plugin_destroy(FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data(plugin);
	if (data->proxy != NULL)
		g_object_unref(data->proxy);
}

static gboolean
fu_plugin_powerd_refresh_cb(FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data(plugin);
	FuContext *ctx = fu_plugin_get_context(plugin);
	guint32 power_type;
	guint32 current_state;
	gdouble current_level;
	g_autoptr(GVariant) powerd_response = NULL;

	/* retrieving battery info from "GetBatteryState" method call to powerd */
	powerd_response = g_dbus_proxy_call_sync(data->proxy,
						 "GetBatteryState",
						 NULL,
						 G_DBUS_CALL_FLAGS_NONE,
						 -1,
						 NULL,
						 G_SOURCE_REMOVE);
	if (powerd_response == NULL) {
		g_debug("battery information was not loaded");
		return G_SOURCE_REMOVE;
	}

	/* parse and use for battery-check conditions */
	g_variant_get(powerd_response, "(uud)", &power_type, &current_state, &current_level);

	/* checking if percentage is invalid */
	if (current_level < 1 || current_level > 100)
		current_level = FU_BATTERY_VALUE_INVALID;

	fu_context_set_battery_state(ctx, current_state);
	fu_context_set_battery_level(ctx, current_level);

	return G_SOURCE_CONTINUE;
}

gboolean
fu_plugin_startup(FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data(plugin);
	g_autofree gchar *name_owner = NULL;

	if (!fu_plugin_powerd_delete_suspend_file(error))
		return FALSE;

	/* establish proxy for method call to powerd */
	data->proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
						    G_DBUS_PROXY_FLAGS_NONE,
						    NULL,
						    "org.chromium.PowerManager",
						    "/org/chromium/PowerManager",
						    "org.chromium.PowerManager",
						    NULL,
						    error);

	if (data->proxy == NULL) {
		g_prefix_error(error, "failed to establish proxy: ");
		return FALSE;
	}
	name_owner = g_dbus_proxy_get_name_owner(data->proxy);
	if (name_owner == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no service that owns the name for %s",
			    g_dbus_proxy_get_name(data->proxy));
		return FALSE;
	}

	/* start a timer to repeatedly run fu_plugin_powerd_refresh_cb at a set interval */
	g_timeout_add_seconds(5, G_SOURCE_FUNC(fu_plugin_powerd_refresh_cb), plugin);

	return TRUE;
}

gboolean
fu_plugin_update_prepare(FuPlugin *plugin, FwupdInstallFlags flags, FuDevice *dev, GError **error)
{
	return fu_plugin_powerd_create_suspend_file(error);
}

gboolean
fu_plugin_update_cleanup(FuPlugin *plugin, FwupdInstallFlags flags, FuDevice *dev, GError **error)
{
	return fu_plugin_powerd_delete_suspend_file(error);
}
