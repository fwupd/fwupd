/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <gio/gunixfdlist.h>
#include <glib/gstdio.h>

#include "fu-logind-plugin.h"

struct _FuLogindPlugin {
	FuPlugin parent_instance;
	GDBusProxy *logind_proxy;
	gint logind_fd;
};

G_DEFINE_TYPE(FuLogindPlugin, fu_logind_plugin, FU_TYPE_PLUGIN)

static void
fu_logind_plugin_to_string(FuPlugin *plugin, guint idt, GString *str)
{
	FuLogindPlugin *self = FU_LOGIND_PLUGIN(plugin);
	fu_string_append_kx(str, idt, "LogindFd", self->logind_fd);
}

static gboolean
fu_logind_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuLogindPlugin *self = FU_LOGIND_PLUGIN(plugin);
	g_autofree gchar *name_owner = NULL;

	self->logind_proxy = g_dbus_proxy_new_for_bus_sync(
	    G_BUS_TYPE_SYSTEM,
	    G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS | G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
	    NULL,
	    "org.freedesktop.login1",
	    "/org/freedesktop/login1",
	    "org.freedesktop.login1.Manager",
	    NULL,
	    error);
	if (self->logind_proxy == NULL) {
		g_prefix_error(error, "failed to connect to logind: ");
		return FALSE;
	}
	name_owner = g_dbus_proxy_get_name_owner(self->logind_proxy);
	if (name_owner == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no owner for %s",
			    g_dbus_proxy_get_name(self->logind_proxy));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_logind_plugin_prepare(FuPlugin *plugin,
			 FuDevice *device,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuLogindPlugin *self = FU_LOGIND_PLUGIN(plugin);
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GUnixFDList) out_fd_list = NULL;
	g_autoptr(GVariant) res = NULL;
	const gchar *what = "shutdown:sleep:idle:handle-power-key:handle-suspend-key:"
			    "handle-hibernate-key:handle-lid-switch";

	/* already inhibited */
	if (self->logind_fd != 0)
		return TRUE;

	/* not yet connected */
	if (self->logind_proxy == NULL) {
		g_warning("no logind connection to use");
		return TRUE;
	}

	/* block shutdown and idle */
	res = g_dbus_proxy_call_with_unix_fd_list_sync(
	    self->logind_proxy,
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
	self->logind_fd = g_unix_fd_list_get(out_fd_list, 0, NULL);
	g_debug("opened logind fd %i", self->logind_fd);
	return TRUE;
}

static gboolean
fu_logind_plugin_cleanup(FuPlugin *plugin,
			 FuDevice *device,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuLogindPlugin *self = FU_LOGIND_PLUGIN(plugin);
	if (self->logind_fd == 0)
		return TRUE;
	g_debug("closed logind fd %i", self->logind_fd);
	if (!g_close(self->logind_fd, error))
		return FALSE;
	self->logind_fd = 0;
	return TRUE;
}

static void
fu_logind_plugin_init(FuLogindPlugin *self)
{
}

static void
fu_logind_finalize(GObject *obj)
{
	FuLogindPlugin *self = FU_LOGIND_PLUGIN(obj);
	if (self->logind_fd != 0)
		g_close(self->logind_fd, NULL);
	if (self->logind_proxy != NULL)
		g_object_unref(self->logind_proxy);
	G_OBJECT_CLASS(fu_logind_plugin_parent_class)->finalize(obj);
}

static void
fu_logind_plugin_class_init(FuLogindPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_logind_finalize;
	plugin_class->to_string = fu_logind_plugin_to_string;
	plugin_class->startup = fu_logind_plugin_startup;
	plugin_class->cleanup = fu_logind_plugin_cleanup;
	plugin_class->prepare = fu_logind_plugin_prepare;
}
