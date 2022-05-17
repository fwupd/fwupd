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

static void
fu_plugin_powerd_init(FuPlugin *plugin)
{
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

static void
fu_plugin_powerd_destroy(FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data(plugin);
	if (data->proxy != NULL)
		g_object_unref(data->proxy);
}

static void
fu_plugin_powerd_rescan(FuPlugin *plugin, GVariant *parameters)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	guint32 power_type;
	guint32 current_state;
	gdouble current_level;

	g_variant_get(parameters, "(uud)", &power_type, &current_state, &current_level);

	/* checking if percentage is invalid */
	if (current_level < 1 || current_level > 100)
		current_level = FWUPD_BATTERY_LEVEL_INVALID;

	fu_context_set_battery_state(ctx, current_state);
	fu_context_set_battery_level(ctx, current_level);
}

static void
fu_plugin_powerd_proxy_changed_cb(GDBusProxy *proxy,
				  const gchar *sender_name,
				  const gchar *signal_name,
				  GVariant *parameters,
				  FuPlugin *plugin)
{
	if (!g_str_equal(signal_name, "BatteryStatePoll"))
		return;
	fu_plugin_powerd_rescan(plugin, parameters);
}

static gboolean
fu_plugin_powerd_startup(FuPlugin *plugin, GError **error)
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
		g_prefix_error(error, "failed to connect to powerd: ");
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

	fu_plugin_powerd_rescan(plugin,
				g_dbus_proxy_call_sync(data->proxy,
						       "GetBatteryState",
						       NULL,
						       G_DBUS_CALL_FLAGS_NONE,
						       -1,
						       NULL,
						       G_SOURCE_REMOVE));

	g_signal_connect(G_DBUS_PROXY(data->proxy),
			 "g-signal",
			 G_CALLBACK(fu_plugin_powerd_proxy_changed_cb),
			 plugin);

	return TRUE;
}

static gboolean
fu_plugin_powerd_prepare(FuPlugin *plugin,
			 FuDevice *device,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error)
{
	return fu_plugin_powerd_create_suspend_file(error);
}

static gboolean
fu_plugin_powerd_cleanup(FuPlugin *plugin,
			 FuDevice *device,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error)
{
	return fu_plugin_powerd_delete_suspend_file(error);
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_powerd_init;
	vfuncs->destroy = fu_plugin_powerd_destroy;
	vfuncs->startup = fu_plugin_powerd_startup;
	vfuncs->cleanup = fu_plugin_powerd_cleanup;
	vfuncs->prepare = fu_plugin_powerd_prepare;
}
