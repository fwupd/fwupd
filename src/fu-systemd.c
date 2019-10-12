/*
 * Copyright (C) 2017-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include <config.h>

#include <stdio.h>
#include <glib/gi18n.h>
#include <gusb.h>

#include "fu-systemd.h"

#define SYSTEMD_SERVICE			"org.freedesktop.systemd1"
#define SYSTEMD_OBJECT_PATH		"/org/freedesktop/systemd1"
#define SYSTEMD_INTERFACE		"org.freedesktop.systemd1"
#define SYSTEMD_MANAGER_INTERFACE	"org.freedesktop.systemd1.Manager"
#define SYSTEMD_UNIT_INTERFACE		"org.freedesktop.systemd1.Unit"

static GDBusProxy *
fu_systemd_get_manager (GError **error)
{
	g_autoptr(GDBusConnection) connection = NULL;
	g_autoptr(GDBusProxy) proxy = NULL;

	connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, error);
	if (connection == NULL) {
		g_prefix_error (error, "failed to get bus: ");
		return NULL;
	}
	proxy = g_dbus_proxy_new_sync (connection,
				       G_DBUS_PROXY_FLAGS_NONE, NULL,
				       SYSTEMD_SERVICE,
				       SYSTEMD_OBJECT_PATH,
				       SYSTEMD_MANAGER_INTERFACE,
				       NULL, error);
	if (proxy == NULL) {
		g_prefix_error (error, "failed to find %s: ", SYSTEMD_SERVICE);
		return NULL;
	}
	return g_steal_pointer (&proxy);
}

static gchar *
fu_systemd_unit_get_path (GDBusProxy *proxy_manager, const gchar *unit, GError **error)
{
	g_autofree gchar *path = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_sync (proxy_manager,
				      "GetUnit",
				      g_variant_new ("(s)", unit),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1, NULL, error);
	if (val == NULL) {
		g_prefix_error (error, "failed to find %s: ", unit);
		return NULL;
	}
	g_variant_get (val, "(o)", &path);
	return g_steal_pointer (&path);
}

static GDBusProxy *
fu_systemd_unit_get_proxy (GDBusProxy *proxy_manager, const gchar *unit, GError **error)
{
	g_autofree gchar *path = NULL;
	g_autoptr(GDBusProxy) proxy_unit = NULL;

	path = fu_systemd_unit_get_path (proxy_manager, unit, error);
	if (path == NULL)
		return NULL;
	proxy_unit = g_dbus_proxy_new_sync (g_dbus_proxy_get_connection (proxy_manager),
					    G_DBUS_PROXY_FLAGS_NONE, NULL,
					    SYSTEMD_SERVICE,
					    path,
					    SYSTEMD_UNIT_INTERFACE,
					    NULL, error);
	if (proxy_unit == NULL) {
		g_prefix_error (error, "failed to register proxy for %s: ", path);
		return NULL;
	}
	return g_steal_pointer (&proxy_unit);
}

gchar *
fu_systemd_get_default_target (GError **error)
{
	const gchar *path = NULL;
	g_autoptr(GDBusProxy) proxy_manager = NULL;
	g_autoptr(GVariant) val = NULL;

	proxy_manager = fu_systemd_get_manager (error);
	if (proxy_manager == NULL)
		return NULL;
	val = g_dbus_proxy_call_sync (proxy_manager,
				      "GetDefaultTarget", NULL,
				      G_DBUS_CALL_FLAGS_NONE,
				      -1, NULL, error);
	if (val == NULL)
		return NULL;
	g_variant_get (val, "(&s)", &path);
	return g_strdup (path);

}

gboolean
fu_systemd_unit_stop (const gchar *unit, GError **error)
{
	g_autoptr(GDBusProxy) proxy_manager = NULL;
	g_autoptr(GDBusProxy) proxy_unit = NULL;
	g_autoptr(GVariant) val = NULL;

	g_return_val_if_fail (unit != NULL, FALSE);

	proxy_manager = fu_systemd_get_manager (error);
	if (proxy_manager == NULL)
		return FALSE;
	proxy_unit = fu_systemd_unit_get_proxy (proxy_manager, unit, error);
	if (proxy_unit == NULL)
		return FALSE;
	val = g_dbus_proxy_call_sync (proxy_unit,
				      "Stop",
				      g_variant_new ("(s)", "replace"),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1, NULL, error);
	return val != NULL;
}

gboolean
fu_systemd_unit_enable (const gchar *unit, GError **error)
{
	const gchar *units[] = { unit, NULL };
	g_autoptr(GDBusProxy) proxy_manager = NULL;
	g_autoptr(GVariant) val = NULL;

	g_return_val_if_fail (unit != NULL, FALSE);

	proxy_manager = fu_systemd_get_manager (error);
	if (proxy_manager == NULL)
		return FALSE;
	val = g_dbus_proxy_call_sync (proxy_manager,
				      "EnableUnitFiles",
				      g_variant_new ("(^asbb)", units, TRUE, TRUE),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1, NULL, error);
	return val != NULL;
}

gboolean
fu_systemd_unit_disable (const gchar *unit, GError **error)
{
	const gchar *units[] = { unit, NULL };
	g_autoptr(GDBusProxy) proxy_manager = NULL;
	g_autoptr(GVariant) val = NULL;

	g_return_val_if_fail (unit != NULL, FALSE);

	proxy_manager = fu_systemd_get_manager (error);
	if (proxy_manager == NULL)
		return FALSE;
	val = g_dbus_proxy_call_sync (proxy_manager,
				      "DisableUnitFiles",
				      g_variant_new ("(^asb)", units, TRUE),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1, NULL, error);
	return val != NULL;
}

gboolean
fu_systemd_unit_check_exists (const gchar *unit, GError **error)
{
	g_autoptr(GDBusProxy) proxy_manager = NULL;
	g_autofree gchar *path = NULL;

	g_return_val_if_fail (unit != NULL, FALSE);

	proxy_manager = fu_systemd_get_manager (error);
	if (proxy_manager == NULL)
		return FALSE;
	path = fu_systemd_unit_get_path (proxy_manager, unit, error);
	return path != NULL;
}
