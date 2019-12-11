/*
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuMain"

#include "config.h"

#include <xmlb.h>
#include <fwupd.h>
#include <gio/gunixfdlist.h>
#include <glib/gi18n.h>
#include <glib-unix.h>
#include <locale.h>
#include <polkit/polkit.h>
#include <stdio.h>
#include <stdlib.h>

#include "fwupd-device-private.h"
#include "fwupd-release-private.h"
#include "fwupd-remote-private.h"
#include "fwupd-resources.h"

#include "fu-common.h"
#include "fu-debug.h"
#include "fu-device-private.h"
#include "fu-engine.h"
#include "fu-install-task.h"

#ifndef HAVE_POLKIT_0_114
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PolkitAuthorizationResult, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PolkitSubject, g_object_unref)
#pragma clang diagnostic pop
#endif

typedef struct {
	GDBusConnection		*connection;
	GDBusNodeInfo		*introspection_daemon;
	GDBusProxy		*proxy_uid;
	GMainLoop		*loop;
	GFileMonitor		*argv0_monitor;
#if GLIB_CHECK_VERSION(2,63,3)
	GMemoryMonitor		*memory_monitor;
#endif
	PolkitAuthority		*authority;
	guint			 owner_id;
	FuEngine		*engine;
	gboolean		 update_in_progress;
	gboolean		 pending_sigterm;
} FuMainPrivate;

static gboolean
fu_main_sigterm_cb (gpointer user_data)
{
	FuMainPrivate *priv = (FuMainPrivate *) user_data;
	if (!priv->update_in_progress) {
		g_main_loop_quit (priv->loop);
		return G_SOURCE_REMOVE;
	}
	g_warning ("Received SIGTERM during a firmware update, ignoring");
	priv->pending_sigterm = TRUE;
	return G_SOURCE_CONTINUE;
}

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
	if (priv->connection == NULL) {
		g_variant_unref (g_variant_ref_sink (property_value));
		return;
	}

	/* build the dict */
	g_variant_builder_init (&invalidated_builder, G_VARIANT_TYPE ("as"));
	g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
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

	/* engine has gone idle */
	if (status == FWUPD_STATUS_SHUTDOWN)
		g_main_loop_quit (priv->loop);
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

static gboolean
fu_main_get_device_flags_for_sender (FuMainPrivate *priv, const char *sender,
				     FwupdDeviceFlags *flags, GError **error)
{
	uid_t calling_uid;
	g_autoptr(GVariant) value = NULL;

	g_return_val_if_fail (sender != NULL, FALSE);
	g_return_val_if_fail (flags != NULL, FALSE);

	value = g_dbus_proxy_call_sync (priv->proxy_uid,
					"GetConnectionUnixUser",
					g_variant_new ("(s)", sender),
					G_DBUS_CALL_FLAGS_NONE,
					2000,
					NULL,
					error);
	if (value == NULL) {
		g_prefix_error (error, "failed to read user id of caller: ");
		return FALSE;
	}
	g_variant_get (value, "(u)", &calling_uid);
	if (calling_uid == 0)
		*flags |= FWUPD_DEVICE_FLAG_TRUSTED;

	return TRUE;
}

static GVariant *
fu_main_device_array_to_variant (FuMainPrivate *priv, const gchar *sender,
				 GPtrArray *devices, GError **error)
{
	GVariantBuilder builder;
	FwupdDeviceFlags flags = FWUPD_DEVICE_FLAG_NONE;

	g_return_val_if_fail (devices->len > 0, NULL);
	g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);

	if (!fu_main_get_device_flags_for_sender (priv, sender, &flags, error))
		return NULL;

	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index (devices, i);
		GVariant *tmp = fwupd_device_to_variant_full (FWUPD_DEVICE (device),
							      flags);
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
	PolkitSubject		*subject;
	GPtrArray		*install_tasks;
	GPtrArray		*action_ids;
	GPtrArray		*checksums;
	guint64			 flags;
	GBytes			*blob_cab;
	FuMainPrivate		*priv;
	gchar			*device_id;
	gchar			*remote_id;
	gchar			*key;
	gchar			*value;
	XbSilo			*silo;
} FuMainAuthHelper;

static void
fu_main_auth_helper_free (FuMainAuthHelper *helper)
{
	if (helper->blob_cab != NULL)
		g_bytes_unref (helper->blob_cab);
	if (helper->subject != NULL)
		g_object_unref (helper->subject);
	if (helper->silo != NULL)
		g_object_unref (helper->silo);
	if (helper->install_tasks != NULL)
		g_ptr_array_unref (helper->install_tasks);
	if (helper->action_ids != NULL)
		g_ptr_array_unref (helper->action_ids);
	if (helper->checksums != NULL)
		g_ptr_array_unref (helper->checksums);
	g_free (helper->device_id);
	g_free (helper->remote_id);
	g_free (helper->key);
	g_free (helper->value);
	g_object_unref (helper->invocation);
	g_free (helper);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuMainAuthHelper, fu_main_auth_helper_free)
#pragma clang diagnostic pop

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
fu_main_authorize_set_approved_firmware_cb (GObject *source, GAsyncResult *res, gpointer user_data)
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

	/* success */
	for (guint i = 0; i < helper->checksums->len; i++) {
		const gchar *csum = g_ptr_array_index (helper->checksums, i);
		fu_engine_add_approved_firmware (helper->priv->engine, csum);
	}
	g_dbus_method_invocation_return_value (helper->invocation, NULL);
}

static void
fu_main_authorize_self_sign_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(FuMainAuthHelper) helper = (FuMainAuthHelper *) user_data;
	g_autofree gchar *sig = NULL;
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
	sig = fu_engine_self_sign (helper->priv->engine, helper->value, helper->flags, &error);
	if (sig == NULL) {
		g_dbus_method_invocation_return_gerror (helper->invocation, error);
		return;
	}

	/* success */
	g_dbus_method_invocation_return_value (helper->invocation, g_variant_new ("(s)", sig));
}

static void
fu_main_modify_config_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(FuMainAuthHelper) helper = (FuMainAuthHelper *) user_data;
	g_autoptr(GError) error = NULL;
	g_autoptr(PolkitAuthorizationResult) auth = NULL;

	/* get result */
	auth = polkit_authority_check_authorization_finish (POLKIT_AUTHORITY (source),
							    res, &error);
	if (!fu_main_authorization_is_valid (auth, &error)) {
		g_dbus_method_invocation_return_gerror (helper->invocation, error);
		return;
	}

	if (!fu_engine_modify_config (helper->priv->engine, helper->key, helper->value, &error)) {
		g_dbus_method_invocation_return_gerror (helper->invocation, error);
		return;
	}

	/* success */
	g_dbus_method_invocation_return_value (helper->invocation, NULL);
}

static void
fu_main_authorize_activate_cb (GObject *source, GAsyncResult *res, gpointer user_data)
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
	if (!fu_engine_activate (helper->priv->engine, helper->device_id, &error)) {
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

static void fu_main_authorize_install_queue (FuMainAuthHelper *helper);

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

	/* do the next authentication action ID */
	fu_main_authorize_install_queue (g_steal_pointer (&helper));
}

static void
fu_main_authorize_install_queue (FuMainAuthHelper *helper_ref)
{
	FuMainPrivate *priv = helper_ref->priv;
	g_autoptr(FuMainAuthHelper) helper = helper_ref;
	g_autoptr(GError) error = NULL;
	gboolean ret;

	/* still more things to to authenticate */
	if (helper->action_ids->len > 0) {
		g_autofree gchar *action_id = g_strdup (g_ptr_array_index (helper->action_ids, 0));
		g_autoptr(PolkitSubject) subject = g_object_ref (helper->subject);
		g_ptr_array_remove_index (helper->action_ids, 0);
		polkit_authority_check_authorization (priv->authority, subject,
						      action_id, NULL,
						      POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
						      NULL,
						      fu_main_authorize_install_cb,
						      g_steal_pointer (&helper));
		return;
	}

	/* all authenticated, so install all the things */
	priv->update_in_progress = TRUE;
	ret = fu_engine_install_tasks (helper->priv->engine,
				       helper->install_tasks,
				       helper->blob_cab,
				       helper->flags,
				       &error);
	priv->update_in_progress = FALSE;
	if (priv->pending_sigterm)
		g_main_loop_quit (priv->loop);
	if (!ret) {
		g_dbus_method_invocation_return_gerror (helper->invocation, error);
		return;
	}

	/* success */
	g_dbus_method_invocation_return_value (helper->invocation, NULL);
}

#if !GLIB_CHECK_VERSION(2,54,0)
static gboolean
g_ptr_array_find (GPtrArray *haystack, gconstpointer needle, guint *index_)
{
	for (guint i = 0; i < haystack->len; i++) {
		gconstpointer *tmp = g_ptr_array_index (haystack, i);
		if (tmp == needle) {
			if (index_ != NULL) {
				*index_ = i;
				return TRUE;
			}
		}
	}
	return FALSE;
}
#endif

static gint
fu_main_install_task_sort_cb (gconstpointer a, gconstpointer b)
{
	FuInstallTask *task_a = *((FuInstallTask **) a);
	FuInstallTask *task_b = *((FuInstallTask **) b);
	return fu_install_task_compare (task_a, task_b);
}

static GPtrArray *
fu_main_get_device_family (FuMainAuthHelper *helper, GError **error)
{
	FuDevice *parent;
	GPtrArray *children;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(GPtrArray) devices_possible = NULL;

	/* get the device */
	device = fu_engine_get_device (helper->priv->engine, helper->device_id, error);
	if (device == NULL)
		return NULL;

	/* device itself */
	devices_possible = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	g_ptr_array_add (devices_possible, g_object_ref (device));

	/* add device children */
	children = fu_device_get_children (device);
	for (guint i = 0; i < children->len; i++) {
		FuDevice *child = g_ptr_array_index (children, i);
		g_ptr_array_add (devices_possible, g_object_ref (child));
	}

	/* add parent and siblings, not including the device itself */
	parent = fu_device_get_parent (device);
	if (parent != NULL) {
		GPtrArray *siblings = fu_device_get_children (parent);
		g_ptr_array_add (devices_possible, g_object_ref (parent));
		for (guint i = 0; i < siblings->len; i++) {
			FuDevice *sibling = g_ptr_array_index (siblings, i);
			if (sibling == device)
				continue;
			g_ptr_array_add (devices_possible, g_object_ref (sibling));
		}
	}

	/* success */
	return g_steal_pointer (&devices_possible);
}

static gboolean
fu_main_install_with_helper (FuMainAuthHelper *helper_ref, GError **error)
{
	FuMainPrivate *priv = helper_ref->priv;
	g_autoptr(FuMainAuthHelper) helper = helper_ref;
	g_autoptr(GPtrArray) components = NULL;
	g_autoptr(GPtrArray) devices_possible = NULL;
	g_autoptr(GPtrArray) errors = NULL;

	/* get a list of devices that in some way match the device_id */
	if (g_strcmp0 (helper->device_id, FWUPD_DEVICE_ID_ANY) == 0) {
		devices_possible = fu_engine_get_devices (priv->engine, error);
		if (devices_possible == NULL)
			return FALSE;
	} else {
		devices_possible = fu_main_get_device_family (helper, error);
		if (devices_possible == NULL)
			return FALSE;
	}

	/* parse silo */
	helper->silo = fu_engine_get_silo_from_blob (priv->engine,
						     helper->blob_cab,
						     error);
	if (helper->silo == NULL)
		return FALSE;

	/* for each component in the silo */
	components = xb_silo_query (helper->silo, "components/component", 0, error);
	if (components == NULL)
		return FALSE;
	helper->action_ids = g_ptr_array_new_with_free_func (g_free);
	helper->install_tasks = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	errors = g_ptr_array_new_with_free_func ((GDestroyNotify) g_error_free);
	for (guint i = 0; i < components->len; i++) {
		XbNode *component = g_ptr_array_index (components, i);

		/* do any devices pass the requirements */
		for (guint j = 0; j < devices_possible->len; j++) {
			FuDevice *device = g_ptr_array_index (devices_possible, j);
			const gchar *action_id;
			g_autoptr(FuInstallTask) task = NULL;
			g_autoptr(GError) error_local = NULL;

			/* is this component valid for the device */
			task = fu_install_task_new (device, component);
			if (!fu_engine_check_requirements (priv->engine,
							   task,
							   helper->flags,
							   &error_local)) {
				g_debug ("requirement on %s:%s failed: %s",
					 fu_device_get_id (device),
					 xb_node_query_text (component, "id", NULL),
					 error_local->message);
				g_ptr_array_add (errors, g_steal_pointer (&error_local));
				continue;
			}

			/* if component should have an update message from CAB */
			fu_device_incorporate_from_component (device, component);

			/* get the action IDs for the valid device */
			action_id = fu_install_task_get_action_id (task);
			if (!g_ptr_array_find (helper->action_ids, action_id, NULL))
				g_ptr_array_add (helper->action_ids, g_strdup (action_id));
			g_ptr_array_add (helper->install_tasks, g_steal_pointer (&task));
		}
	}

	/* order the install tasks by the device priority */
	g_ptr_array_sort (helper->install_tasks, fu_main_install_task_sort_cb);

	/* nothing suitable */
	if (helper->install_tasks->len == 0) {
		GError *error_tmp = fu_common_error_array_get_best (errors);
		g_propagate_error (error, error_tmp);
		return FALSE;
	}

	/* authenticate all things in the action_ids */
	fu_main_set_status (priv, FWUPD_STATUS_WAITING_FOR_AUTH);
	fu_main_authorize_install_queue (g_steal_pointer (&helper));
	return TRUE;
}

static gboolean
fu_main_device_id_valid (const gchar *device_id, GError **error)
{
	if (g_strcmp0 (device_id, FWUPD_DEVICE_ID_ANY) == 0)
		return TRUE;
	if (device_id != NULL && strlen (device_id) >= 4)
		return TRUE;
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_INTERNAL,
		     "invalid device ID: %s",
		     device_id);
	return FALSE;
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

	/* activity */
	fu_engine_idle_reset (priv->engine);

	if (g_strcmp0 (method_name, "GetDevices") == 0) {
		g_autoptr(GPtrArray) devices = NULL;
		g_debug ("Called %s()", method_name);
		devices = fu_engine_get_devices (priv->engine, &error);
		if (devices == NULL) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
		val = fu_main_device_array_to_variant (priv, sender, devices, &error);
		if (val == NULL) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
		g_dbus_method_invocation_return_value (invocation, val);
		return;
	}
	if (g_strcmp0 (method_name, "GetReleases") == 0) {
		const gchar *device_id;
		g_autoptr(GPtrArray) releases = NULL;
		g_variant_get (parameters, "(&s)", &device_id);
		g_debug ("Called %s(%s)", method_name, device_id);
		if (!fu_main_device_id_valid (device_id, &error)) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
		releases = fu_engine_get_releases (priv->engine, device_id, &error);
		if (releases == NULL) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
		val = fu_main_release_array_to_variant (releases);
		g_dbus_method_invocation_return_value (invocation, val);
		return;
	}
	if (g_strcmp0 (method_name, "GetApprovedFirmware") == 0) {
		GVariantBuilder builder;
		GPtrArray *checksums = fu_engine_get_approved_firmware (priv->engine);
		g_variant_builder_init (&builder, G_VARIANT_TYPE ("as"));
		for (guint i = 0; i < checksums->len; i++) {
			const gchar *checksum = g_ptr_array_index (checksums, i);
			g_variant_builder_add_value (&builder, g_variant_new_string (checksum));
		}
		val = g_variant_builder_end (&builder);
		g_dbus_method_invocation_return_value (invocation,
						       g_variant_new_tuple (&val, 1));
		return;
	}
	if (g_strcmp0 (method_name, "SetApprovedFirmware") == 0) {
		g_autofree gchar *checksums_str = NULL;
		g_auto(GStrv) checksums = NULL;
		g_autoptr(FuMainAuthHelper) helper = NULL;
		g_autoptr(PolkitSubject) subject = NULL;

		g_variant_get (parameters, "(^as)", &checksums);
		checksums_str = g_strjoinv (",", checksums);
		g_debug ("Called %s(%s)", method_name, checksums_str);

		/* authenticate */
		fu_main_set_status (priv, FWUPD_STATUS_WAITING_FOR_AUTH);
		helper = g_new0 (FuMainAuthHelper, 1);
		helper->priv = priv;
		helper->invocation = g_object_ref (invocation);
		helper->checksums = g_ptr_array_new_with_free_func (g_free);
		for (guint i = 0; checksums[i] != NULL; i++)
			g_ptr_array_add (helper->checksums, g_strdup (checksums[i]));
		subject = polkit_system_bus_name_new (sender);
		polkit_authority_check_authorization (priv->authority, subject,
						      "org.freedesktop.fwupd.set-approved-firmware",
						      NULL,
						      POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
						      NULL,
						      fu_main_authorize_set_approved_firmware_cb,
						      g_steal_pointer (&helper));
		return;
	}
	if (g_strcmp0 (method_name, "SelfSign") == 0) {
		GVariant *prop_value;
		gchar *prop_key;
		g_autofree gchar *value = NULL;
		g_autoptr(FuMainAuthHelper) helper = NULL;
		g_autoptr(PolkitSubject) subject = NULL;
		g_autoptr(GVariantIter) iter = NULL;

		g_variant_get (parameters, "(sa{sv})", &value, &iter);
		g_debug ("Called %s(%s)", method_name, value);

		/* get flags */
		helper = g_new0 (FuMainAuthHelper, 1);
		while (g_variant_iter_next (iter, "{&sv}", &prop_key, &prop_value)) {
			g_debug ("got option %s", prop_key);
			if (g_strcmp0 (prop_key, "add-timestamp") == 0 &&
			    g_variant_get_boolean (prop_value) == TRUE)
				helper->flags |= FU_KEYRING_SIGN_FLAG_ADD_TIMESTAMP;
			if (g_strcmp0 (prop_key, "add-cert") == 0 &&
			    g_variant_get_boolean (prop_value) == TRUE)
				helper->flags |= FU_KEYRING_SIGN_FLAG_ADD_CERT;
			g_variant_unref (prop_value);
		}

		/* authenticate */
		fu_main_set_status (priv, FWUPD_STATUS_WAITING_FOR_AUTH);
		helper->priv = priv;
		helper->value = g_steal_pointer (&value);
		helper->invocation = g_object_ref (invocation);
		subject = polkit_system_bus_name_new (sender);
		polkit_authority_check_authorization (priv->authority, subject,
						      "org.freedesktop.fwupd.self-sign",
						      NULL,
						      POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
						      NULL,
						      fu_main_authorize_self_sign_cb,
						      g_steal_pointer (&helper));
		return;
	}
	if (g_strcmp0 (method_name, "GetDowngrades") == 0) {
		const gchar *device_id;
		g_autoptr(GPtrArray) releases = NULL;
		g_variant_get (parameters, "(&s)", &device_id);
		g_debug ("Called %s(%s)", method_name, device_id);
		if (!fu_main_device_id_valid (device_id, &error)) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
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
		if (!fu_main_device_id_valid (device_id, &error)) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
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
	if (g_strcmp0 (method_name, "GetHistory") == 0) {
		g_autoptr(GPtrArray) devices = NULL;
		g_debug ("Called %s()", method_name);
		devices = fu_engine_get_history (priv->engine, &error);
		if (devices == NULL) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
		val = fu_main_device_array_to_variant (priv, sender, devices, &error);
		if (val == NULL) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
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
	if (g_strcmp0 (method_name, "ModifyDevice") == 0) {
		const gchar *device_id;
		const gchar *key = NULL;
		const gchar *value = NULL;

		/* check the id exists */
		g_variant_get (parameters, "(&s&s&s)", &device_id, &key, &value);
		g_debug ("Called %s(%s,%s=%s)", method_name, device_id, key, value);
		if (!fu_engine_modify_device (priv->engine, device_id, key, value, &error)) {
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
		if (!fu_main_device_id_valid (device_id, &error)) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
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
			g_prefix_error (&error, "Failed to update metadata for %s: ", remote_id);
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
		g_dbus_method_invocation_return_value (invocation, NULL);
		return;
	}
	if (g_strcmp0 (method_name, "Unlock") == 0) {
		const gchar *device_id = NULL;
		g_autoptr(FuMainAuthHelper) helper = NULL;
		g_autoptr(PolkitSubject) subject = NULL;

		g_variant_get (parameters, "(&s)", &device_id);
		g_debug ("Called %s(%s)", method_name, device_id);
		if (!fu_main_device_id_valid (device_id, &error)) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}

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
						      g_steal_pointer (&helper));
		return;
	}
	if (g_strcmp0 (method_name, "Activate") == 0) {
		const gchar *device_id = NULL;
		g_autoptr(FuMainAuthHelper) helper = NULL;
		g_autoptr(PolkitSubject) subject = NULL;

		g_variant_get (parameters, "(&s)", &device_id);
		g_debug ("Called %s(%s)", method_name, device_id);
		if (!fu_main_device_id_valid (device_id, &error)) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}

		/* authenticate */
		fu_main_set_status (priv, FWUPD_STATUS_WAITING_FOR_AUTH);
		helper = g_new0 (FuMainAuthHelper, 1);
		helper->priv = priv;
		helper->invocation = g_object_ref (invocation);
		helper->device_id = g_strdup (device_id);
		subject = polkit_system_bus_name_new (sender);
		polkit_authority_check_authorization (priv->authority, subject,
						      "org.freedesktop.fwupd.device-activate",
						      NULL,
						      POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
						      NULL,
						      fu_main_authorize_activate_cb,
						      g_steal_pointer (&helper));
		return;
	}
	if (g_strcmp0 (method_name, "ModifyConfig") == 0) {
		g_autofree gchar *key = NULL;
		g_autofree gchar *value = NULL;
		g_autoptr(FuMainAuthHelper) helper = NULL;
		g_autoptr(PolkitSubject) subject = NULL;

		g_variant_get (parameters, "(ss)", &key, &value);
		g_debug ("Called %s(%s=%s)", method_name, key, value);

		/* authenticate */
		helper = g_new0 (FuMainAuthHelper, 1);
		helper->priv = priv;
		helper->key = g_steal_pointer (&key);
		helper->value = g_steal_pointer (&value);
		helper->invocation = g_object_ref (invocation);
		subject = polkit_system_bus_name_new (sender);
		polkit_authority_check_authorization (priv->authority, subject,
						      "org.freedesktop.fwupd.modify-config",
						      NULL,
						      POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
						      NULL,
						      fu_main_modify_config_cb,
						      g_steal_pointer (&helper));
		return;
	}
	if (g_strcmp0 (method_name, "ModifyRemote") == 0) {
		const gchar *remote_id = NULL;
		const gchar *key = NULL;
		const gchar *value = NULL;
		g_autoptr(FuMainAuthHelper) helper = NULL;
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
		polkit_authority_check_authorization (priv->authority, subject,
						      "org.freedesktop.fwupd.modify-remote",
						      NULL,
						      POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
						      NULL,
						      fu_main_authorize_modify_remote_cb,
						      g_steal_pointer (&helper));
		return;
	}
	if (g_strcmp0 (method_name, "VerifyUpdate") == 0) {
		const gchar *device_id = NULL;
		g_autoptr(FuMainAuthHelper) helper = NULL;
		g_autoptr(PolkitSubject) subject = NULL;

		/* check the id exists */
		g_variant_get (parameters, "(&s)", &device_id);
		g_debug ("Called %s(%s)", method_name, device_id);
		if (!fu_main_device_id_valid (device_id, &error)) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}

		/* create helper object */
		helper = g_new0 (FuMainAuthHelper, 1);
		helper->invocation = g_object_ref (invocation);
		helper->device_id = g_strdup (device_id);
		helper->priv = priv;

		/* authenticate */
		fu_main_set_status (priv, FWUPD_STATUS_WAITING_FOR_AUTH);
		subject = polkit_system_bus_name_new (sender);
		polkit_authority_check_authorization (priv->authority, subject,
						      "org.freedesktop.fwupd.verify-update",
						      NULL,
						      POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
						      NULL,
						      fu_main_authorize_verify_update_cb,
						      g_steal_pointer (&helper));
		return;
	}
	if (g_strcmp0 (method_name, "Verify") == 0) {
		const gchar *device_id = NULL;
		g_variant_get (parameters, "(&s)", &device_id);
		g_debug ("Called %s(%s)", method_name, device_id);
		if (!fu_main_device_id_valid (device_id, &error)) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
		if (!fu_engine_verify (priv->engine, device_id, &error)) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
		g_dbus_method_invocation_return_value (invocation, NULL);
		return;
	}
	if (g_strcmp0 (method_name, "Install") == 0) {
		GVariant *prop_value;
		const gchar *device_id = NULL;
		gchar *prop_key;
		gint32 fd_handle = 0;
		gint fd;
		guint64 archive_size_max;
		GDBusMessage *message;
		GUnixFDList *fd_list;
		g_autoptr(FuMainAuthHelper) helper = NULL;
		g_autoptr(GVariantIter) iter = NULL;

		/* check the id exists */
		g_variant_get (parameters, "(&sha{sv})", &device_id, &fd_handle, &iter);
		g_debug ("Called %s(%s,%i)", method_name, device_id, fd_handle);
		if (!fu_main_device_id_valid (device_id, &error)) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}

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
			if (g_strcmp0 (prop_key, "no-history") == 0 &&
			    g_variant_get_boolean (prop_value) == TRUE)
				helper->flags |= FWUPD_INSTALL_FLAG_NO_HISTORY;
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
		archive_size_max = fu_engine_get_archive_size_max (priv->engine);
		helper->blob_cab = fu_common_get_contents_fd (fd, archive_size_max, &error);
		if (helper->blob_cab == NULL) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}

		/* install all the things in the store */
		helper->subject = polkit_system_bus_name_new (sender);
		if (!fu_main_install_with_helper (g_steal_pointer (&helper), &error)) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}

		/* async return */
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

	/* activity */
	fu_engine_idle_reset (priv->engine);

	if (g_strcmp0 (property_name, "DaemonVersion") == 0)
		return g_variant_new_string (VERSION);

	if (g_strcmp0 (property_name, "Tainted") == 0)
		return g_variant_new_boolean (fu_engine_get_tainted (priv->engine));

	if (g_strcmp0 (property_name, "Status") == 0)
		return g_variant_new_uint32 (fu_engine_get_status (priv->engine));

	if (g_strcmp0 (property_name, "HostProduct") == 0)
		return g_variant_new_string (fu_engine_get_host_product (priv->engine));

	if (g_strcmp0 (property_name, "HostMachineId") == 0)
		return g_variant_new_string (fu_engine_get_host_machine_id (priv->engine));

	if (g_strcmp0 (property_name, "Interactive") == 0)
		return g_variant_new_boolean (isatty (fileno (stdout)) != 0);

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

static void
fu_main_argv_changed_cb (GFileMonitor *monitor, GFile *file, GFile *other_file,
			 GFileMonitorEvent event_type, gpointer user_data)
{
	FuMainPrivate *priv = (FuMainPrivate *) user_data;

	/* can do straight away? */
	if (priv->update_in_progress) {
		g_warning ("binary changed during a firmware update, ignoring");
		return;
	}
	g_debug ("binary changed, shutting down");
	g_main_loop_quit (priv->loop);
}

#if GLIB_CHECK_VERSION(2,63,3)
static void
fu_main_memory_monitor_warning_cb (GMemoryMonitor *memory_monitor,
				   GMemoryMonitorWarningLevel level,
				   FuMainPrivate *priv)
{
	/* can do straight away? */
	if (priv->update_in_progress) {
		g_warning ("OOM during a firmware update, ignoring");
		priv->pending_sigterm = TRUE;
		return;
	}
	g_debug ("OOM event, shutting down");
	g_main_loop_quit (priv->loop);
}
#endif

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
	if (priv->argv0_monitor != NULL)
		g_object_unref (priv->argv0_monitor);
	if (priv->introspection_daemon != NULL)
		g_dbus_node_info_unref (priv->introspection_daemon);
#if GLIB_CHECK_VERSION(2,63,3)
	if (priv->memory_monitor != NULL)
		g_object_unref (priv->memory_monitor);
#endif
	g_free (priv);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuMainPrivate, fu_main_private_free)
#pragma clang diagnostic pop

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
	g_autoptr(GFile) argv0_file = g_file_new_for_path (argv[0]);
	g_autoptr(GOptionContext) context = NULL;

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, FWUPD_LOCALEDIR);
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
	priv->engine = fu_engine_new (FU_APP_FLAGS_NONE);
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
	if (!fu_engine_load (priv->engine, FU_ENGINE_LOAD_FLAG_NONE, &error)) {
		g_printerr ("Failed to load engine: %s\n", error->message);
		return EXIT_FAILURE;
	}

	g_unix_signal_add_full (G_PRIORITY_DEFAULT,
				SIGTERM, fu_main_sigterm_cb,
				priv, NULL);

	/* restart the daemon if the binary gets replaced */
	priv->argv0_monitor = g_file_monitor_file (argv0_file, G_FILE_MONITOR_NONE,
						   NULL, &error);
	g_signal_connect (priv->argv0_monitor, "changed",
			  G_CALLBACK (fu_main_argv_changed_cb), priv);

#if GLIB_CHECK_VERSION(2,63,3)
	/* shut down on low memory event as we can just rescan hardware */
	priv->memory_monitor = g_memory_monitor_dup_default ();
	g_signal_connect (G_OBJECT (priv->memory_monitor), "low-memory-warning",
			  G_CALLBACK (fu_main_memory_monitor_warning_cb), priv);
#endif

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

	/* own the object */
	priv->owner_id = g_bus_own_name (G_BUS_TYPE_SYSTEM,
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

	g_debug ("Started with locale %s", g_getenv ("LANG"));

	/* wait */
	g_message ("Daemon ready for requests");
	g_main_loop_run (priv->loop);

	/* success */
	return EXIT_SUCCESS;
}
