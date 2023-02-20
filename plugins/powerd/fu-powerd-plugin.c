/*
 * Copyright (C) 2021 Twain Byrnes <binarynewts@google.com>
 * Copyright (C) 2021 George Popoola <gpopoola@google.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-powerd-plugin.h"

struct _FuPowerdPlugin {
	FuPlugin parent_instance;
	GDBusProxy *proxy; /* nullable */
};

G_DEFINE_TYPE(FuPowerdPlugin, fu_powerd_plugin, FU_TYPE_PLUGIN)

typedef enum {
	FU_POWERD_BATTERY_STATE_UNKNOWN,
	FU_POWERD_BATTERY_STATE_CHARGING,
	FU_POWERD_BATTERY_STATE_DISCHARGING,
	FU_POWERD_BATTERY_STATE_EMPTY,
	FU_POWERD_BATTERY_STATE_FULLY_CHARGED,
} FuPowerdBatteryState;

static gboolean
fu_powerd_plugin_create_suspend_file(GError **error)
{
	g_autofree gchar *lockdir = NULL;
	g_autofree gchar *inhibitsuspend_filename = NULL;
	g_autofree gchar *getpid_str = NULL;

	lockdir = fu_path_from_kind(FU_PATH_KIND_LOCKDIR);
	inhibitsuspend_filename = g_build_filename(lockdir, "power_override", "fwupd.lock", NULL);
	getpid_str = g_strdup_printf("%d", getpid());
	if (!g_file_set_contents(inhibitsuspend_filename, getpid_str, -1, error)) {
		g_prefix_error(error, "lock file unable to be created: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_powerd_plugin_delete_suspend_file(GError **error)
{
	g_autoptr(GError) local_error = NULL;
	g_autofree gchar *lockdir = NULL;
	g_autoptr(GFile) inhibitsuspend_file = NULL;

	lockdir = fu_path_from_kind(FU_PATH_KIND_LOCKDIR);
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
fu_powerd_plugin_rescan(FuPlugin *plugin, GVariant *parameters)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	guint32 power_type;
	guint32 current_state;
	gdouble current_level;

	g_variant_get(parameters, "(uud)", &power_type, &current_state, &current_level);

	/* checking if percentage is invalid */
	if (current_level < 1 || current_level > 100)
		current_level = FWUPD_BATTERY_LEVEL_INVALID;
	fu_context_set_battery_level(ctx, current_level);

	switch (current_state) {
	case FU_POWERD_BATTERY_STATE_CHARGING:
		fu_context_set_power_state(ctx, FU_POWER_STATE_AC_CHARGING);
		break;
	case FU_POWERD_BATTERY_STATE_DISCHARGING:
		fu_context_set_power_state(ctx, FU_POWER_STATE_BATTERY_DISCHARGING);
		break;
	case FU_POWERD_BATTERY_STATE_EMPTY:
		fu_context_set_power_state(ctx, FU_POWER_STATE_BATTERY_EMPTY);
		break;
	case FU_POWERD_BATTERY_STATE_FULLY_CHARGED:
		fu_context_set_power_state(ctx, FU_POWER_STATE_AC_FULLY_CHARGED);
		break;
	default:
		fu_context_set_power_state(ctx, FU_POWER_STATE_UNKNOWN);
		break;
	}
}

static void
fu_powerd_plugin_proxy_changed_cb(GDBusProxy *proxy,
				  const gchar *sender_name,
				  const gchar *signal_name,
				  GVariant *parameters,
				  FuPlugin *plugin)
{
	if (!g_str_equal(signal_name, "BatteryStatePoll"))
		return;
	fu_powerd_plugin_rescan(plugin, parameters);
}

static gboolean
fu_powerd_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuPowerdPlugin *self = FU_POWERD_PLUGIN(plugin);
	g_autofree gchar *name_owner = NULL;

	if (!fu_powerd_plugin_delete_suspend_file(error))
		return FALSE;

	/* establish proxy for method call to powerd */
	self->proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
						    G_DBUS_PROXY_FLAGS_NONE,
						    NULL,
						    "org.chromium.PowerManager",
						    "/org/chromium/PowerManager",
						    "org.chromium.PowerManager",
						    NULL,
						    error);

	if (self->proxy == NULL) {
		g_prefix_error(error, "failed to connect to powerd: ");
		return FALSE;
	}
	name_owner = g_dbus_proxy_get_name_owner(self->proxy);
	if (name_owner == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no service that owns the name for %s",
			    g_dbus_proxy_get_name(self->proxy));
		return FALSE;
	}

	fu_powerd_plugin_rescan(plugin,
				g_dbus_proxy_call_sync(self->proxy,
						       "GetBatteryState",
						       NULL,
						       G_DBUS_CALL_FLAGS_NONE,
						       -1,
						       NULL,
						       G_SOURCE_REMOVE));

	g_signal_connect(G_DBUS_PROXY(self->proxy),
			 "g-signal",
			 G_CALLBACK(fu_powerd_plugin_proxy_changed_cb),
			 plugin);

	return TRUE;
}

static gboolean
fu_powerd_plugin_prepare(FuPlugin *plugin,
			 FuDevice *device,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error)
{
	return fu_powerd_plugin_create_suspend_file(error);
}

static gboolean
fu_powerd_plugin_cleanup(FuPlugin *plugin,
			 FuDevice *device,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error)
{
	return fu_powerd_plugin_delete_suspend_file(error);
}

static void
fu_powerd_plugin_init(FuPowerdPlugin *self)
{
}

static void
fu_powerd_finalize(GObject *obj)
{
	FuPowerdPlugin *self = FU_POWERD_PLUGIN(obj);
	if (self->proxy != NULL)
		g_object_unref(self->proxy);
	G_OBJECT_CLASS(fu_powerd_plugin_parent_class)->finalize(obj);
}

static void
fu_powerd_plugin_class_init(FuPowerdPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_powerd_finalize;
	plugin_class->startup = fu_powerd_plugin_startup;
	plugin_class->cleanup = fu_powerd_plugin_cleanup;
	plugin_class->prepare = fu_powerd_plugin_prepare;
}
