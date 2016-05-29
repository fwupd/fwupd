/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "config.h"

#include <glib-object.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "fwupd-client.h"
#include "fwupd-enums.h"
#include "fwupd-error.h"
#include "fwupd-result.h"

static void fwupd_client_finalize	 (GObject *object);

/**
 * FwupdClientPrivate:
 *
 * Private #FwupdClient data
 **/
typedef struct {
	FwupdStatus			 status;
	GDBusConnection			*conn;
	GDBusProxy			*proxy;
} FwupdClientPrivate;

enum {
	SIGNAL_CHANGED,
	SIGNAL_STATUS_CHANGED,
	SIGNAL_DEVICE_ADDED,
	SIGNAL_DEVICE_REMOVED,
	SIGNAL_DEVICE_CHANGED,
	SIGNAL_LAST
};

enum {
	PROP_0,
	PROP_STATUS,
	PROP_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (FwupdClient, fwupd_client, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fwupd_client_get_instance_private (o))

typedef struct {
	gboolean	 ret;
	GError		*error;
	GMainLoop	*loop;
	GVariant	*val;
	GDBusMessage	*message;
} FwupdClientHelper;

/**
 * fwupd_client_helper_free:
 **/
static void
fwupd_client_helper_free (FwupdClientHelper *helper)
{
	if (helper->message != NULL)
		g_object_unref (helper->message);
	if (helper->val != NULL)
		g_variant_unref (helper->val);
	if (helper->error != NULL)
		g_error_free (helper->error);
	g_main_loop_unref (helper->loop);
	g_free (helper);
}

/**
 * fwupd_client_helper_new:
 **/
static FwupdClientHelper *
fwupd_client_helper_new (void)
{
	FwupdClientHelper *helper;
	helper = g_new0 (FwupdClientHelper, 1);
	helper->loop = g_main_loop_new (NULL, FALSE);
	return helper;
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FwupdClientHelper, fwupd_client_helper_free)

/**
 * fwupd_client_properties_changed_cb:
 **/
static void
fwupd_client_properties_changed_cb (GDBusProxy *proxy,
				    GVariant *changed_properties,
				    GStrv invalidated_properties,
				    FwupdClient *client)
{
	g_autoptr(GVariant) val = NULL;
	FwupdClientPrivate *priv = GET_PRIVATE (client);

	/* print to the console */
	val = g_dbus_proxy_get_cached_property (proxy, "Status");
	if (val == NULL)
		return;
	priv->status = g_variant_get_uint32 (val);
	g_debug ("Emitting ::status-changed() [%s]", fwupd_status_to_string (priv->status));
	g_signal_emit (client, signals[SIGNAL_STATUS_CHANGED], 0, priv->status);
}

/**
 * fwupd_client_signal_cb:
 */
static void
fwupd_client_signal_cb (GDBusProxy *proxy,
			const gchar *sender_name,
			const gchar *signal_name,
			GVariant *parameters,
			FwupdClient *client)
{
	g_autoptr(FwupdResult) res = NULL;
	if (g_strcmp0 (signal_name, "Changed") == 0) {
		g_debug ("Emitting ::changed()");
		g_signal_emit (client, signals[SIGNAL_CHANGED], 0);
		return;
	}
	if (g_strcmp0 (signal_name, "DeviceAdded") == 0) {
		res = fwupd_result_new_from_data (parameters);
		g_debug ("Emitting ::device-added(%s)",
			 fwupd_result_get_device_id (res));
		g_signal_emit (client, signals[SIGNAL_DEVICE_ADDED], 0, res);
		return;
	}
	if (g_strcmp0 (signal_name, "DeviceRemoved") == 0) {
		res = fwupd_result_new_from_data (parameters);
		g_signal_emit (client, signals[SIGNAL_DEVICE_REMOVED], 0, res);
		g_debug ("Emitting ::device-removed(%s)",
			 fwupd_result_get_device_id (res));
		return;
	}
	if (g_strcmp0 (signal_name, "DeviceChanged") == 0) {
		res = fwupd_result_new_from_data (parameters);
		g_signal_emit (client, signals[SIGNAL_DEVICE_CHANGED], 0, res);
		g_debug ("Emitting ::device-changed(%s)",
			 fwupd_result_get_device_id (res));
		return;
	}
	g_warning ("Unknown signal name '%s' from %s",
		   signal_name, sender_name);
}

/**
 * fwupd_client_connect:
 * @client: A #FwupdClient
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Sets up the client ready for use. Most other methods call this
 * for you, and do you only need to call this if you are just watching
 * the client.
 *
 * Returns: %TRUE for success
 *
 * Since: 0.7.1
 **/
gboolean
fwupd_client_connect (FwupdClient *client, GCancellable *cancellable, GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* nothing to do */
	if (priv->proxy != NULL)
		return TRUE;

	/* connect to the daemon */
	priv->conn = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, error);
	if (priv->conn == NULL)
		return FALSE;
	priv->proxy = g_dbus_proxy_new_sync (priv->conn,
					     G_DBUS_PROXY_FLAGS_NONE,
					     NULL,
					     FWUPD_DBUS_SERVICE,
					     FWUPD_DBUS_PATH,
					     FWUPD_DBUS_INTERFACE,
					     NULL,
					     error);
	if (priv->proxy == NULL)
		return FALSE;
	g_signal_connect (priv->proxy, "g-properties-changed",
			  G_CALLBACK (fwupd_client_properties_changed_cb), client);
	g_signal_connect (priv->proxy, "g-signal",
			  G_CALLBACK (fwupd_client_signal_cb), client);
	return TRUE;
}

/**
 * fwupd_client_parse_results_from_data:
 **/
static GPtrArray *
fwupd_client_parse_results_from_data (GVariant *devices)
{
	FwupdResult *res;
	GPtrArray *results = NULL;
	gsize sz;
	guint i;
	g_autoptr(GVariant) untuple = NULL;

	results = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	untuple = g_variant_get_child_value (devices, 0);
	sz = g_variant_n_children (untuple);
	for (i = 0; i < sz; i++) {
		g_autoptr(GVariant) data = NULL;
		data = g_variant_get_child_value (untuple, i);
		res = fwupd_result_new_from_data (data);
		g_ptr_array_add (results, res);
	}

	return results;
}

/**
 * fwupd_client_fixup_dbus_error:
 **/
static void
fwupd_client_fixup_dbus_error (GError *error)
{
	g_autofree gchar *name = NULL;

	g_return_if_fail (error != NULL);

	/* is a remote error? */
	if (!g_dbus_error_is_remote_error (error))
		return;

	/* parse the remote error */
	name = g_dbus_error_get_remote_error (error);
	error->domain = FWUPD_ERROR;
	error->code = fwupd_error_from_string (name);
	g_dbus_error_strip_remote_error (error);
}

/**
 * fwupd_client_get_devices:
 * @client: A #FwupdClient
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Gets all the devices registered with the daemon.
 *
 * Returns: (element-type FwupdResult) (transfer container): results
 *
 * Since: 0.7.0
 **/
GPtrArray *
fwupd_client_get_devices (FwupdClient *client, GCancellable *cancellable, GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_autoptr(GVariant) val = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return NULL;

	/* call into daemon */
	val = g_dbus_proxy_call_sync (priv->proxy,
				      "GetDevices",
				      NULL,
				      G_DBUS_CALL_FLAGS_NONE,
				      -1,
				      cancellable,
				      error);
	if (val == NULL) {
		if (error != NULL)
			fwupd_client_fixup_dbus_error (*error);
		return NULL;
	}
	return fwupd_client_parse_results_from_data (val);
}

/**
 * fwupd_client_get_updates:
 * @client: A #FwupdClient
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Gets all the devices with known updates.
 *
 * Returns: (element-type FwupdResult) (transfer container): results
 *
 * Since: 0.7.0
 **/
GPtrArray *
fwupd_client_get_updates (FwupdClient *client, GCancellable *cancellable, GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_autoptr(GVariant) val = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return NULL;

	/* call into daemon */
	val = g_dbus_proxy_call_sync (priv->proxy,
				      "GetUpdates",
				      NULL,
				      G_DBUS_CALL_FLAGS_NONE,
				      -1,
				      cancellable,
				      error);
	if (val == NULL) {
		if (error != NULL)
			fwupd_client_fixup_dbus_error (*error);
		return NULL;
	}
	return fwupd_client_parse_results_from_data (val);
}

/**
 * fwupd_client_proxy_call_cb:
 **/
static void
fwupd_client_proxy_call_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *) user_data;
	helper->val = g_dbus_proxy_call_finish (G_DBUS_PROXY (source),
						res, &helper->error);
	if (helper->val != NULL)
		helper->ret = TRUE;
	if (helper->error != NULL)
		fwupd_client_fixup_dbus_error (helper->error);
	g_main_loop_quit (helper->loop);
}

/**
 * fwupd_client_verify:
 * @client: A #FwupdClient
 * @device_id: the device ID
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Verify a specific device.
 *
 * Returns: %TRUE for verification success
 *
 * Since: 0.7.0
 **/
gboolean
fwupd_client_verify (FwupdClient *client, const gchar *device_id,
		     GCancellable *cancellable, GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_autoptr(FwupdClientHelper) helper = NULL;
	g_autoptr(GVariant) val = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (device_id != NULL, FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return FALSE;

	/* call into daemon */
	helper = fwupd_client_helper_new ();
	g_dbus_proxy_call (priv->proxy,
			   "Verify",
			   g_variant_new ("(s)", device_id),
			   G_DBUS_CALL_FLAGS_NONE,
			   -1,
			   cancellable,
			   fwupd_client_proxy_call_cb,
			   helper);
	g_main_loop_run (helper->loop);
	if (!helper->ret) {
		g_propagate_error (error, helper->error);
		helper->error = NULL;
		return FALSE;
	}
	return TRUE;
}

/**
 * fwupd_client_unlock:
 * @client: A #FwupdClient
 * @device_id: the device ID
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Unlocks a specific device so firmware can be read or wrote.
 *
 * Returns: %TRUE for success
 *
 * Since: 0.7.0
 **/
gboolean
fwupd_client_unlock (FwupdClient *client, const gchar *device_id,
		     GCancellable *cancellable, GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_autoptr(FwupdClientHelper) helper = NULL;
	g_autoptr(GVariant) val = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (device_id != NULL, FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return FALSE;

	/* call into daemon */
	helper = fwupd_client_helper_new ();
	g_dbus_proxy_call (priv->proxy,
			   "Unlock",
			   g_variant_new ("(s)", device_id),
			   G_DBUS_CALL_FLAGS_NONE,
			   -1,
			   cancellable,
			   fwupd_client_proxy_call_cb,
			   helper);
	g_main_loop_run (helper->loop);
	if (!helper->ret) {
		g_propagate_error (error, helper->error);
		helper->error = NULL;
		return FALSE;
	}
	return TRUE;
}

/**
 * fwupd_client_clear_results:
 * @client: A #FwupdClient
 * @device_id: the device ID
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Clears the results for a specific device.
 *
 * Returns: %TRUE for success
 *
 * Since: 0.7.0
 **/
gboolean
fwupd_client_clear_results (FwupdClient *client, const gchar *device_id,
			    GCancellable *cancellable, GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_autoptr(FwupdClientHelper) helper = NULL;
	g_autoptr(GVariant) val = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (device_id != NULL, FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return FALSE;

	/* call into daemon */
	helper = fwupd_client_helper_new ();
	g_dbus_proxy_call (priv->proxy,
			   "ClearResults",
			   g_variant_new ("(s)", device_id),
			   G_DBUS_CALL_FLAGS_NONE,
			   -1,
			   cancellable,
			   fwupd_client_proxy_call_cb,
			   helper);
	g_main_loop_run (helper->loop);
	if (!helper->ret) {
		g_propagate_error (error, helper->error);
		helper->error = NULL;
		return FALSE;
	}
	return TRUE;
}

/**
 * fwupd_client_get_results:
 * @client: A #FwupdClient
 * @device_id: the device ID
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Gets the results of a previous firmware update for a specific device.
 *
 * Returns: (transfer full): a #FwupdResult, or %NULL for failure
 *
 * Since: 0.7.0
 **/
FwupdResult *
fwupd_client_get_results (FwupdClient *client, const gchar *device_id,
			  GCancellable *cancellable, GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_autoptr(FwupdClientHelper) helper = NULL;
	g_autoptr(GVariant) val = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), NULL);
	g_return_val_if_fail (device_id != NULL, NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return NULL;

	/* call into daemon */
	helper = fwupd_client_helper_new ();
	g_dbus_proxy_call (priv->proxy,
			   "GetResults",
			   g_variant_new ("(s)", device_id),
			   G_DBUS_CALL_FLAGS_NONE,
			   -1,
			   cancellable,
			   fwupd_client_proxy_call_cb,
			   helper);
	g_main_loop_run (helper->loop);
	if (!helper->ret) {
		g_propagate_error (error, helper->error);
		helper->error = NULL;
		return NULL;
	}
	return fwupd_result_new_from_data (helper->val);
}

/**
 * fwupd_client_send_message_cb:
 **/
static void
fwupd_client_send_message_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *) user_data;
	GDBusConnection *con = G_DBUS_CONNECTION (source_object);
	helper->message = g_dbus_connection_send_message_with_reply_finish (con, res,
									    &helper->error);
	if (helper->message &&
	    !g_dbus_message_to_gerror (helper->message, &helper->error)) {
		helper->ret = TRUE;
		helper->val = g_dbus_message_get_body (helper->message);
		if (helper->val != NULL)
			g_variant_ref (helper->val);
	}
	if (helper->error != NULL)
		fwupd_client_fixup_dbus_error (helper->error);
	g_main_loop_quit (helper->loop);
}

/**
 * fwupd_client_install:
 * @client: A #FwupdClient
 * @device_id: the device ID
 * @filename: the filename to install
 * @install_flags: the #FwupdInstallFlags, e.g. %FWUPD_INSTALL_FLAG_ALLOW_REINSTALL
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Install a file onto a specific device.
 *
 * Returns: %TRUE for success
 *
 * Since: 0.7.0
 **/
gboolean
fwupd_client_install (FwupdClient *client,
		      const gchar *device_id,
		      const gchar *filename,
		      FwupdInstallFlags install_flags,
		      GCancellable *cancellable,
		      GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	GVariant *body;
	GVariantBuilder builder;
	gint retval;
	gint fd;
	g_autoptr(FwupdClientHelper) helper = NULL;
	g_autoptr(GDBusMessage) request = NULL;
	g_autoptr(GUnixFDList) fd_list = NULL;
	g_autoptr(GVariant) val = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (device_id != NULL, FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return FALSE;

	/* set options */
	g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
	g_variant_builder_add (&builder, "{sv}",
			       "reason", g_variant_new_string ("user-action"));
	g_variant_builder_add (&builder, "{sv}",
			       "filename", g_variant_new_string (filename));
	if (install_flags & FWUPD_INSTALL_FLAG_OFFLINE) {
		g_variant_builder_add (&builder, "{sv}",
				       "offline", g_variant_new_boolean (TRUE));
	}
	if (install_flags & FWUPD_INSTALL_FLAG_ALLOW_OLDER) {
		g_variant_builder_add (&builder, "{sv}",
				       "allow-older", g_variant_new_boolean (TRUE));
	}
	if (install_flags & FWUPD_INSTALL_FLAG_ALLOW_REINSTALL) {
		g_variant_builder_add (&builder, "{sv}",
				       "allow-reinstall", g_variant_new_boolean (TRUE));
	}
	if (install_flags & FWUPD_INSTALL_FLAG_FORCE) {
		g_variant_builder_add (&builder, "{sv}",
				       "force", g_variant_new_boolean (TRUE));
	}

	/* open file */
	fd = open (filename, O_RDONLY);
	if (fd < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "failed to open %s",
			     filename);
		return FALSE;
	}

	/* set out of band file descriptor */
	fd_list = g_unix_fd_list_new ();
	retval = g_unix_fd_list_append (fd_list, fd, NULL);
	g_assert (retval != -1);
	request = g_dbus_message_new_method_call (FWUPD_DBUS_SERVICE,
						  FWUPD_DBUS_PATH,
						  FWUPD_DBUS_INTERFACE,
						  "Install");
	g_dbus_message_set_unix_fd_list (request, fd_list);

	/* g_unix_fd_list_append did a dup() already */
	close (fd);

	/* call into daemon */
	helper = fwupd_client_helper_new ();
	body = g_variant_new ("(sha{sv})", device_id, fd > -1 ? 0 : -1, &builder);
	g_dbus_message_set_body (request, body);
	g_dbus_connection_send_message_with_reply (priv->conn,
						   request,
						   G_DBUS_SEND_MESSAGE_FLAGS_NONE,
						   -1,
						   NULL,
						   cancellable,
						   fwupd_client_send_message_cb,
						   helper);
	g_main_loop_run (helper->loop);
	if (!helper->ret) {
		g_propagate_error (error, helper->error);
		helper->error = NULL;
		return FALSE;
	}
	return TRUE;
}

/**
 * fwupd_client_get_details:
 * @client: A #FwupdClient
 * @filename: the firmware filename, e.g. "firmware.cab"
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Gets details about a specific firmware file.
 *
 * Returns: (transfer full): a #FwupdResult, or %NULL for failure
 *
 * Since: 0.7.0
 **/
FwupdResult *
fwupd_client_get_details (FwupdClient *client, const gchar *filename,
			  GCancellable *cancellable, GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	GVariant *body;
	gint fd;
	gint retval;
	g_autoptr(FwupdClientHelper) helper = NULL;
	g_autoptr(GDBusMessage) request = NULL;
	g_autoptr(GUnixFDList) fd_list = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), NULL);
	g_return_val_if_fail (filename != NULL, NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return NULL;

	/* open file */
	fd = open (filename, O_RDONLY);
	if (fd < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "failed to open %s",
			     filename);
		return NULL;
	}

	/* set out of band file descriptor */
	fd_list = g_unix_fd_list_new ();
	retval = g_unix_fd_list_append (fd_list, fd, NULL);
	g_assert (retval != -1);
	request = g_dbus_message_new_method_call (FWUPD_DBUS_SERVICE,
						  FWUPD_DBUS_PATH,
						  FWUPD_DBUS_INTERFACE,
						  "GetDetails");
	g_dbus_message_set_unix_fd_list (request, fd_list);

	/* g_unix_fd_list_append did a dup() already */
	close (fd);

	/* call into daemon */
	helper = fwupd_client_helper_new ();
	body = g_variant_new ("(h)", fd > -1 ? 0 : -1);
	g_dbus_message_set_body (request, body);

	g_dbus_connection_send_message_with_reply (priv->conn,
						   request,
						   G_DBUS_SEND_MESSAGE_FLAGS_NONE,
						   -1,
						   NULL,
						   cancellable,
						   fwupd_client_send_message_cb,
						   helper);
	g_main_loop_run (helper->loop);
	if (!helper->ret) {
		g_propagate_error (error, helper->error);
		helper->error = NULL;
		return NULL;
	}

	/* print results */
	return fwupd_result_new_from_data (helper->val);
}

/**
 * fwupd_client_get_details_local:
 * @client: A #FwupdClient
 * @filename: the firmware filename, e.g. "firmware.cab"
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Gets details about a specific firmware file.
 *
 * Returns: (transfer container) (element-type FwupdResult): an array of results
 *
 * Since: 0.7.2
 **/
GPtrArray *
fwupd_client_get_details_local (FwupdClient *client, const gchar *filename,
				GCancellable *cancellable, GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	GVariant *body;
	gint fd;
	gint retval;
	g_autoptr(FwupdClientHelper) helper = NULL;
	g_autoptr(GDBusMessage) request = NULL;
	g_autoptr(GUnixFDList) fd_list = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), NULL);
	g_return_val_if_fail (filename != NULL, NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return NULL;

	/* open file */
	fd = open (filename, O_RDONLY);
	if (fd < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "failed to open %s",
			     filename);
		return NULL;
	}

	/* set out of band file descriptor */
	fd_list = g_unix_fd_list_new ();
	retval = g_unix_fd_list_append (fd_list, fd, NULL);
	g_assert (retval != -1);
	request = g_dbus_message_new_method_call (FWUPD_DBUS_SERVICE,
						  FWUPD_DBUS_PATH,
						  FWUPD_DBUS_INTERFACE,
						  "GetDetailsLocal");
	g_dbus_message_set_unix_fd_list (request, fd_list);

	/* g_unix_fd_list_append did a dup() already */
	close (fd);

	/* call into daemon */
	helper = fwupd_client_helper_new ();
	body = g_variant_new ("(h)", fd > -1 ? 0 : -1);
	g_dbus_message_set_body (request, body);

	g_dbus_connection_send_message_with_reply (priv->conn,
						   request,
						   G_DBUS_SEND_MESSAGE_FLAGS_NONE,
						   -1,
						   NULL,
						   cancellable,
						   fwupd_client_send_message_cb,
						   helper);
	g_main_loop_run (helper->loop);
	if (!helper->ret) {
		g_propagate_error (error, helper->error);
		helper->error = NULL;
		return NULL;
	}

	/* return results */
	return fwupd_client_parse_results_from_data (helper->val);
}

/**
 * fwupd_client_update_metadata:
 * @client: A #FwupdClient
 * @metadata_fn: the XML metadata filename
 * @signature_fn: the GPG signature file
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Updates the metadata. This allows a session process to download the metadata
 * and metadata signing file to be passed into the daemon to be checked and
 * parsed.
 *
 * Returns: %TRUE for success
 *
 * Since: 0.7.0
 **/
gboolean
fwupd_client_update_metadata (FwupdClient *client,
			      const gchar *metadata_fn,
			      const gchar *signature_fn,
			      GCancellable *cancellable,
			      GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	GVariant *body;
	gint fd;
	gint fd_sig;
	g_autoptr(FwupdClientHelper) helper = NULL;
	g_autoptr(GDBusMessage) request = NULL;
	g_autoptr(GUnixFDList) fd_list = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (metadata_fn != NULL, FALSE);
	g_return_val_if_fail (signature_fn != NULL, FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return FALSE;

	/* open file */
	fd = open (metadata_fn, O_RDONLY);
	if (fd < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "failed to open %s",
			     metadata_fn);
		return FALSE;
	}
	fd_sig = open (signature_fn, O_RDONLY);
	if (fd_sig < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "failed to open %s",
			     signature_fn);
		return FALSE;
	}

	/* set out of band file descriptor */
	fd_list = g_unix_fd_list_new ();
	g_unix_fd_list_append (fd_list, fd, NULL);
	g_unix_fd_list_append (fd_list, fd_sig, NULL);
	request = g_dbus_message_new_method_call (FWUPD_DBUS_SERVICE,
						  FWUPD_DBUS_PATH,
						  FWUPD_DBUS_INTERFACE,
						  "UpdateMetadata");
	g_dbus_message_set_unix_fd_list (request, fd_list);

	/* g_unix_fd_list_append did a dup() already */
	close (fd);
	close (fd_sig);

	/* call into daemon */
	body = g_variant_new ("(hh)", fd, fd_sig);
	g_dbus_message_set_body (request, body);
	helper = fwupd_client_helper_new ();
	g_dbus_connection_send_message_with_reply (priv->conn,
						   request,
						   G_DBUS_SEND_MESSAGE_FLAGS_NONE,
						   -1,
						   NULL,
						   cancellable,
						   fwupd_client_send_message_cb,
						   helper);
	g_main_loop_run (helper->loop);
	if (!helper->ret) {
		g_propagate_error (error, helper->error);
		helper->error = NULL;
		return FALSE;
	}
	return TRUE;
}

/**
 * fwupd_client_get_property:
 **/
static void
fwupd_client_get_property (GObject *object, guint prop_id,
			   GValue *value, GParamSpec *pspec)
{
	FwupdClient *client = FWUPD_CLIENT (object);
	FwupdClientPrivate *priv = GET_PRIVATE (client);

	switch (prop_id) {
	case PROP_STATUS:
		g_value_set_uint (value, priv->status);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * fwupd_client_set_property:
 **/
static void
fwupd_client_set_property (GObject *object, guint prop_id,
			   const GValue *value, GParamSpec *pspec)
{
	FwupdClient *client = FWUPD_CLIENT (object);
	FwupdClientPrivate *priv = GET_PRIVATE (client);

	switch (prop_id) {
	case PROP_STATUS:
		priv->status = g_value_get_uint (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * fwupd_client_class_init:
 **/
static void
fwupd_client_class_init (FwupdClientClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fwupd_client_finalize;
	object_class->get_property = fwupd_client_get_property;
	object_class->set_property = fwupd_client_set_property;

	/**
	 * FwupdClient::changed:
	 * @client: the #FwupdClient instance that emitted the signal
	 *
	 * The ::changed signal is emitted when the daemon internal has
	 * changed, for instance when a device has been added or removed.
	 *
	 * Since: 0.7.0
	 **/
	signals [SIGNAL_CHANGED] =
		g_signal_new ("changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FwupdClientClass, changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	/**
	 * FwupdClient::state-changed:
	 * @client: the #FwupdClient instance that emitted the signal
	 * @status: the #FwupdStatus
	 *
	 * The ::state-changed signal is emitted when the daemon status has
	 * changed, e.g. going from %FWUPD_STATUS_IDLE to %FWUPD_STATUS_DEVICE_WRITE.
	 *
	 * Since: 0.7.0
	 **/
	signals [SIGNAL_STATUS_CHANGED] =
		g_signal_new ("status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FwupdClientClass, status_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	/**
	 * FwupdClient::device-added:
	 * @client: the #FwupdClient instance that emitted the signal
	 * @result: the #FwupdResult
	 *
	 * The ::device-added signal is emitted when a device has been
	 * added.
	 *
	 * Since: 0.7.1
	 **/
	signals [SIGNAL_DEVICE_ADDED] =
		g_signal_new ("device-added",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FwupdClientClass, device_added),
			      NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 1, FWUPD_TYPE_RESULT);

	/**
	 * FwupdClient::device-removed:
	 * @client: the #FwupdClient instance that emitted the signal
	 * @result: the #FwupdResult
	 *
	 * The ::device-removed signal is emitted when a device has been
	 * removed.
	 *
	 * Since: 0.7.1
	 **/
	signals [SIGNAL_DEVICE_REMOVED] =
		g_signal_new ("device-removed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FwupdClientClass, device_removed),
			      NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 1, FWUPD_TYPE_RESULT);

	/**
	 * FwupdClient::device-changed:
	 * @client: the #FwupdClient instance that emitted the signal
	 * @result: the #FwupdResult
	 *
	 * The ::device-changed signal is emitted when a device has been
	 * changed in some way, e.g. the version number is updated.
	 *
	 * Since: 0.7.1
	 **/
	signals [SIGNAL_DEVICE_CHANGED] =
		g_signal_new ("device-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FwupdClientClass, device_changed),
			      NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 1, FWUPD_TYPE_RESULT);

	/**
	 * FwupdClient:status:
	 *
	 * The last-reported status of the daemon.
	 *
	 * Since: 0.7.0
	 */
	pspec = g_param_spec_uint ("status", NULL, NULL,
				   0, FWUPD_STATUS_LAST, FWUPD_STATUS_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_STATUS, pspec);
}

/**
 * fwupd_client_init:
 **/
static void
fwupd_client_init (FwupdClient *client)
{
}

/**
 * fwupd_client_finalize:
 **/
static void
fwupd_client_finalize (GObject *object)
{
	FwupdClient *client = FWUPD_CLIENT (object);
	FwupdClientPrivate *priv = GET_PRIVATE (client);

	if (priv->conn != NULL)
		g_object_unref (priv->conn);
	if (priv->proxy != NULL)
		g_object_unref (priv->proxy);

	G_OBJECT_CLASS (fwupd_client_parent_class)->finalize (object);
}

/**
 * fwupd_client_new:
 *
 * Creates a new client.
 *
 * Returns: a new #FwupdClient
 *
 * Since: 0.7.0
 **/
FwupdClient *
fwupd_client_new (void)
{
	FwupdClient *client;
	client = g_object_new (FWUPD_TYPE_CLIENT, NULL);
	return FWUPD_CLIENT (client);
}
