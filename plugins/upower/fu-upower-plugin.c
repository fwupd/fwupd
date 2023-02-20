/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-upower-plugin.h"

struct _FuUpowerPlugin {
	FuPlugin parent_instance;
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

G_DEFINE_TYPE(FuUpowerPlugin, fu_upower_plugin, FU_TYPE_PLUGIN)

static void
fu_upower_plugin_rescan_devices(FuPlugin *plugin)
{
	FuUpowerPlugin *self = FU_UPOWER_PLUGIN(plugin);
	FuContext *ctx = fu_plugin_get_context(plugin);
	g_autoptr(GVariant) percentage_val = NULL;
	g_autoptr(GVariant) type_val = NULL;
	g_autoptr(GVariant) state_val = NULL;

	/* check that we "have" a battery */
	type_val = g_dbus_proxy_get_cached_property(self->proxy, "Type");
	if (type_val == NULL || g_variant_get_uint32(type_val) == 0) {
		fu_context_set_power_state(ctx, FU_POWER_STATE_UNKNOWN);
		fu_context_set_battery_level(ctx, FWUPD_BATTERY_LEVEL_INVALID);
		return;
	}
	state_val = g_dbus_proxy_get_cached_property(self->proxy, "State");
	if (state_val == NULL || g_variant_get_uint32(state_val) == 0) {
		g_warning("failed to query power state");
		fu_context_set_power_state(ctx, FU_POWER_STATE_UNKNOWN);
		fu_context_set_battery_level(ctx, FWUPD_BATTERY_LEVEL_INVALID);
		return;
	}

	/* map from UpDeviceState to FuPowerState */
	switch (g_variant_get_uint32(state_val)) {
	case UP_DEVICE_STATE_CHARGING:
	case UP_DEVICE_STATE_PENDING_CHARGE:
		fu_context_set_power_state(ctx, FU_POWER_STATE_AC_CHARGING);
		break;
	case UP_DEVICE_STATE_DISCHARGING:
	case UP_DEVICE_STATE_PENDING_DISCHARGE:
		fu_context_set_power_state(ctx, FU_POWER_STATE_BATTERY_DISCHARGING);
		break;
	case UP_DEVICE_STATE_EMPTY:
		fu_context_set_power_state(ctx, FU_POWER_STATE_BATTERY_EMPTY);
		break;
	case UP_DEVICE_STATE_FULLY_CHARGED:
		fu_context_set_power_state(ctx, FU_POWER_STATE_AC_FULLY_CHARGED);
		break;
	default:
		fu_context_set_power_state(ctx, FU_POWER_STATE_UNKNOWN);
		break;
	}

	/* get percentage */
	percentage_val = g_dbus_proxy_get_cached_property(self->proxy, "Percentage");
	if (percentage_val == NULL) {
		g_warning("failed to query power percentage level");
		fu_context_set_battery_level(ctx, FWUPD_BATTERY_LEVEL_INVALID);
		return;
	}
	fu_context_set_battery_level(ctx, g_variant_get_double(percentage_val));
}

static void
fu_upower_plugin_rescan_manager(FuPlugin *plugin)
{
	FuUpowerPlugin *self = FU_UPOWER_PLUGIN(plugin);
	FuContext *ctx = fu_plugin_get_context(plugin);
	g_autoptr(GVariant) lid_is_closed = NULL;
	g_autoptr(GVariant) lid_is_present = NULL;

	/* check that we "have" a lid */
	lid_is_present = g_dbus_proxy_get_cached_property(self->proxy_manager, "LidIsPresent");
	lid_is_closed = g_dbus_proxy_get_cached_property(self->proxy_manager, "LidIsClosed");
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
fu_upower_plugin_proxy_changed_cb(GDBusProxy *proxy,
				  GVariant *changed_properties,
				  GStrv invalidated_properties,
				  FuPlugin *plugin)
{
	fu_upower_plugin_rescan_manager(plugin);
	fu_upower_plugin_rescan_devices(plugin);
}

static gboolean
fu_upower_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuUpowerPlugin *self = FU_UPOWER_PLUGIN(plugin);
	g_autofree gchar *name_owner = NULL;

	self->proxy_manager = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
							    G_DBUS_PROXY_FLAGS_NONE,
							    NULL,
							    "org.freedesktop.UPower",
							    "/org/freedesktop/UPower",
							    "org.freedesktop.UPower",
							    NULL,
							    error);
	if (self->proxy_manager == NULL) {
		g_prefix_error(error, "failed to connect to upower: ");
		return FALSE;
	}
	self->proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
						    G_DBUS_PROXY_FLAGS_NONE,
						    NULL,
						    "org.freedesktop.UPower",
						    "/org/freedesktop/UPower/devices/DisplayDevice",
						    "org.freedesktop.UPower.Device",
						    NULL,
						    error);
	if (self->proxy == NULL) {
		g_prefix_error(error, "failed to connect to upower: ");
		return FALSE;
	}
	name_owner = g_dbus_proxy_get_name_owner(self->proxy);
	if (name_owner == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no owner for %s",
			    g_dbus_proxy_get_name(self->proxy));
		return FALSE;
	}
	g_signal_connect(G_DBUS_PROXY(self->proxy),
			 "g-properties-changed",
			 G_CALLBACK(fu_upower_plugin_proxy_changed_cb),
			 plugin);
	g_signal_connect(G_DBUS_PROXY(self->proxy_manager),
			 "g-properties-changed",
			 G_CALLBACK(fu_upower_plugin_proxy_changed_cb),
			 plugin);

	fu_upower_plugin_rescan_devices(plugin);
	fu_upower_plugin_rescan_manager(plugin);

	/* success */
	return TRUE;
}

static void
fu_upower_plugin_init(FuUpowerPlugin *self)
{
}

static void
fu_upower_finalize(GObject *obj)
{
	FuUpowerPlugin *self = FU_UPOWER_PLUGIN(obj);
	if (self->proxy != NULL)
		g_object_unref(self->proxy);
	if (self->proxy_manager != NULL)
		g_object_unref(self->proxy_manager);
	G_OBJECT_CLASS(fu_upower_plugin_parent_class)->finalize(obj);
}

static void
fu_upower_plugin_class_init(FuUpowerPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_upower_finalize;
	plugin_class->startup = fu_upower_plugin_startup;
}
