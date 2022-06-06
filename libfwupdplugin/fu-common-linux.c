/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuCommon"

#include "config.h"

#include <gio/gio.h>
#include <unistd.h>

#ifdef HAVE_FNMATCH_H
#include <fnmatch.h>
#endif

#include "fu-common-private.h"
#include "fu-path-private.h"

#define UDISKS_DBUS_PATH	      "/org/freedesktop/UDisks2/Manager"
#define UDISKS_DBUS_MANAGER_INTERFACE "org.freedesktop.UDisks2.Manager"

GPtrArray *
fu_common_get_block_devices(GError **error)
{
	GVariantBuilder builder;
	const gchar *obj;
	g_autoptr(GVariant) output = NULL;
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GVariantIter) obj_iter = NULL;
	g_autoptr(GDBusConnection) connection = NULL;

	connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, error);
	if (connection == NULL) {
		g_prefix_error(error, "failed to get system bus: ");
		return NULL;
	}
	proxy = g_dbus_proxy_new_sync(connection,
				      G_DBUS_PROXY_FLAGS_NONE,
				      NULL,
				      UDISKS_DBUS_SERVICE,
				      UDISKS_DBUS_PATH,
				      UDISKS_DBUS_MANAGER_INTERFACE,
				      NULL,
				      error);
	if (proxy == NULL) {
		g_prefix_error(error, "failed to find %s: ", UDISKS_DBUS_SERVICE);
		return NULL;
	}

	devices = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
	output = g_dbus_proxy_call_sync(proxy,
					"GetBlockDevices",
					g_variant_new("(a{sv})", &builder),
					G_DBUS_CALL_FLAGS_NONE,
					-1,
					NULL,
					error);
	if (output == NULL) {
		if (error != NULL)
			g_dbus_error_strip_remote_error(*error);
		g_prefix_error(error,
			       "failed to call %s.%s(): ",
			       UDISKS_DBUS_MANAGER_INTERFACE,
			       "GetBlockDevices");
		return NULL;
	}

	g_variant_get(output, "(ao)", &obj_iter);
	while (g_variant_iter_next(obj_iter, "&o", &obj)) {
		g_autoptr(GDBusProxy) proxy_blk = NULL;
		proxy_blk = g_dbus_proxy_new_sync(connection,
						  G_DBUS_PROXY_FLAGS_NONE,
						  NULL,
						  UDISKS_DBUS_SERVICE,
						  obj,
						  UDISKS_DBUS_INTERFACE_BLOCK,
						  NULL,
						  error);
		if (proxy_blk == NULL) {
			g_prefix_error(error, "failed to initialize d-bus proxy for %s: ", obj);
			return NULL;
		}
		g_ptr_array_add(devices, g_steal_pointer(&proxy_blk));
	}
	return g_steal_pointer(&devices);
}

gboolean
fu_path_fnmatch_impl(const gchar *pattern, const gchar *str)
{
#ifdef HAVE_FNMATCH_H
	return fnmatch(pattern, str, FNM_NOESCAPE) == 0;
#else
	return g_strcmp0(pattern, str) == 0;
#endif
}

guint64
fu_common_get_memory_size_impl(void)
{
	return (guint64)sysconf(_SC_PHYS_PAGES) * (guint64)sysconf(_SC_PAGE_SIZE);
}
