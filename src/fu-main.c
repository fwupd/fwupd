/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
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

#include <stdlib.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <glib/gi18n.h>
#include <locale.h>
#include <polkit/polkit.h>

#include "fu-cleanup.h"
#include "fu-common.h"
#include "fu-debug.h"
#include "fu-device.h"
#include "fu-provider-uefi.h"
#include "fu-resources.h"

typedef struct {
	GDBusConnection		*connection;
	GDBusNodeInfo		*introspection_daemon;
	GMainLoop		*loop;
	GPtrArray		*devices;
	GPtrArray		*providers;
	PolkitAuthority		*authority;
} FuMainPrivate;

typedef struct {
	FuDevice		*device;
	FuProvider		*provider;
} FuDeviceItem;

/**
 * fu_main_get_device_list_as_strv:
 **/
static gchar **
fu_main_get_device_list_as_strv (FuMainPrivate *priv)
{
	gchar **val;
	guint i;
	FuDevice *dev_tmp;

	val = g_new0 (gchar *, priv->devices->len + 1);
	for (i = 0; i < priv->devices->len; i++) {
		dev_tmp = g_ptr_array_index (priv->devices, i);
		val[i] = g_strdup (fu_device_get_id (dev_tmp));
	}
	return val;
}

/**
 * fu_main_item_free:
 **/
static void
fu_main_item_free (FuDeviceItem *item)
{
	g_object_unref (item->device);
	g_object_unref (item->provider);
	g_free (item);
}

/**
 * fu_main_get_item_by_id:
 **/
static FuDeviceItem *
fu_main_get_item_by_id (FuMainPrivate *priv, const gchar *id)
{
	FuDeviceItem *item;
	guint i;

	for (i = 0; i < priv->devices->len; i++) {
		item = g_ptr_array_index (priv->devices, i);
		if (g_strcmp0 (fu_device_get_id (item->device), id) == 0)
			return item;
	}
	return NULL;
}

typedef struct {
	GDBusMethodInvocation	*invocation;
	FuMainPrivate		*priv;
	FuProviderFlags		 flags;
	gint			 fd;
	gchar			*id;
} FuMainAuthHelper;

/**
 * fu_main_helper_free:
 **/
static void
fu_main_helper_free (FuMainAuthHelper *helper)
{
	g_object_unref (helper->invocation);
	g_free (helper->id);
	g_free (helper);
}

/**
 * fu_main_check_authorization_cb:
 **/
static void
fu_main_check_authorization_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	FuMainAuthHelper *helper = (FuMainAuthHelper *) user_data;
	FuDeviceItem *item;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_object_unref_ PolkitAuthorizationResult *auth = NULL;

	/* get result */
	auth = polkit_authority_check_authorization_finish (POLKIT_AUTHORITY (source),
							    res, &error);
	if (auth == NULL) {
		g_dbus_method_invocation_return_error (helper->invocation,
						       FU_ERROR,
						       FU_ERROR_INTERNAL,
						       "could not check for auth: %s",
						       error->message);
		fu_main_helper_free (helper);
		return;
	}

	/* did not auth */
	if (!polkit_authorization_result_get_is_authorized (auth)) {
		g_dbus_method_invocation_return_error (helper->invocation,
						       FU_ERROR,
						       FU_ERROR_INTERNAL,
						       "failed to obtain auth");
		fu_main_helper_free (helper);
		return;
	}

	/* check the device still exists */
	item = fu_main_get_item_by_id (helper->priv, helper->id);
	if (item == NULL) {
		g_dbus_method_invocation_return_error (helper->invocation,
						       FU_ERROR,
						       FU_ERROR_INTERNAL,
						       "device %s was removed",
						       helper->id);
		fu_main_helper_free (helper);
		return;
	}

	/* run the correct provider that added this */
	if (!fu_provider_update (item->provider,
				 item->device,
				 helper->flags,
				 helper->fd, &error)) {
		g_dbus_method_invocation_return_gerror (helper->invocation,
							error);
		fu_main_helper_free (helper);
		return;
	}

	/* success */
	g_dbus_method_invocation_return_value (helper->invocation, NULL);
	fu_main_helper_free (helper);
}

/**
 * fu_main_daemon_method_call:
 **/
static void
fu_main_daemon_method_call (GDBusConnection *connection, const gchar *sender,
			    const gchar *object_path, const gchar *interface_name,
			    const gchar *method_name, GVariant *parameters,
			    GDBusMethodInvocation *invocation, gpointer user_data)
{
	FuMainPrivate *priv = (FuMainPrivate *) user_data;
	GVariant *val;

	/* return 'as' */
	if (g_strcmp0 (method_name, "GetDevices") == 0) {
		_cleanup_strv_free_ gchar **devices = NULL;
		g_debug ("Called %s()", method_name);
		devices = fu_main_get_device_list_as_strv (priv);
		val = g_variant_new ("(^as)", devices);
		g_dbus_method_invocation_return_value (invocation, val);
		return;
	}

	/* return '' */
	if (g_strcmp0 (method_name, "Update") == 0) {
		FuDeviceItem *item;
		FuMainAuthHelper *helper;
		FuProviderFlags flags = FU_PROVIDER_UPDATE_FLAG_NONE;
		GDBusMessage *message;
		GUnixFDList *fd_list;
		GVariant *prop_value;
		const gchar *id = NULL;
		gchar *prop_key;
		gint32 fd_handle = 0;
		gint fd;
		_cleanup_error_free_ GError *error = NULL;
		_cleanup_object_unref_ PolkitSubject *subject = NULL;
		_cleanup_variant_iter_free_ GVariantIter *iter = NULL;

		/* check the id exists */
		g_variant_get (parameters, "(&sha{sv})", &id, &fd_handle, &iter);
		g_debug ("Called %s(%s,%i)", method_name, id, fd_handle);
		item = fu_main_get_item_by_id (priv, id);
		if (item == NULL) {
			g_dbus_method_invocation_return_error (invocation,
							       FU_ERROR,
							       FU_ERROR_INTERNAL,
							       "no such ID %s",
							       id);
			return;
		}

		/* get options */
		while (g_variant_iter_next (iter, "{&sv}",
					    &prop_key, &prop_value)) {
			g_debug ("got option %s", prop_key);
			if (g_strcmp0 (prop_key, "offline") == 0 &&
			    g_variant_get_boolean (prop_value) == TRUE)
				flags |= FU_PROVIDER_UPDATE_FLAG_OFFLINE;
		}

		/* get the fd */
		message = g_dbus_method_invocation_get_message (invocation);
		fd_list = g_dbus_message_get_unix_fd_list (message);
		if (fd_list == NULL || g_unix_fd_list_get_length (fd_list) != 1) {
			g_dbus_method_invocation_return_error (invocation,
							       FU_ERROR,
							       FU_ERROR_INTERNAL,
							       "invalid handle");
			return;
		}
		fd = g_unix_fd_list_get (fd_list, fd_handle, &error);
		if (fd < 0) {
			g_dbus_method_invocation_return_gerror (invocation,
								error);
			return;
		}


		/* do authorization async */
		helper = g_new0 (FuMainAuthHelper, 1);
		helper->invocation = g_object_ref (invocation);
		helper->fd = fd;
		helper->id = g_strdup (id);
		helper->flags = flags;
		helper->priv = priv;
		subject = polkit_system_bus_name_new (sender);
		polkit_authority_check_authorization (priv->authority, subject,
						      "org.freedesktop.fwupd.update",
						      NULL,
						      POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
						      NULL,
						      fu_main_check_authorization_cb,
						      helper);
		return;
	}

	/* we suck */
	g_dbus_method_invocation_return_error (invocation,
					       FU_ERROR,
					       FU_ERROR_INTERNAL,
					       "no such method %s",
					       method_name);
}

/**
 * fu_main_daemon_get_property:
 **/
static GVariant *
fu_main_daemon_get_property (GDBusConnection *connection_, const gchar *sender,
			     const gchar *object_path, const gchar *interface_name,
			     const gchar *property_name, GError **error,
			     gpointer user_data)
{
	if (g_strcmp0 (property_name, "DaemonVersion") == 0)
		return g_variant_new_string (VERSION);

	/* return an error */
	g_set_error (error,
		     FU_ERROR,
		     FU_ERROR_INTERNAL,
		     "failed to get daemon property %s",
		     property_name);
	return NULL;
}

/**
 * fu_main_on_bus_acquired_cb:
 **/
static void
fu_main_on_bus_acquired_cb (GDBusConnection *connection,
			    const gchar *name,
			    gpointer user_data)
{
	FuMainPrivate *priv = (FuMainPrivate *) user_data;
	guint registration_id;
	static const GDBusInterfaceVTable interface_vtable = {
		fu_main_daemon_method_call,
		fu_main_daemon_get_property,
		NULL
	};

	priv->connection = g_object_ref (connection);
	registration_id = g_dbus_connection_register_object (connection,
							     FWUPD_DBUS_PATH,
							     priv->introspection_daemon->interfaces[0],
							     &interface_vtable,
							     priv,  /* user_data */
							     NULL,  /* user_data_free_func */
							     NULL); /* GError** */
	g_assert (registration_id > 0);
}

/**
 * fu_main_on_name_acquired_cb:
 **/
static void
fu_main_on_name_acquired_cb (GDBusConnection *connection,
			     const gchar *name,
			     gpointer user_data)
{
	FuMainPrivate *priv = (FuMainPrivate *) user_data;
	FuProvider *provider;
	guint i;

	g_debug ("FuMain: acquired name: %s", name);
	for (i = 0; i < priv->providers->len; i++) {
		_cleanup_error_free_ GError *error = NULL;
		provider = g_ptr_array_index (priv->providers, i);
		if (!fu_provider_coldplug (FU_PROVIDER (provider), &error))
			g_warning ("Failed to coldplug: %s", error->message);
	}
}

/**
 * fu_main_on_name_lost_cb:
 **/
static void
fu_main_on_name_lost_cb (GDBusConnection *connection,
			 const gchar *name,
			 gpointer user_data)
{
	FuMainPrivate *priv = (FuMainPrivate *) user_data;
	g_debug ("FuMain: lost name: %s", name);
	g_main_loop_quit (priv->loop);
}

/**
 * fu_main_timed_exit_cb:
 **/
static gboolean
fu_main_timed_exit_cb (gpointer user_data)
{
	GMainLoop *loop = (GMainLoop *) user_data;
	g_main_loop_quit (loop);
	return G_SOURCE_REMOVE;
}

/**
 * fu_main_load_introspection:
 **/
static GDBusNodeInfo *
fu_main_load_introspection (const gchar *filename, GError **error)
{
	_cleanup_bytes_unref_ GBytes *data = NULL;
	_cleanup_free_ gchar *path = NULL;

	/* lookup data */
	path = g_build_filename ("/org/freedesktop/fwupd", filename, NULL);
	data = g_resource_lookup_data (fu_get_resource (),
				       path,
				       G_RESOURCE_LOOKUP_FLAGS_NONE,
				       error);
	if (data == NULL)
		return NULL;

	/* build introspection from XML */
	return g_dbus_node_info_new_for_xml (g_bytes_get_data (data, NULL), error);
}

/**
 * cd_main_provider_device_added_cb:
 **/
static void
cd_main_provider_device_added_cb (FuProvider *provider,
				  FuDevice *device,
				  gpointer user_data)
{
	FuMainPrivate *priv = (FuMainPrivate *) user_data;
	FuDeviceItem *item;

	item = g_new0 (FuDeviceItem, 1);
	item->device = g_object_ref (device);
	item->provider = g_object_ref (provider);
	g_ptr_array_add (priv->devices, item);
}

/**
 * cd_main_provider_device_removed_cb:
 **/
static void
cd_main_provider_device_removed_cb (FuProvider *provider,
				    FuDevice *device,
				    gpointer user_data)
{
	FuMainPrivate *priv = (FuMainPrivate *) user_data;
	FuDeviceItem *item;

	item = fu_main_get_item_by_id (priv, fu_device_get_id (device));
	if (item == NULL) {
		g_warning ("can't remove device %s", fu_device_get_id (device));
		return;
	}
	g_ptr_array_remove (priv->devices, item);
}

/**
 * fu_main_add_provider:
 **/
static void
fu_main_add_provider (FuMainPrivate *priv, FuProvider *provider)
{
	g_signal_connect (provider, "device-added",
			  G_CALLBACK (cd_main_provider_device_added_cb),
			  priv);
	g_signal_connect (provider, "device-removed",
			  G_CALLBACK (cd_main_provider_device_removed_cb),
			  priv);
	g_ptr_array_add (priv->providers, provider);
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	FuMainPrivate *priv = NULL;
	gboolean immediate_exit = FALSE;
	gboolean ret;
	gboolean timed_exit = FALSE;
	GOptionContext *context;
	guint owner_id = 0;
	guint retval = 1;
	const GOptionEntry options[] = {
		{ "timed-exit", '\0', 0, G_OPTION_ARG_NONE, &timed_exit,
		  /* TRANSLATORS: exit after we've started up, used for user profiling */
		  _("Exit after a small delay"), NULL },
		{ "immediate-exit", '\0', 0, G_OPTION_ARG_NONE, &immediate_exit,
		  /* TRANSLATORS: exit straight away, used for automatic profiling */
		  _("Exit after the engine has loaded"), NULL },
		{ NULL}
	};
	_cleanup_error_free_ GError *error = NULL;

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* TRANSLATORS: program name */
	g_set_application_name (_("Firmware Update"));
	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_add_group (context, fu_debug_get_option_group ());
	g_option_context_set_summary (context, _("Firmware Update D-Bus Service"));
	ret = g_option_context_parse (context, &argc, &argv, &error);
	if (!ret) {
		g_warning ("FuMain: failed to parse command line arguments: %s",
			   error->message);
		goto out;
	}

	/* create new objects */
	priv = g_new0 (FuMainPrivate, 1);
	priv->devices = g_ptr_array_new_with_free_func ((GDestroyNotify) fu_main_item_free);
	priv->loop = g_main_loop_new (NULL, FALSE);

	/* add providers */
	priv->providers = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	fu_main_add_provider (priv, fu_provider_uefi_new ());

	/* load introspection from file */
	priv->introspection_daemon = fu_main_load_introspection (FWUPD_DBUS_INTERFACE ".xml",
								 &error);
	if (priv->introspection_daemon == NULL) {
		g_warning ("FuMain: failed to load daemon introspection: %s",
			   error->message);
		goto out;
	}

	/* get authority */
	priv->authority = polkit_authority_get_sync (NULL, &error);
	if (priv->authority == NULL) {
		g_warning ("FuMain: failed to load polkit authority: %s",
			   error->message);
		goto out;
	}

	/* own the object */
	owner_id = g_bus_own_name (G_BUS_TYPE_SYSTEM,
				   FWUPD_DBUS_SERVICE,
				   G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
				    G_BUS_NAME_OWNER_FLAGS_REPLACE,
				   fu_main_on_bus_acquired_cb,
				   fu_main_on_name_acquired_cb,
				   fu_main_on_name_lost_cb,
				   priv, NULL);

	/* Only timeout and close the mainloop if we have specified it
	 * on the command line */
	if (immediate_exit)
		g_idle_add (fu_main_timed_exit_cb, priv->loop);
	else if (timed_exit)
		g_timeout_add_seconds (5, fu_main_timed_exit_cb, priv->loop);

	/* wait */
	g_info ("Daemon ready for requests");
	g_main_loop_run (priv->loop);

	/* success */
	retval = 0;
out:
	g_option_context_free (context);
	if (owner_id > 0)
		g_bus_unown_name (owner_id);
	if (priv != NULL) {
		if (priv->loop != NULL)
			g_main_loop_unref (priv->loop);
		if (priv->connection != NULL)
			g_object_unref (priv->connection);
		if (priv->authority != NULL)
			g_object_unref (priv->authority);
		if (priv->introspection_daemon != NULL)
			g_dbus_node_info_unref (priv->introspection_daemon);
		g_ptr_array_unref (priv->providers);
		g_ptr_array_unref (priv->devices);
		g_free (priv);
	}
	return retval;
}

