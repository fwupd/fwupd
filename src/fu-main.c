/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
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

#include <appstream-glib.h>
#include <fwupd.h>
#include <gio/gunixfdlist.h>
#include <glib/gi18n.h>
#include <locale.h>
#include <polkit/polkit.h>
#include <stdlib.h>

#include "fwupd-device-private.h"
#include "fwupd-release-private.h"
#include "fwupd-remote-private.h"
#include "fwupd-resources.h"

#include "fu-common.h"
#include "fu-debug.h"
#include "fu-device-private.h"
#include "fu-engine.h"

#ifndef HAVE_POLKIT_0_114
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PolkitAuthorizationResult, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PolkitSubject, g_object_unref)
#endif

typedef struct {
	GDBusConnection		*connection;
	GDBusNodeInfo		*introspection_daemon;
	GDBusProxy		*proxy_uid;
	GMainLoop		*loop;
	PolkitAuthority		*authority;
	guint			 owner_id;
	FuEngine		*engine;
} FuMainPrivate;

static void
fu_main_engine_changed_cb (FuEngine *engine, FuMainPrivate *priv)
{
	/* not yet connected */
	if (priv->connection == NULL)
		return;
	g_dbus_connection_emit_signal (priv->connection,
				       NULL,
				       FWUPD_DBUS_PATH,
				       FWUPD_DBUS_INTERFACE,
				       "Changed",
				       NULL, NULL);
}

static void
fu_main_engine_device_added_cb (FuEngine *engine,
				FuDevice *device,
				FuMainPrivate *priv)
{
	GVariant *val;

	/* not yet connected */
	if (priv->connection == NULL)
		return;
	val = fwupd_device_to_variant (FWUPD_DEVICE (device));
	g_dbus_connection_emit_signal (priv->connection,
				       NULL,
				       FWUPD_DBUS_PATH,
				       FWUPD_DBUS_INTERFACE,
				       "DeviceAdded",
				       g_variant_new_tuple (&val, 1), NULL);
}

static void
fu_main_engine_device_removed_cb (FuEngine *engine,
				  FuDevice *device,
				  FuMainPrivate *priv)
{
	GVariant *val;

	/* not yet connected */
	if (priv->connection == NULL)
		return;
	val = fwupd_device_to_variant (FWUPD_DEVICE (device));
	g_dbus_connection_emit_signal (priv->connection,
				       NULL,
				       FWUPD_DBUS_PATH,
				       FWUPD_DBUS_INTERFACE,
				       "DeviceRemoved",
				       g_variant_new_tuple (&val, 1), NULL);
}

static void
fu_main_engine_device_changed_cb (FuEngine *engine,
				  FuDevice *device,
				  FuMainPrivate *priv)
{
	GVariant *val;

	/* not yet connected */
	if (priv->connection == NULL)
		return;
	val = fwupd_device_to_variant (FWUPD_DEVICE (device));
	g_dbus_connection_emit_signal (priv->connection,
				       NULL,
				       FWUPD_DBUS_PATH,
				       FWUPD_DBUS_INTERFACE,
				       "DeviceChanged",
				       g_variant_new_tuple (&val, 1), NULL);
}

static void
fu_main_emit_property_changed (FuMainPrivate *priv,
			       const gchar *property_name,
			       GVariant *property_value)
{
	GVariantBuilder builder;
	GVariantBuilder invalidated_builder;

	/* not yet connected */
	if (priv->connection == NULL)
		return;

	/* build the dict */
	g_variant_builder_init (&invalidated_builder, G_VARIANT_TYPE ("as"));
	g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
	g_variant_builder_add (&builder,
			       "{sv}",
			       property_name,
			       property_value);
	g_dbus_connection_emit_signal (priv->connection,
				       NULL,
				       FWUPD_DBUS_PATH,
				       "org.freedesktop.DBus.Properties",
				       "PropertiesChanged",
				       g_variant_new ("(sa{sv}as)",
				       FWUPD_DBUS_INTERFACE,
				       &builder,
				       &invalidated_builder),
				       NULL);
	g_variant_builder_clear (&builder);
	g_variant_builder_clear (&invalidated_builder);
}

static void
fu_main_set_status (FuMainPrivate *priv, FwupdStatus status)
{
	g_debug ("Emitting PropertyChanged('Status'='%s')",
		 fwupd_status_to_string (status));
	fu_main_emit_property_changed (priv, "Status",
				       g_variant_new_uint32 (status));
}

static void
fu_main_engine_status_changed_cb (FuEngine *engine,
				  FwupdStatus status,
				  FuMainPrivate *priv)
{
	fu_main_set_status (priv, status);
}

static void
fu_main_engine_percentage_changed_cb (FuEngine *engine,
				      guint percentage,
				      FuMainPrivate *priv)
{
	g_debug ("Emitting PropertyChanged('Percentage'='%u%%')", percentage);
	fu_main_emit_property_changed (priv, "Percentage",
				       g_variant_new_uint32 (percentage));
}

static GVariant *
fu_main_device_array_to_variant (GPtrArray *devices)
{
	GVariantBuilder builder;
	g_return_val_if_fail (devices->len > 0, NULL);
	g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index (devices, i);
		GVariant *tmp = fwupd_device_to_variant (FWUPD_DEVICE (device));
		g_variant_builder_add_value (&builder, tmp);
	}
	return g_variant_new ("(aa{sv})", &builder);
}

static GVariant *
fu_main_release_array_to_variant (GPtrArray *results)
{
	GVariantBuilder builder;
	g_return_val_if_fail (results->len > 0, NULL);
	g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
	for (guint i = 0; i < results->len; i++) {
		FwupdRelease *rel = g_ptr_array_index (results, i);
		GVariant *tmp = fwupd_release_to_variant (rel);
		g_variant_builder_add_value (&builder, tmp);
	}
	return g_variant_new ("(aa{sv})", &builder);
}

static GVariant *
fu_main_remote_array_to_variant (GPtrArray *remotes)
{
	GVariantBuilder builder;
	g_return_val_if_fail (remotes->len > 0, NULL);
	g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote = g_ptr_array_index (remotes, i);
		GVariant *tmp = fwupd_remote_to_variant (remote);
		g_variant_builder_add_value (&builder, tmp);
	}
	return g_variant_new ("(aa{sv})", &builder);
}

static GVariant *
fu_main_result_array_to_variant (GPtrArray *results)
{
	GVariantBuilder builder;
	g_return_val_if_fail (results->len > 0, NULL);
	g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
	for (guint i = 0; i < results->len; i++) {
		FwupdDevice *result = g_ptr_array_index (results, i);
		GVariant *tmp = fwupd_device_to_variant (result);
		g_variant_builder_add_value (&builder, tmp);
	}
	return g_variant_new ("(aa{sv})", &builder);
}

typedef struct {
	GDBusMethodInvocation	*invocation;
	AsStore			*store;
	FwupdInstallFlags	 flags;
	GBytes			*blob_cab;
	FuMainPrivate		*priv;
	gchar			*device_id;
	gchar			*remote_id;
	gchar			*key;
	gchar			*value;
} FuMainAuthHelper;

static void
fu_main_auth_helper_free (FuMainAuthHelper *helper)
{
	if (helper->blob_cab != NULL)
		g_bytes_unref (helper->blob_cab);
	if (helper->store != NULL)
		g_object_unref (helper->store);
	g_free (helper->device_id);
	g_free (helper->remote_id);
	g_free (helper->key);
	g_free (helper->value);
	g_object_unref (helper->invocation);
	g_free (helper);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuMainAuthHelper, fu_main_auth_helper_free)

/* error may or may not already have been set */
static gboolean
fu_main_authorization_is_valid (PolkitAuthorizationResult *auth, GError **error)
{
	/* failed */
	if (auth == NULL) {
		g_autofree gchar *message = g_strdup ((*error)->message);
		g_clear_error (error);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_AUTH_FAILED,
			     "Could not check for auth: %s", message);
		return FALSE;
	}

	/* did not auth */
	if (!polkit_authorization_result_get_is_authorized (auth)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_AUTH_FAILED,
				     "Failed to obtain auth");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_main_authorize_unlock_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(FuMainAuthHelper) helper = (FuMainAuthHelper *) user_data;
	g_autoptr(GError) error = NULL;
	g_autoptr(PolkitAuthorizationResult) auth = NULL;


	/* get result */
	fu_main_set_status (helper->priv, FWUPD_STATUS_IDLE);
	auth = polkit_authority_check_authorization_finish (POLKIT_AUTHORITY (source),
							    res, &error);
	if (!fu_main_authorization_is_valid (auth, &error)) {
		g_dbus_method_invocation_return_gerror (helper->invocation, error);
		return;
	}

	/* authenticated */
	if (!fu_engine_unlock (helper->priv->engine, helper->device_id, &error)) {
		g_dbus_method_invocation_return_gerror (helper->invocation, error);
		return;
	}

	/* success */
	g_dbus_method_invocation_return_value (helper->invocation, NULL);
}

static void
fu_main_authorize_verify_update_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(FuMainAuthHelper) helper = (FuMainAuthHelper *) user_data;
	g_autoptr(GError) error = NULL;
	g_autoptr(PolkitAuthorizationResult) auth = NULL;

	/* get result */
	fu_main_set_status (helper->priv, FWUPD_STATUS_IDLE);
	auth = polkit_authority_check_authorization_finish (POLKIT_AUTHORITY (source),
							    res, &error);
	if (!fu_main_authorization_is_valid (auth, &error)) {
		g_dbus_method_invocation_return_gerror (helper->invocation, error);
		return;
	}

	/* authenticated */
	if (!fu_engine_verify_update (helper->priv->engine, helper->device_id, &error)) {
		g_dbus_method_invocation_return_gerror (helper->invocation, error);
		return;
	}

	/* success */
	g_dbus_method_invocation_return_value (helper->invocation, NULL);
}

static void
fu_main_authorize_modify_remote_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(FuMainAuthHelper) helper = (FuMainAuthHelper *) user_data;
	g_autoptr(GError) error = NULL;
	g_autoptr(PolkitAuthorizationResult) auth = NULL;

	/* get result */
	fu_main_set_status (helper->priv, FWUPD_STATUS_IDLE);
	auth = polkit_authority_check_authorization_finish (POLKIT_AUTHORITY (source),
							    res, &error);
	if (!fu_main_authorization_is_valid (auth, &error)) {
		g_dbus_method_invocation_return_gerror (helper->invocation, error);
		return;
	}

	/* authenticated */
	if (!fu_engine_modify_remote (helper->priv->engine,
				      helper->remote_id,
				      helper->key,
				      helper->value,
				      &error)) {
		g_dbus_method_invocation_return_gerror (helper->invocation, error);
		return;
	}

	/* success */
	g_dbus_method_invocation_return_value (helper->invocation, NULL);
}

static void
fu_main_authorize_install_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(FuMainAuthHelper) helper = (FuMainAuthHelper *) user_data;
	g_autoptr(GError) error = NULL;
	g_autoptr(PolkitAuthorizationResult) auth = NULL;

	/* get result */
	fu_main_set_status (helper->priv, FWUPD_STATUS_IDLE);
	auth = polkit_authority_check_authorization_finish (POLKIT_AUTHORITY (source),
							    res, &error);
	if (!fu_main_authorization_is_valid (auth, &error)) {
		g_dbus_method_invocation_return_gerror (helper->invocation, error);
		return;
	}

	/* authenticated */
	if (!fu_engine_install (helper->priv->engine,
				helper->device_id,
				helper->store,
				helper->blob_cab,
				helper->flags,
				&error)) {
		g_dbus_method_invocation_return_gerror (helper->invocation, error);
		return;
	}

	/* success */
	g_dbus_method_invocation_return_value (helper->invocation, NULL);
}

static void
fu_main_daemon_method_call (GDBusConnection *connection, const gchar *sender,
			    const gchar *object_path, const gchar *interface_name,
			    const gchar *method_name, GVariant *parameters,
			    GDBusMethodInvocation *invocation, gpointer user_data)
{
	FuMainPrivate *priv = (FuMainPrivate *) user_data;
	GVariant *val = NULL;
	g_autoptr(GError) error = NULL;

	if (g_strcmp0 (method_name, "GetDevices") == 0) {
		g_autoptr(GPtrArray) devices = NULL;
		g_debug ("Called %s()", method_name);
		devices = fu_engine_get_devices (priv->engine, &error);
		if (devices == NULL) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
		val = fu_main_device_array_to_variant (devices);
		g_dbus_method_invocation_return_value (invocation, val);
		return;
	}
	if (g_strcmp0 (method_name, "GetReleases") == 0) {
		const gchar *device_id;
		g_autoptr(GPtrArray) releases = NULL;
		g_variant_get (parameters, "(&s)", &device_id);
		g_debug ("Called %s(%s)", method_name, device_id);
		releases = fu_engine_get_releases (priv->engine, device_id, &error);
		if (releases == NULL) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
		val = fu_main_release_array_to_variant (releases);
		g_dbus_method_invocation_return_value (invocation, val);
		return;
	}
	if (g_strcmp0 (method_name, "GetDowngrades") == 0) {
		const gchar *device_id;
		g_autoptr(GPtrArray) releases = NULL;
		g_variant_get (parameters, "(&s)", &device_id);
		g_debug ("Called %s(%s)", method_name, device_id);
		releases = fu_engine_get_downgrades (priv->engine, device_id, &error);
		if (releases == NULL) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
		val = fu_main_release_array_to_variant (releases);
		g_dbus_method_invocation_return_value (invocation, val);
		return;
	}
	if (g_strcmp0 (method_name, "GetUpgrades") == 0) {
		const gchar *device_id;
		g_autoptr(GPtrArray) releases = NULL;
		g_variant_get (parameters, "(&s)", &device_id);
		g_debug ("Called %s(%s)", method_name, device_id);
		releases = fu_engine_get_upgrades (priv->engine, device_id, &error);
		if (releases == NULL) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
		val = fu_main_release_array_to_variant (releases);
		g_dbus_method_invocation_return_value (invocation, val);
		return;
	}
	if (g_strcmp0 (method_name, "GetRemotes") == 0) {
		g_autoptr(GPtrArray) remotes = NULL;
		g_debug ("Called %s()", method_name);
		remotes = fu_engine_get_remotes (priv->engine, &error);
		if (remotes == NULL) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
		val = fu_main_remote_array_to_variant (remotes);
		g_dbus_method_invocation_return_value (invocation, val);
		return;
	}
	if (g_strcmp0 (method_name, "ClearResults") == 0) {
		const gchar *device_id;
		g_variant_get (parameters, "(&s)", &device_id);
		g_debug ("Called %s(%s)", method_name, device_id);
		if (!fu_engine_clear_results (priv->engine, device_id, &error)) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
		g_dbus_method_invocation_return_value (invocation, NULL);
		return;
	}
	if (g_strcmp0 (method_name, "GetResults") == 0) {
		const gchar *device_id = NULL;
		g_autoptr(FwupdDevice) result = NULL;
		g_variant_get (parameters, "(&s)", &device_id);
		g_debug ("Called %s(%s)", method_name, device_id);
		result = fu_engine_get_results (priv->engine, device_id, &error);
		if (result == NULL) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
		val = fwupd_device_to_variant (result);
		g_dbus_method_invocation_return_value (invocation,
						       g_variant_new_tuple (&val, 1));
		return;
	}
	if (g_strcmp0 (method_name, "UpdateMetadata") == 0) {
		GDBusMessage *message;
		GUnixFDList *fd_list;
		const gchar *remote_id = NULL;
		gint fd_data;
		gint fd_sig;

		g_variant_get (parameters, "(&shh)", &remote_id, &fd_data, &fd_sig);
		g_debug ("Called %s(%s,%i,%i)", method_name, remote_id, fd_data, fd_sig);

		/* update the metadata store */
		message = g_dbus_method_invocation_get_message (invocation);
		fd_list = g_dbus_message_get_unix_fd_list (message);
		if (fd_list == NULL || g_unix_fd_list_get_length (fd_list) != 2) {
			g_set_error (&error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "invalid handle");
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
		fd_data = g_unix_fd_list_get (fd_list, 0, &error);
		if (fd_data < 0) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
		fd_sig = g_unix_fd_list_get (fd_list, 1, &error);
		if (fd_sig < 0) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}

		/* store new metadata (will close the fds when done) */
		if (!fu_engine_update_metadata (priv->engine, remote_id,
						fd_data, fd_sig, &error)) {
			g_prefix_error (&error, "Failed to update metadata: ");
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
		g_dbus_method_invocation_return_value (invocation, NULL);
		return;
	}
	if (g_strcmp0 (method_name, "Unlock") == 0) {
		FuMainAuthHelper *helper;
		const gchar *device_id = NULL;
		g_autoptr(PolkitSubject) subject = NULL;

		g_variant_get (parameters, "(&s)", &device_id);
		g_debug ("Called %s(%s)", method_name, device_id);

		/* authenticate */
		fu_main_set_status (priv, FWUPD_STATUS_WAITING_FOR_AUTH);
		helper = g_new0 (FuMainAuthHelper, 1);
		helper->priv = priv;
		helper->invocation = g_object_ref (invocation);
		helper->device_id = g_strdup (device_id);
		subject = polkit_system_bus_name_new (sender);
		polkit_authority_check_authorization (priv->authority, subject,
						      "org.freedesktop.fwupd.device-unlock",
						      NULL,
						      POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
						      NULL,
						      fu_main_authorize_unlock_cb,
						      helper);
		return;
	}
	if (g_strcmp0 (method_name, "ModifyRemote") == 0) {
		FuMainAuthHelper *helper;
		const gchar *remote_id = NULL;
		const gchar *key = NULL;
		const gchar *value = NULL;
		g_autoptr(PolkitSubject) subject = NULL;

		/* check the id exists */
		g_variant_get (parameters, "(&s&s&s)", &remote_id, &key, &value);
		g_debug ("Called %s(%s,%s=%s)", method_name, remote_id, key, value);

		/* create helper object */
		helper = g_new0 (FuMainAuthHelper, 1);
		helper->invocation = g_object_ref (invocation);
		helper->remote_id = g_strdup (remote_id);
		helper->key = g_strdup (key);
		helper->value = g_strdup (value);
		helper->priv = priv;

		/* authenticate */
		fu_main_set_status (priv, FWUPD_STATUS_WAITING_FOR_AUTH);
		subject = polkit_system_bus_name_new (sender);
		polkit_authority_check_authorization (helper->priv->authority, subject,
						      "org.freedesktop.fwupd.modify-remote",
						      NULL,
						      POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
						      NULL,
						      fu_main_authorize_modify_remote_cb,
						      helper);
		return;
	}
	if (g_strcmp0 (method_name, "VerifyUpdate") == 0) {
		FuMainAuthHelper *helper;
		const gchar *device_id = NULL;
		g_autoptr(PolkitSubject) subject = NULL;

		/* check the id exists */
		g_variant_get (parameters, "(&s)", &device_id);
		g_debug ("Called %s(%s)", method_name, device_id);

		/* create helper object */
		helper = g_new0 (FuMainAuthHelper, 1);
		helper->invocation = g_object_ref (invocation);
		helper->device_id = g_strdup (device_id);
		helper->priv = priv;

		/* authenticate */
		fu_main_set_status (priv, FWUPD_STATUS_WAITING_FOR_AUTH);
		subject = polkit_system_bus_name_new (sender);
		polkit_authority_check_authorization (helper->priv->authority, subject,
						      "org.freedesktop.fwupd.verify-update",
						      NULL,
						      POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
						      NULL,
						      fu_main_authorize_verify_update_cb,
						      helper);
		return;
	}
	if (g_strcmp0 (method_name, "Verify") == 0) {
		const gchar *device_id = NULL;
		g_variant_get (parameters, "(&s)", &device_id);
		g_debug ("Called %s(%s)", method_name, device_id);
		if (!fu_engine_verify (priv->engine, device_id, &error)) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
		g_dbus_method_invocation_return_value (invocation, NULL);
		return;
	}
	if (g_strcmp0 (method_name, "Install") == 0) {
		FuMainAuthHelper *helper;
		GVariant *prop_value;
		const gchar *action_id;
		const gchar *device_id = NULL;
		gchar *prop_key;
		gint32 fd_handle = 0;
		gint fd;
		GDBusMessage *message;
		GUnixFDList *fd_list;
		g_autoptr(PolkitSubject) subject = NULL;
		g_autoptr(GVariantIter) iter = NULL;

		/* check the id exists */
		g_variant_get (parameters, "(&sha{sv})", &device_id, &fd_handle, &iter);
		g_debug ("Called %s(%s,%i)", method_name, device_id, fd_handle);

		/* create helper object */
		helper = g_new0 (FuMainAuthHelper, 1);
		helper->invocation = g_object_ref (invocation);
		helper->device_id = g_strdup (device_id);
		helper->priv = priv;

		/* get flags */
		while (g_variant_iter_next (iter, "{&sv}", &prop_key, &prop_value)) {
			g_debug ("got option %s", prop_key);
			if (g_strcmp0 (prop_key, "offline") == 0 &&
			    g_variant_get_boolean (prop_value) == TRUE)
				helper->flags |= FWUPD_INSTALL_FLAG_OFFLINE;
			if (g_strcmp0 (prop_key, "allow-older") == 0 &&
			    g_variant_get_boolean (prop_value) == TRUE)
				helper->flags |= FWUPD_INSTALL_FLAG_ALLOW_OLDER;
			if (g_strcmp0 (prop_key, "allow-reinstall") == 0 &&
			    g_variant_get_boolean (prop_value) == TRUE)
				helper->flags |= FWUPD_INSTALL_FLAG_ALLOW_REINSTALL;
			if (g_strcmp0 (prop_key, "force") == 0 &&
			    g_variant_get_boolean (prop_value) == TRUE)
				helper->flags |= FWUPD_INSTALL_FLAG_FORCE;
			g_variant_unref (prop_value);
		}


		/* get the fd */
		message = g_dbus_method_invocation_get_message (invocation);
		fd_list = g_dbus_message_get_unix_fd_list (message);
		if (fd_list == NULL || g_unix_fd_list_get_length (fd_list) != 1) {
			g_set_error (&error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "invalid handle");
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
		fd = g_unix_fd_list_get (fd_list, 0, &error);
		if (fd < 0) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}

		/* parse the cab file before authenticating so we can work out
		 * what action ID to use, for instance, if this is trusted --
		 * this will also close the fd when done */
		helper->blob_cab = fu_common_get_contents_fd (fd, FU_ENGINE_FIRMWARE_SIZE_MAX, &error);
		if (helper->blob_cab == NULL) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
		helper->store = fu_engine_get_store_from_blob (priv->engine,
							       helper->blob_cab,
							       &error);
		if (helper->store == NULL) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}

		/* authenticate */
		action_id = fu_engine_get_action_id_for_device (priv->engine,
								helper->device_id,
								helper->store,
								helper->flags,
								&error);
		if (action_id == NULL) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
		fu_main_set_status (priv, FWUPD_STATUS_WAITING_FOR_AUTH);
		subject = polkit_system_bus_name_new (sender);
		polkit_authority_check_authorization (priv->authority, subject,
						      action_id, NULL,
						      POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
						      NULL,
						      fu_main_authorize_install_cb,
						      helper);
		return;
	}
	if (g_strcmp0 (method_name, "GetDetails") == 0) {
		GDBusMessage *message;
		GUnixFDList *fd_list;
		gint32 fd_handle = 0;
		gint fd;
		g_autoptr(GPtrArray) results = NULL;

		/* get parameters */
		g_variant_get (parameters, "(h)", &fd_handle);
		g_debug ("Called %s(%i)", method_name, fd_handle);

		/* get the fd */
		message = g_dbus_method_invocation_get_message (invocation);
		fd_list = g_dbus_message_get_unix_fd_list (message);
		if (fd_list == NULL || g_unix_fd_list_get_length (fd_list) != 1) {
			g_set_error (&error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "invalid handle");
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
		fd = g_unix_fd_list_get (fd_list, 0, &error);
		if (fd < 0) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}

		/* get details about the file (will close the fd when done) */
		results = fu_engine_get_details (priv->engine, fd, &error);
		if (results == NULL) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
		val = fu_main_result_array_to_variant (results);
		g_dbus_method_invocation_return_value (invocation, val);
		return;
	}
	g_set_error (&error,
		     G_DBUS_ERROR,
		     G_DBUS_ERROR_UNKNOWN_METHOD,
		     "no such method %s", method_name);
	g_dbus_method_invocation_return_gerror (invocation, error);
}

static GVariant *
fu_main_daemon_get_property (GDBusConnection *connection_, const gchar *sender,
			     const gchar *object_path, const gchar *interface_name,
			     const gchar *property_name, GError **error,
			     gpointer user_data)
{
	FuMainPrivate *priv = (FuMainPrivate *) user_data;

	if (g_strcmp0 (property_name, "DaemonVersion") == 0)
		return g_variant_new_string (VERSION);

	if (g_strcmp0 (property_name, "Status") == 0)
		return g_variant_new_uint32 (fu_engine_get_status (priv->engine));

	/* return an error */
	g_set_error (error,
		     G_DBUS_ERROR,
		     G_DBUS_ERROR_UNKNOWN_PROPERTY,
		     "failed to get daemon property %s",
		     property_name);
	return NULL;
}

static void
fu_main_on_bus_acquired_cb (GDBusConnection *connection,
			    const gchar *name,
			    gpointer user_data)
{
	FuMainPrivate *priv = (FuMainPrivate *) user_data;
	guint registration_id;
	g_autoptr(GError) error = NULL;
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

	/* connect to D-Bus directly */
	priv->proxy_uid =
		g_dbus_proxy_new_sync (priv->connection,
				       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
				       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
				       NULL,
				       "org.freedesktop.DBus",
				       "/org/freedesktop/DBus",
				       "org.freedesktop.DBus",
				       NULL,
				       &error);
	if (priv->proxy_uid == NULL) {
		g_warning ("cannot connect to DBus: %s", error->message);
		return;
	}

	/* dump startup profile data */
	if (fu_debug_is_verbose ())
		fu_engine_profile_dump (priv->engine);
}

static void
fu_main_on_name_acquired_cb (GDBusConnection *connection,
			     const gchar *name,
			     gpointer user_data)
{
	g_debug ("FuMain: acquired name: %s", name);
}

static void
fu_main_on_name_lost_cb (GDBusConnection *connection,
			 const gchar *name,
			 gpointer user_data)
{
	FuMainPrivate *priv = (FuMainPrivate *) user_data;
	g_debug ("FuMain: lost name: %s", name);
	g_main_loop_quit (priv->loop);
}

static gboolean
fu_main_timed_exit_cb (gpointer user_data)
{
	GMainLoop *loop = (GMainLoop *) user_data;
	g_main_loop_quit (loop);
	return G_SOURCE_REMOVE;
}

static GDBusNodeInfo *
fu_main_load_introspection (const gchar *filename, GError **error)
{
	g_autoptr(GBytes) data = NULL;
	g_autofree gchar *path = NULL;

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

static gboolean
fu_main_perhaps_own_name (gpointer user_data)
{
	FuMainPrivate *priv = (FuMainPrivate *) user_data;
	g_autoptr(GError) error = NULL;

	/* are any plugins pending */
	if (!fu_engine_check_plugins_pending (priv->engine, &error)) {
		g_debug ("trying again: %s", error->message);
		return G_SOURCE_CONTINUE;
	}

	/* own the object */
	g_debug ("registering D-Bus service");
	priv->owner_id = g_bus_own_name (G_BUS_TYPE_SYSTEM,
					 FWUPD_DBUS_SERVICE,
					 G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
					 G_BUS_NAME_OWNER_FLAGS_REPLACE,
					 fu_main_on_bus_acquired_cb,
					 fu_main_on_name_acquired_cb,
					 fu_main_on_name_lost_cb,
					 priv, NULL);
	return G_SOURCE_REMOVE;
}

static void
fu_main_private_free (FuMainPrivate *priv)
{
	if (priv->loop != NULL)
		g_main_loop_unref (priv->loop);
	if (priv->owner_id > 0)
		g_bus_unown_name (priv->owner_id);
	if (priv->proxy_uid != NULL)
		g_object_unref (priv->proxy_uid);
	if (priv->engine != NULL)
		g_object_unref (priv->engine);
	if (priv->connection != NULL)
		g_object_unref (priv->connection);
	if (priv->authority != NULL)
		g_object_unref (priv->authority);
	if (priv->introspection_daemon != NULL)
		g_dbus_node_info_unref (priv->introspection_daemon);
	g_free (priv);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuMainPrivate, fu_main_private_free)

int
main (int argc, char *argv[])
{
	gboolean immediate_exit = FALSE;
	gboolean timed_exit = FALSE;
	const GOptionEntry options[] = {
		{ "timed-exit", '\0', 0, G_OPTION_ARG_NONE, &timed_exit,
		  /* TRANSLATORS: exit after we've started up, used for user profiling */
		  _("Exit after a small delay"), NULL },
		{ "immediate-exit", '\0', 0, G_OPTION_ARG_NONE, &immediate_exit,
		  /* TRANSLATORS: exit straight away, used for automatic profiling */
		  _("Exit after the engine has loaded"), NULL },
		{ NULL}
	};
	g_autoptr(FuMainPrivate) priv = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GOptionContext) context = NULL;

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* TRANSLATORS: program name */
	g_set_application_name (_("Firmware Update Daemon"));
	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_add_group (context, fu_debug_get_option_group ());
	/* TRANSLATORS: program summary */
	g_option_context_set_summary (context, _("Firmware Update D-Bus Service"));
	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		g_printerr ("Failed to parse command line: %s\n", error->message);
		return EXIT_FAILURE;
	}

	/* create new objects */
	priv = g_new0 (FuMainPrivate, 1);
	priv->loop = g_main_loop_new (NULL, FALSE);

	/* load engine */
	priv->engine = fu_engine_new ();
	g_signal_connect (priv->engine, "changed",
			  G_CALLBACK (fu_main_engine_changed_cb),
			  priv);
	g_signal_connect (priv->engine, "device-added",
			  G_CALLBACK (fu_main_engine_device_added_cb),
			  priv);
	g_signal_connect (priv->engine, "device-removed",
			  G_CALLBACK (fu_main_engine_device_removed_cb),
			  priv);
	g_signal_connect (priv->engine, "device-changed",
			  G_CALLBACK (fu_main_engine_device_changed_cb),
			  priv);
	g_signal_connect (priv->engine, "status-changed",
			  G_CALLBACK (fu_main_engine_status_changed_cb),
			  priv);
	g_signal_connect (priv->engine, "percentage-changed",
			  G_CALLBACK (fu_main_engine_percentage_changed_cb),
			  priv);
	if (!fu_engine_load (priv->engine, &error)) {
		g_printerr ("Failed to load engine: %s\n", error->message);
		return EXIT_FAILURE;
	}

	/* keep polling until all the plugins are ready */
	g_timeout_add (200, fu_main_perhaps_own_name, priv);

	/* load introspection from file */
	priv->introspection_daemon = fu_main_load_introspection (FWUPD_DBUS_INTERFACE ".xml",
								 &error);
	if (priv->introspection_daemon == NULL) {
		g_printerr ("Failed to load introspection: %s\n", error->message);
		return EXIT_FAILURE;
	}

	/* get authority */
	priv->authority = polkit_authority_get_sync (NULL, &error);
	if (priv->authority == NULL) {
		g_printerr ("Failed to load authority: %s\n", error->message);
		return EXIT_FAILURE;
	}

	/* Only timeout and close the mainloop if we have specified it
	 * on the command line */
	if (immediate_exit)
		g_idle_add (fu_main_timed_exit_cb, priv->loop);
	else if (timed_exit)
		g_timeout_add_seconds (5, fu_main_timed_exit_cb, priv->loop);

	/* wait */
	g_message ("Daemon ready for requests");
	g_main_loop_run (priv->loop);

	/* success */
	return EXIT_SUCCESS;
}
