/*
 * Copyright (C) 2019 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <gio/gio.h>

#include "fu-uefi-udisks.h"
#include "fwupd-common.h"
#include "fwupd-error.h"

#define UDISKS_DBUS_SERVICE		"org.freedesktop.UDisks2"
#define UDISKS_DBUS_PATH		"/org/freedesktop/UDisks2/Manager"
#define UDISKS_DBUS_MANAGER_INTERFACE	"org.freedesktop.UDisks2.Manager"
#define UDISKS_DBUS_PART_INTERFACE 	"org.freedesktop.UDisks2.Partition"
#define UDISKS_DBUS_FILE_INTERFACE	"org.freedesktop.UDisks2.Filesystem"
#define ESP_DISK_TYPE			"c12a7328-f81f-11d2-ba4b-00a0c93ec93b"

gboolean
fu_uefi_udisks_objpath (const gchar *path)
{
	return g_str_has_prefix (path, "/org/freedesktop/UDisks2/");
}

static GDBusProxy *
fu_uefi_udisks_get_dbus_proxy (const gchar *path, const gchar *interface,
			       GError **error)
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
				       UDISKS_DBUS_SERVICE,
				       path,
				       interface,
				       NULL, error);
	if (proxy == NULL) {
		g_prefix_error (error, "failed to find %s: ", UDISKS_DBUS_SERVICE);
		return NULL;
	}
	return g_steal_pointer (&proxy);
}


GPtrArray *
fu_uefi_udisks_get_block_devices (GError **error)
{
	g_autoptr(GVariant) output = NULL;
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GVariantIter) iter = NULL;
	GVariant *input;
	GVariantBuilder builder;
	const gchar *obj;

	proxy = fu_uefi_udisks_get_dbus_proxy (UDISKS_DBUS_PATH,
					       UDISKS_DBUS_MANAGER_INTERFACE,
					       error);
	if (proxy == NULL)
		return NULL;
	g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
	input = g_variant_new ("(a{sv})", &builder);
	output =  g_dbus_proxy_call_sync (proxy,
					  "GetBlockDevices", g_variant_ref (input),
					  G_DBUS_CALL_FLAGS_NONE,
					  -1, NULL, error);
	if (output == NULL)
		return NULL;
	devices = g_ptr_array_new_with_free_func (g_free);
	g_variant_get (output, "(ao)", &iter);
	while (g_variant_iter_next (iter, "o", &obj))
		g_ptr_array_add (devices, g_strdup (obj));

	return g_steal_pointer (&devices);
}

gboolean
fu_uefi_udisks_objpath_is_esp (const gchar *obj)
{
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GVariant) val = NULL;
	const gchar *str;

	proxy = fu_uefi_udisks_get_dbus_proxy (obj,
					       UDISKS_DBUS_PART_INTERFACE,
					       &error_local);
	if (proxy == NULL) {
		g_warning ("Failed to initialize d-bus proxy: %s",
			   error_local->message);
		return FALSE;
	}
	val = g_dbus_proxy_get_cached_property (proxy, "Type");
	if (val == NULL)
		return FALSE;

	g_variant_get (val, "s", &str);
	return g_strcmp0 (str, ESP_DISK_TYPE) == 0;
}

gboolean
fu_uefi_udisks_objpath_umount (const gchar *path, GError **error)
{
	GVariant *input;
	GVariantBuilder builder;
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GVariant) val = NULL;

	g_return_val_if_fail (fu_uefi_udisks_objpath (path), FALSE);

	proxy = fu_uefi_udisks_get_dbus_proxy (path,
					       UDISKS_DBUS_FILE_INTERFACE,
					       error);
	if (proxy == NULL)
		return FALSE;

	g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
	input = g_variant_new ("(a{sv})", &builder);
	val = g_dbus_proxy_call_sync (proxy,
				      "Unmount", input,
				      G_DBUS_CALL_FLAGS_NONE,
				      -1, NULL, error);
	if (val == NULL)
		return FALSE;
	return TRUE;
}

gchar *
fu_uefi_udisks_objpath_mount (const gchar *path, GError **error)
{
	GVariant *input;
	GVariantBuilder builder;
	const gchar *str;
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GVariant) val = NULL;

	g_return_val_if_fail (fu_uefi_udisks_objpath (path), NULL);

	proxy = fu_uefi_udisks_get_dbus_proxy (path,
					       UDISKS_DBUS_FILE_INTERFACE,
					       error);
	if (proxy == NULL)
		return NULL;

	g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
	input = g_variant_new ("(a{sv})", &builder);
	val = g_dbus_proxy_call_sync (proxy,
				      "Mount", input,
				      G_DBUS_CALL_FLAGS_NONE,
				      -1, NULL, error);
	if (val == NULL)
		return NULL;
	g_variant_get (val, "(s)", &str);

	return g_strdup (str);
}

gchar *
fu_uefi_udisks_objpath_is_mounted (const gchar *path)
{
	const gchar **mountpoints = NULL;
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GVariant) val = NULL;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (fu_uefi_udisks_objpath (path), NULL);

	proxy = fu_uefi_udisks_get_dbus_proxy (path,
					       UDISKS_DBUS_FILE_INTERFACE,
					       &error_local);
	if (proxy == NULL) {
		g_warning ("%s", error_local->message);
		return NULL;
	}
	val = g_dbus_proxy_get_cached_property (proxy, "MountPoints");
	if (val == NULL)
		return NULL;
	mountpoints = g_variant_get_bytestring_array (val, NULL);

	return g_strdup (mountpoints[0]);
}
