/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <gio/gunixfdlist.h>
#include <glib/gstdio.h>

struct FuPluginData {
	GDBusProxy *logind_proxy;
	gint logind_fd;
};

static void
fu_plugin_logind_init(FuPlugin *plugin)
{
	fu_plugin_alloc_data(plugin, sizeof(FuPluginData));
}

static void
fu_plugin_logind_destroy(FuPlugin *plugin)
{
	FuPluginData *priv = fu_plugin_get_data(plugin);
	if (priv->logind_fd != 0)
		g_close(priv->logind_fd, NULL);
	if (priv->logind_proxy != NULL)
		g_object_unref(priv->logind_proxy);
}

static gboolean
fu_plugin_logind_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuPluginData *priv = fu_plugin_get_data(plugin);
	g_autofree gchar *name_owner = NULL;

	priv->logind_proxy = g_dbus_proxy_new_for_bus_sync(
	    G_BUS_TYPE_SYSTEM,
	    G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS | G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
	    NULL,
	    "org.freedesktop.login1",
	    "/org/freedesktop/login1",
	    "org.freedesktop.login1.Manager",
	    NULL,
	    error);
	if (priv->logind_proxy == NULL) {
		g_prefix_error(error, "failed to connect to logind: ");
		return FALSE;
	}
	name_owner = g_dbus_proxy_get_name_owner(priv->logind_proxy);
	if (name_owner == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no owner for %s",
			    g_dbus_proxy_get_name(priv->logind_proxy));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_plugin_logind_prepare(FuPlugin *plugin,
			 FuDevice *device,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuPluginData *priv = fu_plugin_get_data(plugin);
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GUnixFDList) out_fd_list = NULL;
	g_autoptr(GVariant) res = NULL;
	const gchar *what = "shutdown:sleep:idle:handle-power-key:handle-suspend-key:"
			    "handle-hibernate-key:handle-lid-switch";

	/* already inhibited */
	if (priv->logind_fd != 0)
		return TRUE;

	/* not yet connected */
	if (priv->logind_proxy == NULL) {
		g_warning("no logind connection to use");
		return TRUE;
	}

	/* block shutdown and idle */
	res = g_dbus_proxy_call_with_unix_fd_list_sync(
	    priv->logind_proxy,
	    "Inhibit",
	    g_variant_new("(ssss)", what, PACKAGE_NAME, "Firmware Update in Progress", "block"),
	    G_DBUS_CALL_FLAGS_NONE,
	    -1,
	    NULL, /* fd_list */
	    &out_fd_list,
	    NULL, /* GCancellable */
	    &error_local);
	if (res == NULL) {
		g_warning("failed to Inhibit using logind: %s", error_local->message);
		return TRUE;
	}

	/* keep fd as cookie */
	if (g_unix_fd_list_get_length(out_fd_list) != 1) {
		g_warning("invalid response from logind");
		return TRUE;
	}
	priv->logind_fd = g_unix_fd_list_get(out_fd_list, 0, NULL);
	g_debug("opened logind fd %i", priv->logind_fd);
	return TRUE;
}

static gboolean
fu_plugin_logind_cleanup(FuPlugin *plugin,
			 FuDevice *device,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuPluginData *priv = fu_plugin_get_data(plugin);
	if (priv->logind_fd == 0)
		return TRUE;
	g_debug("closed logind fd %i", priv->logind_fd);
	if (!g_close(priv->logind_fd, error))
		return FALSE;
	priv->logind_fd = 0;
	return TRUE;
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_logind_init;
	vfuncs->destroy = fu_plugin_logind_destroy;
	vfuncs->startup = fu_plugin_logind_startup;
	vfuncs->cleanup = fu_plugin_logind_cleanup;
	vfuncs->prepare = fu_plugin_logind_prepare;
}
