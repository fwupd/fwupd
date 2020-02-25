/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <gio/gunixfdlist.h>
#include <glib/gstdio.h>

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"

struct FuPluginData {
	GDBusProxy		*logind_proxy;
	gint			 logind_fd;
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
	if (data->logind_fd != 0)
		g_close (data->logind_fd, NULL);
	if (data->logind_proxy != NULL)
		g_object_unref (data->logind_proxy);
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	data->logind_proxy =
		g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
					       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS |
					       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
					       NULL,
					       "org.freedesktop.login1",
					       "/org/freedesktop/login1",
					       "org.freedesktop.login1.Manager",
					       NULL,
					       error);
	if (data->logind_proxy == NULL) {
		g_prefix_error (error, "failed to connect to logind: ");
		return FALSE;
	}
	if (g_dbus_proxy_get_name_owner (data->logind_proxy) == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "no owner for %s",
			     g_dbus_proxy_get_name (data->logind_proxy));
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
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GUnixFDList) out_fd_list = NULL;
	g_autoptr(GVariant) res = NULL;
	const gchar *what = "shutdown:sleep:idle:handle-power-key:handle-suspend-key:"
			    "handle-hibernate-key:handle-lid-switch";

	/* already inhibited */
	if (data->logind_fd != 0)
		return TRUE;

	/* not yet connected */
	if (data->logind_proxy == NULL) {
		g_warning ("no logind connection to use");
		return TRUE;
	}

	/* block shutdown and idle */
	res = g_dbus_proxy_call_with_unix_fd_list_sync (data->logind_proxy,
							"Inhibit",
							g_variant_new ("(ssss)",
								       what,
								       PACKAGE_NAME,
								       "Firmware Update in Progress",
								       "block"),
							G_DBUS_CALL_FLAGS_NONE,
							-1,
							NULL, /* fd_list */
							&out_fd_list,
							NULL, /* GCancellable */
							&error_local);
	if (res == NULL) {
		g_warning ("failed to Inhibit using logind: %s", error_local->message);
		return TRUE;
	}

	/* keep fd as cookie */
	if (g_unix_fd_list_get_length (out_fd_list) != 1) {
		g_warning ("invalid response from logind");
		return TRUE;
	}
	data->logind_fd = g_unix_fd_list_get (out_fd_list, 0, NULL);
	g_debug ("opened logind fd %i", data->logind_fd);
	return TRUE;
}

gboolean
fu_plugin_update_cleanup (FuPlugin *plugin,
			  FwupdInstallFlags flags,
			  FuDevice *device,
			  GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	if (data->logind_fd == 0)
		return TRUE;
	g_debug ("closed logind fd %i", data->logind_fd);
	if (!g_close (data->logind_fd, error))
		return FALSE;
	data->logind_fd = 0;
	return TRUE;
}
