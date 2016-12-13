/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"

struct FuPluginData {
	GDBusProxy		*proxy;
};

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
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
	data->proxy =
		g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
					       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
					       NULL,
					       "org.freedesktop.UPower",
					       "/org/freedesktop/UPower",
					       "org.freedesktop.UPower",
					       NULL,
					       error);
	if (data->proxy == NULL) {
		g_prefix_error (error, "failed to conect to upower: ");
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_plugin_update_prepare (FuPlugin *plugin,
			  FuDevice *device,
			  GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_autoptr(GVariant) value = NULL;

	/* can we only do this on AC power */
	if (!fu_device_has_flag (device, FWUPD_DEVICE_FLAG_REQUIRE_AC))
		return TRUE;
	value = g_dbus_proxy_get_cached_property (data->proxy, "OnBattery");
	if (value == NULL) {
		g_warning ("failed to get OnBattery value, assume on AC power");
		return TRUE;
	}
	if (g_variant_get_boolean (value)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_AC_POWER_REQUIRED,
				     "Cannot install update "
				     "when not on AC power");
		return FALSE;
	}
	return TRUE;
}
