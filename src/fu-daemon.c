/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuMain"

#include "config.h"

#include <fwupd.h>
#ifdef HAVE_GIO_UNIX
#include <gio/gunixfdlist.h>
#endif
#include <glib/gstdio.h>
#include <xmlb.h>
#ifdef HAVE_POLKIT
#include <polkit/polkit.h>
#endif
#include <jcat.h>

#include "fwupd-device-private.h"
#include "fwupd-enums-private.h"
#include "fwupd-plugin-private.h"
#include "fwupd-release-private.h"
#include "fwupd-remote-private.h"
#include "fwupd-request-private.h"
#include "fwupd-resources.h"
#include "fwupd-security-attr-private.h"

#include "fu-common.h"
#include "fu-daemon.h"
#include "fu-device-private.h"
#include "fu-engine.h"
#include "fu-release.h"
#include "fu-security-attrs-private.h"

#ifdef HAVE_POLKIT
#ifndef HAVE_POLKIT_0_114
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PolkitAuthorizationResult, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PolkitSubject, g_object_unref)
#pragma clang diagnostic pop
#endif /* HAVE_POLKIT_0_114 */
#endif /* HAVE_POLKIT */

static void
fu_daemon_finalize(GObject *obj);

struct _FuDaemon {
	GObject parent_instance;
	GDBusConnection *connection;
	GDBusNodeInfo *introspection_daemon;
	GDBusProxy *proxy_uid;
	GMainLoop *loop;
	GHashTable *sender_items; /* sender:FuDaemonSenderItem */
#ifdef HAVE_POLKIT
	PolkitAuthority *authority;
#endif
	FwupdStatus status; /* last emitted */
	guint owner_id;
	guint process_quit_id;
	FuEngine *engine;
	gboolean update_in_progress;
	gboolean pending_stop;
	FuDaemonMachineKind machine_kind;
};

G_DEFINE_TYPE(FuDaemon, fu_daemon, G_TYPE_OBJECT)

void
fu_daemon_start(FuDaemon *self)
{
	g_return_if_fail(FU_IS_DAEMON(self));
	g_main_loop_run(self->loop);
}

void
fu_daemon_stop(FuDaemon *self)
{
	g_return_if_fail(FU_IS_DAEMON(self));
	if (self->update_in_progress) {
		self->pending_stop = TRUE;
		return;
	}
	g_main_loop_quit(self->loop);
}

typedef struct {
	FwupdFeatureFlags feature_flags;
	GHashTable *hints; /* str:str */
} FuDaemonSenderItem;

static FuDaemonMachineKind
fu_daemon_machine_kind_from_string(const gchar *kind)
{
	if (g_strcmp0(kind, "physical") == 0)
		return FU_DAEMON_MACHINE_KIND_PHYSICAL;
	if (g_strcmp0(kind, "virtual") == 0)
		return FU_DAEMON_MACHINE_KIND_VIRTUAL;
	if (g_strcmp0(kind, "container") == 0)
		return FU_DAEMON_MACHINE_KIND_CONTAINER;
	return FU_DAEMON_MACHINE_KIND_UNKNOWN;
}

static void
fu_daemon_engine_changed_cb(FuEngine *engine, FuDaemon *self)
{
	/* not yet connected */
	if (self->connection == NULL)
		return;
	g_dbus_connection_emit_signal(self->connection,
				      NULL,
				      FWUPD_DBUS_PATH,
				      FWUPD_DBUS_INTERFACE,
				      "Changed",
				      NULL,
				      NULL);
}

static void
fu_daemon_engine_device_added_cb(FuEngine *engine, FuDevice *device, FuDaemon *self)
{
	GVariant *val;

	/* not yet connected */
	if (self->connection == NULL)
		return;
	val = fwupd_device_to_variant(FWUPD_DEVICE(device));
	g_dbus_connection_emit_signal(self->connection,
				      NULL,
				      FWUPD_DBUS_PATH,
				      FWUPD_DBUS_INTERFACE,
				      "DeviceAdded",
				      g_variant_new_tuple(&val, 1),
				      NULL);
}

static void
fu_daemon_engine_device_removed_cb(FuEngine *engine, FuDevice *device, FuDaemon *self)
{
	GVariant *val;

	/* not yet connected */
	if (self->connection == NULL)
		return;
	val = fwupd_device_to_variant(FWUPD_DEVICE(device));
	g_dbus_connection_emit_signal(self->connection,
				      NULL,
				      FWUPD_DBUS_PATH,
				      FWUPD_DBUS_INTERFACE,
				      "DeviceRemoved",
				      g_variant_new_tuple(&val, 1),
				      NULL);
}

static void
fu_daemon_engine_device_changed_cb(FuEngine *engine, FuDevice *device, FuDaemon *self)
{
	GVariant *val;

	/* not yet connected */
	if (self->connection == NULL)
		return;
	val = fwupd_device_to_variant(FWUPD_DEVICE(device));
	g_dbus_connection_emit_signal(self->connection,
				      NULL,
				      FWUPD_DBUS_PATH,
				      FWUPD_DBUS_INTERFACE,
				      "DeviceChanged",
				      g_variant_new_tuple(&val, 1),
				      NULL);
}

static void
fu_daemon_engine_device_request_cb(FuEngine *engine, FwupdRequest *request, FuDaemon *self)
{
	GVariant *val;

	/* not yet connected */
	if (self->connection == NULL)
		return;
	val = fwupd_request_to_variant(FWUPD_REQUEST(request));
	g_dbus_connection_emit_signal(self->connection,
				      NULL,
				      FWUPD_DBUS_PATH,
				      FWUPD_DBUS_INTERFACE,
				      "DeviceRequest",
				      g_variant_new_tuple(&val, 1),
				      NULL);
}

static void
fu_daemon_emit_property_changed(FuDaemon *self,
				const gchar *property_name,
				GVariant *property_value)
{
	GVariantBuilder builder;
	GVariantBuilder invalidated_builder;

	/* not yet connected */
	if (self->connection == NULL) {
		g_variant_unref(g_variant_ref_sink(property_value));
		return;
	}

	/* build the dict */
	g_variant_builder_init(&invalidated_builder, G_VARIANT_TYPE("as"));
	g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add(&builder, "{sv}", property_name, property_value);
	g_dbus_connection_emit_signal(
	    self->connection,
	    NULL,
	    FWUPD_DBUS_PATH,
	    "org.freedesktop.DBus.Properties",
	    "PropertiesChanged",
	    g_variant_new("(sa{sv}as)", FWUPD_DBUS_INTERFACE, &builder, &invalidated_builder),
	    NULL);
	g_variant_builder_clear(&builder);
	g_variant_builder_clear(&invalidated_builder);
}

static void
fu_daemon_set_status(FuDaemon *self, FwupdStatus status)
{
	/* sanity check */
	if (self->status == status)
		return;
	self->status = status;

	g_debug("Emitting PropertyChanged('Status'='%s')", fwupd_status_to_string(status));
	fu_daemon_emit_property_changed(self, "Status", g_variant_new_uint32(status));
}

static void
fu_daemon_engine_status_changed_cb(FuEngine *engine, FwupdStatus status, FuDaemon *self)
{
	fu_daemon_set_status(self, status);

	/* engine has gone idle */
	if (status == FWUPD_STATUS_SHUTDOWN)
		g_main_loop_quit(self->loop);
}

static FuEngineRequest *
fu_daemon_create_request(FuDaemon *self, const gchar *sender, GError **error)
{
	FuDaemonSenderItem *sender_item;
	FwupdDeviceFlags device_flags = FWUPD_DEVICE_FLAG_NONE;
	guint64 calling_uid = 0;
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(FU_ENGINE_REQUEST_KIND_ACTIVE);
	g_autoptr(GVariant) value = NULL;

	/* if using FWUPD_DBUS_SOCKET... */
	if (sender == NULL) {
		fu_engine_request_set_device_flags(request, FWUPD_DEVICE_FLAG_TRUSTED);
		return g_steal_pointer(&request);
	}

	g_return_val_if_fail(sender != NULL, NULL);

	/* did the client set the list of supported features or any hints */
	sender_item = g_hash_table_lookup(self->sender_items, sender);
	if (sender_item != NULL) {
		const gchar *locale = g_hash_table_lookup(sender_item->hints, "locale");
		if (locale != NULL)
			fu_engine_request_set_locale(request, locale);
		fu_engine_request_set_feature_flags(request, sender_item->feature_flags);
	}

	/* are we root and therefore trusted? */
	value = g_dbus_proxy_call_sync(self->proxy_uid,
				       "GetConnectionUnixUser",
				       g_variant_new("(s)", sender),
				       G_DBUS_CALL_FLAGS_NONE,
				       2000,
				       NULL,
				       error);
	if (value == NULL) {
		g_prefix_error(error, "failed to read user id of caller: ");
		return NULL;
	}
	g_variant_get(value, "(u)", &calling_uid);
	if (fu_engine_is_uid_trusted(self->engine, calling_uid))
		device_flags |= FWUPD_DEVICE_FLAG_TRUSTED;
	fu_engine_request_set_device_flags(request, device_flags);

	/* success */
	return g_steal_pointer(&request);
}

static GVariant *
fu_daemon_device_array_to_variant(FuDaemon *self,
				  FuEngineRequest *request,
				  GPtrArray *devices,
				  GError **error)
{
	GVariantBuilder builder;
	FwupdDeviceFlags flags = fu_engine_request_get_device_flags(request);

	g_return_val_if_fail(devices->len > 0, NULL);
	g_variant_builder_init(&builder, G_VARIANT_TYPE_ARRAY);

	/* override when required */
	if (fu_engine_get_show_device_private(self->engine))
		flags |= FWUPD_DEVICE_FLAG_TRUSTED;
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		GVariant *tmp = fwupd_device_to_variant_full(FWUPD_DEVICE(device), flags);
		g_variant_builder_add_value(&builder, tmp);
	}
	return g_variant_new("(aa{sv})", &builder);
}

static GVariant *
fu_daemon_plugin_array_to_variant(GPtrArray *plugins)
{
	GVariantBuilder builder;

	g_return_val_if_fail(plugins->len > 0, NULL);
	g_variant_builder_init(&builder, G_VARIANT_TYPE_ARRAY);

	for (guint i = 0; i < plugins->len; i++) {
		FuDevice *plugin = g_ptr_array_index(plugins, i);
		GVariant *tmp = fwupd_plugin_to_variant(FWUPD_PLUGIN(plugin));
		g_variant_builder_add_value(&builder, tmp);
	}
	return g_variant_new("(aa{sv})", &builder);
}

static GVariant *
fu_daemon_release_array_to_variant(GPtrArray *results)
{
	GVariantBuilder builder;
	g_return_val_if_fail(results->len > 0, NULL);
	g_variant_builder_init(&builder, G_VARIANT_TYPE_ARRAY);
	for (guint i = 0; i < results->len; i++) {
		FwupdRelease *rel = g_ptr_array_index(results, i);
		GVariant *tmp = fwupd_release_to_variant(rel);
		g_variant_builder_add_value(&builder, tmp);
	}
	return g_variant_new("(aa{sv})", &builder);
}

static GVariant *
fu_daemon_remote_array_to_variant(GPtrArray *remotes)
{
	GVariantBuilder builder;
	g_return_val_if_fail(remotes->len > 0, NULL);
	g_variant_builder_init(&builder, G_VARIANT_TYPE_ARRAY);
	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote = g_ptr_array_index(remotes, i);
		GVariant *tmp = fwupd_remote_to_variant(remote);
		g_variant_builder_add_value(&builder, tmp);
	}
	return g_variant_new("(aa{sv})", &builder);
}

#ifdef HAVE_GIO_UNIX
static GVariant *
fu_daemon_result_array_to_variant(GPtrArray *results)
{
	GVariantBuilder builder;
	g_return_val_if_fail(results->len > 0, NULL);
	g_variant_builder_init(&builder, G_VARIANT_TYPE_ARRAY);
	for (guint i = 0; i < results->len; i++) {
		FwupdDevice *result = g_ptr_array_index(results, i);
		GVariant *tmp = fwupd_device_to_variant(result);
		g_variant_builder_add_value(&builder, tmp);
	}
	return g_variant_new("(aa{sv})", &builder);
}
#endif /* HAVE_GIO_UNIX */

typedef struct {
	GDBusMethodInvocation *invocation;
	FuEngineRequest *request;
#ifdef HAVE_POLKIT
	PolkitSubject *subject;
#endif
	GPtrArray *releases;
	GPtrArray *action_ids;
	GPtrArray *checksums;
	GPtrArray *errors;
	guint64 flags;
	GBytes *blob_cab;
	FuDaemon *self;
	gchar *device_id;
	gchar *remote_id;
	gchar *key;
	gchar *value;
	XbSilo *silo;
} FuMainAuthHelper;

static void
fu_daemon_auth_helper_free(FuMainAuthHelper *helper)
{
	/* always return to IDLE even in event of an auth error */
	fu_daemon_set_status(helper->self, FWUPD_STATUS_IDLE);

	if (helper->blob_cab != NULL)
		g_bytes_unref(helper->blob_cab);
#ifdef HAVE_POLKIT
	if (helper->subject != NULL)
		g_object_unref(helper->subject);
#endif
	if (helper->silo != NULL)
		g_object_unref(helper->silo);
	if (helper->request != NULL)
		g_object_unref(helper->request);
	if (helper->releases != NULL)
		g_ptr_array_unref(helper->releases);
	if (helper->action_ids != NULL)
		g_ptr_array_unref(helper->action_ids);
	if (helper->checksums != NULL)
		g_ptr_array_unref(helper->checksums);
	if (helper->errors != NULL)
		g_ptr_array_unref(helper->errors);
	g_free(helper->device_id);
	g_free(helper->remote_id);
	g_free(helper->key);
	g_free(helper->value);
	g_object_unref(helper->invocation);
	g_free(helper);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuMainAuthHelper, fu_daemon_auth_helper_free)
#pragma clang diagnostic pop

#ifdef HAVE_POLKIT
/* error may or may not already have been set */
static gboolean
fu_daemon_authorization_is_valid(PolkitAuthorizationResult *auth, GError **error)
{
	/* failed */
	if (auth == NULL) {
		g_autofree gchar *message = g_strdup((*error)->message);
		g_clear_error(error);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_AUTH_FAILED,
			    "Could not check for auth: %s",
			    message);
		return FALSE;
	}

	/* did not auth */
	if (!polkit_authorization_result_get_is_authorized(auth)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_AUTH_FAILED,
				    "Failed to obtain auth");
		return FALSE;
	}

	/* success */
	return TRUE;
}
#else
static gboolean
fu_daemon_authorization_is_trusted(FuEngineRequest *request, GError **error)
{
	FwupdDeviceFlags flags = fu_engine_request_get_device_flags(request);
	if ((flags & FWUPD_DEVICE_FLAG_TRUSTED) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_AUTH_FAILED,
				    "permission denied: untrusted client process");
		return FALSE;
	}
	return TRUE;
}
#endif /* HAVE_POLKIT */

static void
fu_daemon_authorize_unlock_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(FuMainAuthHelper) helper = (FuMainAuthHelper *)user_data;
	g_autoptr(GError) error = NULL;
#ifdef HAVE_POLKIT
	g_autoptr(PolkitAuthorizationResult) auth = NULL;

	/* get result */
	auth = polkit_authority_check_authorization_finish(POLKIT_AUTHORITY(source), res, &error);
	if (!fu_daemon_authorization_is_valid(auth, &error)) {
		g_dbus_method_invocation_return_gerror(helper->invocation, error);
		return;
	}
#else
	if (!fu_daemon_authorization_is_trusted(helper->request, &error)) {
		g_dbus_method_invocation_return_gerror(helper->invocation, error);
		return;
	}
#endif /* HAVE_POLKIT */

	/* authenticated */
	if (!fu_engine_unlock(helper->self->engine, helper->device_id, &error)) {
		g_dbus_method_invocation_return_gerror(helper->invocation, error);
		return;
	}

	/* success */
	g_dbus_method_invocation_return_value(helper->invocation, NULL);
}

static void
fu_daemon_authorize_set_approved_firmware_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(FuMainAuthHelper) helper = (FuMainAuthHelper *)user_data;
	g_autoptr(GError) error = NULL;
#ifdef HAVE_POLKIT
	g_autoptr(PolkitAuthorizationResult) auth = NULL;

	/* get result */
	auth = polkit_authority_check_authorization_finish(POLKIT_AUTHORITY(source), res, &error);
	if (!fu_daemon_authorization_is_valid(auth, &error)) {
		g_dbus_method_invocation_return_gerror(helper->invocation, error);
		return;
	}
#else
	if (!fu_daemon_authorization_is_trusted(helper->request, &error)) {
		g_dbus_method_invocation_return_gerror(helper->invocation, error);
		return;
	}
#endif /* HAVE_POLKIT */

	/* success */
	for (guint i = 0; i < helper->checksums->len; i++) {
		const gchar *csum = g_ptr_array_index(helper->checksums, i);
		fu_engine_add_approved_firmware(helper->self->engine, csum);
	}
	g_dbus_method_invocation_return_value(helper->invocation, NULL);
}

static void
fu_daemon_authorize_set_blocked_firmware_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(FuMainAuthHelper) helper = (FuMainAuthHelper *)user_data;
	g_autoptr(GError) error = NULL;
#ifdef HAVE_POLKIT
	g_autoptr(PolkitAuthorizationResult) auth = NULL;

	/* get result */
	auth = polkit_authority_check_authorization_finish(POLKIT_AUTHORITY(source), res, &error);
	if (!fu_daemon_authorization_is_valid(auth, &error)) {
		g_dbus_method_invocation_return_gerror(helper->invocation, error);
		return;
	}
#else
	if (!fu_daemon_authorization_is_trusted(helper->request, &error)) {
		g_dbus_method_invocation_return_gerror(helper->invocation, error);
		return;
	}
#endif /* HAVE_POLKIT */

	/* success */
	if (!fu_engine_set_blocked_firmware(helper->self->engine, helper->checksums, &error)) {
		g_dbus_method_invocation_return_gerror(helper->invocation, error);
		return;
	}
	g_dbus_method_invocation_return_value(helper->invocation, NULL);
}

static void
fu_daemon_authorize_self_sign_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(FuMainAuthHelper) helper = (FuMainAuthHelper *)user_data;
	g_autofree gchar *sig = NULL;
	g_autoptr(GError) error = NULL;
#ifdef HAVE_POLKIT
	g_autoptr(PolkitAuthorizationResult) auth = NULL;

	/* get result */
	auth = polkit_authority_check_authorization_finish(POLKIT_AUTHORITY(source), res, &error);
	if (!fu_daemon_authorization_is_valid(auth, &error)) {
		g_dbus_method_invocation_return_gerror(helper->invocation, error);
		return;
	}
#else
	if (!fu_daemon_authorization_is_trusted(helper->request, &error)) {
		g_dbus_method_invocation_return_gerror(helper->invocation, error);
		return;
	}
#endif /* HAVE_POLKIT */

	/* authenticated */
	sig = fu_engine_self_sign(helper->self->engine, helper->value, helper->flags, &error);
	if (sig == NULL) {
		g_dbus_method_invocation_return_gerror(helper->invocation, error);
		return;
	}

	/* success */
	g_dbus_method_invocation_return_value(helper->invocation, g_variant_new("(s)", sig));
}

static void
fu_daemon_modify_config_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(FuMainAuthHelper) helper = (FuMainAuthHelper *)user_data;
	g_autoptr(GError) error = NULL;
#ifdef HAVE_POLKIT
	g_autoptr(PolkitAuthorizationResult) auth = NULL;

	/* get result */
	auth = polkit_authority_check_authorization_finish(POLKIT_AUTHORITY(source), res, &error);
	if (!fu_daemon_authorization_is_valid(auth, &error)) {
		g_dbus_method_invocation_return_gerror(helper->invocation, error);
		return;
	}
#else
	if (!fu_daemon_authorization_is_trusted(helper->request, &error)) {
		g_dbus_method_invocation_return_gerror(helper->invocation, error);
		return;
	}
#endif /* HAVE_POLKIT */

	if (!fu_engine_modify_config(helper->self->engine, helper->key, helper->value, &error)) {
		g_dbus_method_invocation_return_gerror(helper->invocation, error);
		return;
	}

	/* success */
	g_dbus_method_invocation_return_value(helper->invocation, NULL);
}

static void
fu_daemon_progress_percentage_changed_cb(FuProgress *progress, guint percentage, FuDaemon *self)
{
	g_debug("Emitting PropertyChanged('Percentage'='%u%%')", percentage);
	fu_daemon_emit_property_changed(self, "Percentage", g_variant_new_uint32(percentage));
}

static void
fu_daemon_progress_status_changed_cb(FuProgress *progress, FwupdStatus status, FuDaemon *self)
{
	fu_daemon_set_status(self, status);
}

static void
fu_daemon_authorize_activate_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(FuMainAuthHelper) helper = (FuMainAuthHelper *)user_data;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
#ifdef HAVE_POLKIT
	g_autoptr(PolkitAuthorizationResult) auth = NULL;

	/* get result */
	auth = polkit_authority_check_authorization_finish(POLKIT_AUTHORITY(source), res, &error);
	if (!fu_daemon_authorization_is_valid(auth, &error)) {
		g_dbus_method_invocation_return_gerror(helper->invocation, error);
		return;
	}
#endif /* HAVE_POLKIT */

	/* progress */
	fu_progress_set_profile(progress, g_getenv("FWUPD_VERBOSE") != NULL);
	g_signal_connect(FU_PROGRESS(progress),
			 "percentage-changed",
			 G_CALLBACK(fu_daemon_progress_percentage_changed_cb),
			 helper->self);
	g_signal_connect(FU_PROGRESS(progress),
			 "status-changed",
			 G_CALLBACK(fu_daemon_progress_status_changed_cb),
			 helper->self);

	/* authenticated */
	if (!fu_engine_activate(helper->self->engine, helper->device_id, progress, &error)) {
		g_dbus_method_invocation_return_gerror(helper->invocation, error);
		return;
	}

	/* success */
	g_dbus_method_invocation_return_value(helper->invocation, NULL);
}

static void
fu_daemon_authorize_verify_update_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(FuMainAuthHelper) helper = (FuMainAuthHelper *)user_data;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
#ifdef HAVE_POLKIT
	g_autoptr(PolkitAuthorizationResult) auth = NULL;

	/* get result */
	auth = polkit_authority_check_authorization_finish(POLKIT_AUTHORITY(source), res, &error);
	if (!fu_daemon_authorization_is_valid(auth, &error)) {
		g_dbus_method_invocation_return_gerror(helper->invocation, error);
		return;
	}
#else
	if (!fu_daemon_authorization_is_trusted(helper->request, &error)) {
		g_dbus_method_invocation_return_gerror(helper->invocation, error);
		return;
	}
#endif /* HAVE_POLKIT */

	/* progress */
	fu_progress_set_profile(progress, g_getenv("FWUPD_VERBOSE") != NULL);
	g_signal_connect(FU_PROGRESS(progress),
			 "percentage-changed",
			 G_CALLBACK(fu_daemon_progress_percentage_changed_cb),
			 helper->self);
	g_signal_connect(FU_PROGRESS(progress),
			 "status-changed",
			 G_CALLBACK(fu_daemon_progress_status_changed_cb),
			 helper->self);

	/* authenticated */
	if (!fu_engine_verify_update(helper->self->engine, helper->device_id, progress, &error)) {
		g_dbus_method_invocation_return_gerror(helper->invocation, error);
		return;
	}

	/* success */
	g_dbus_method_invocation_return_value(helper->invocation, NULL);
}

static void
fu_daemon_authorize_modify_remote_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(FuMainAuthHelper) helper = (FuMainAuthHelper *)user_data;
	g_autoptr(GError) error = NULL;
#ifdef HAVE_POLKIT
	g_autoptr(PolkitAuthorizationResult) auth = NULL;

	/* get result */
	auth = polkit_authority_check_authorization_finish(POLKIT_AUTHORITY(source), res, &error);
	if (!fu_daemon_authorization_is_valid(auth, &error)) {
		g_dbus_method_invocation_return_gerror(helper->invocation, error);
		return;
	}
#else
	if (!fu_daemon_authorization_is_trusted(helper->request, &error)) {
		g_dbus_method_invocation_return_gerror(helper->invocation, error);
		return;
	}
#endif /* HAVE_POLKIT */

	/* authenticated */
	if (!fu_engine_modify_remote(helper->self->engine,
				     helper->remote_id,
				     helper->key,
				     helper->value,
				     &error)) {
		g_dbus_method_invocation_return_gerror(helper->invocation, error);
		return;
	}

	/* success */
	g_dbus_method_invocation_return_value(helper->invocation, NULL);
}

#ifdef HAVE_GIO_UNIX
static void
fu_daemon_authorize_install_queue(FuMainAuthHelper *helper);

#ifdef HAVE_POLKIT
static void
fu_daemon_authorize_install_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(FuMainAuthHelper) helper = (FuMainAuthHelper *)user_data;
	g_autoptr(GError) error = NULL;
	g_autoptr(PolkitAuthorizationResult) auth = NULL;

	/* get result */
	auth = polkit_authority_check_authorization_finish(POLKIT_AUTHORITY(source), res, &error);
	if (!fu_daemon_authorization_is_valid(auth, &error)) {
		g_dbus_method_invocation_return_gerror(helper->invocation, error);
		return;
	}

	/* do the next authentication action ID */
	fu_daemon_authorize_install_queue(g_steal_pointer(&helper));
}
#endif /* HAVE_POLKIT */

static void
fu_daemon_authorize_install_queue(FuMainAuthHelper *helper_ref)
{
	FuDaemon *self = helper_ref->self;
	g_autoptr(FuMainAuthHelper) helper = helper_ref;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	gboolean ret;

#ifdef HAVE_POLKIT
	/* still more things to to authenticate */
	if (helper->action_ids->len > 0) {
		g_autofree gchar *action_id = g_strdup(g_ptr_array_index(helper->action_ids, 0));
		g_autoptr(PolkitSubject) subject = g_object_ref(helper->subject);
		g_ptr_array_remove_index(helper->action_ids, 0);
		polkit_authority_check_authorization(
		    self->authority,
		    subject,
		    action_id,
		    NULL,
		    POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
		    NULL,
		    fu_daemon_authorize_install_cb,
		    g_steal_pointer(&helper));
		return;
	}
#endif /* HAVE_POLKIT */

	/* all authenticated, so install all the things */
	fu_progress_set_profile(progress, g_getenv("FWUPD_VERBOSE") != NULL);
	g_signal_connect(FU_PROGRESS(progress),
			 "percentage-changed",
			 G_CALLBACK(fu_daemon_progress_percentage_changed_cb),
			 helper->self);
	g_signal_connect(FU_PROGRESS(progress),
			 "status-changed",
			 G_CALLBACK(fu_daemon_progress_status_changed_cb),
			 helper->self);

	/* all authenticated, so install all the things */
	self->update_in_progress = TRUE;
	ret = fu_engine_install_releases(helper->self->engine,
					 helper->request,
					 helper->releases,
					 helper->blob_cab,
					 progress,
					 helper->flags,
					 &error);
	self->update_in_progress = FALSE;
	if (self->pending_stop)
		g_main_loop_quit(self->loop);
	if (!ret) {
		g_dbus_method_invocation_return_gerror(helper->invocation, error);
		return;
	}

	/* success */
	g_dbus_method_invocation_return_value(helper->invocation, NULL);
}
#endif /* HAVE_GIO_UNIX */

#if !GLIB_CHECK_VERSION(2, 54, 0)
static gboolean
g_ptr_array_find(GPtrArray *haystack, gconstpointer needle, guint *index_)
{
	for (guint i = 0; i < haystack->len; i++) {
		gconstpointer *tmp = g_ptr_array_index(haystack, i);
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

#ifdef HAVE_GIO_UNIX
static gint
fu_daemon_release_sort_cb(gconstpointer a, gconstpointer b)
{
	FuRelease *release1 = *((FuRelease **)a);
	FuRelease *release2 = *((FuRelease **)b);
	return fu_release_compare(release1, release2);
}

static gboolean
fu_daemon_install_with_helper_device(FuMainAuthHelper *helper,
				     XbNode *component,
				     FuDevice *device,
				     GError **error)
{
	FuDaemon *self = helper->self;
	const gchar *action_id;
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) releases = NULL;

	/* is this component valid for the device */
	fu_release_set_device(release, device);
	fu_release_set_request(release, helper->request);
	if (!fu_release_load(release,
			     component,
			     NULL,
			     helper->flags | FWUPD_INSTALL_FLAG_FORCE,
			     &error_local)) {
		g_ptr_array_add(helper->errors, g_steal_pointer(&error_local));
		return TRUE;
	}
	if (!fu_engine_check_requirements(self->engine,
					  release,
					  helper->flags | FWUPD_INSTALL_FLAG_FORCE,
					  &error_local)) {
		if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
			g_debug("first pass requirement on %s:%s failed: %s",
				fu_device_get_id(device),
				xb_node_query_text(component, "id", NULL),
				error_local->message);
		}
		g_ptr_array_add(helper->errors, g_steal_pointer(&error_local));
		return TRUE;
	}

	/* possibly update version format */
	fu_engine_md_refresh_device_from_component(self->engine, device, component);

	/* sync update message from CAB */
	fu_device_incorporate_from_component(device, component);

	/* install each intermediate release */
	releases = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_INSTALL_ALL_RELEASES)) {
		g_autoptr(GPtrArray) rels = NULL;
#if LIBXMLB_CHECK_VERSION(0, 2, 0)
		g_autoptr(XbQuery) query = NULL;
#endif
		/* we get this one "for free" */
		g_ptr_array_add(releases, g_object_ref(release));

#if LIBXMLB_CHECK_VERSION(0, 2, 0)
		query = xb_query_new_full(xb_node_get_silo(component),
					  "releases/release",
					  XB_QUERY_FLAG_FORCE_NODE_CACHE,
					  error);
		if (query == NULL)
			return FALSE;
		rels = xb_node_query_full(component, query, NULL);
#else
		rels = xb_node_query(component, "releases/release", 0, NULL);
#endif
		/* add all but the the first entry */
		for (guint i = 1; i < rels->len; i++) {
			XbNode *rel = g_ptr_array_index(rels, i);
			g_autoptr(FuRelease) release2 = fu_release_new();
			g_autoptr(GError) error_loop = NULL;
			fu_release_set_device(release2, device);
			fu_release_set_request(release2, helper->request);
			if (!fu_release_load(release2,
					     component,
					     rel,
					     helper->flags,
					     &error_loop)) {
				g_ptr_array_add(helper->errors, g_steal_pointer(&error_loop));
				continue;
			}
			g_ptr_array_add(releases, g_object_ref(release2));
		}
	} else {
		g_ptr_array_add(releases, g_object_ref(release));
	}

	/* make a second pass */
	for (guint i = 0; i < releases->len; i++) {
		FuRelease *release_tmp = g_ptr_array_index(releases, i);
		if (!fu_engine_check_requirements(self->engine,
						  release_tmp,
						  helper->flags,
						  &error_local)) {
			g_debug("second pass requirement on %s:%s failed: %s",
				fu_device_get_id(device),
				xb_node_query_text(component, "id", NULL),
				error_local->message);
			g_ptr_array_add(helper->errors, g_steal_pointer(&error_local));
			continue;
		}
		if (!fu_engine_check_trust(self->engine, release_tmp, &error_local)) {
			g_ptr_array_add(helper->errors, g_steal_pointer(&error_local));
			continue;
		}

		/* get the action IDs for the valid device */
		action_id = fu_release_get_action_id(release_tmp);
		if (!g_ptr_array_find(helper->action_ids, action_id, NULL))
			g_ptr_array_add(helper->action_ids, g_strdup(action_id));
		g_ptr_array_add(helper->releases, g_object_ref(release_tmp));
	}

	/* success */
	return TRUE;
}

static gboolean
fu_daemon_install_with_helper(FuMainAuthHelper *helper_ref, GError **error)
{
	FuDaemon *self = helper_ref->self;
	g_autoptr(FuMainAuthHelper) helper = helper_ref;
	g_autoptr(GPtrArray) components = NULL;
	g_autoptr(GPtrArray) devices_possible = NULL;

	/* get a list of devices that in some way match the device_id */
	if (g_strcmp0(helper->device_id, FWUPD_DEVICE_ID_ANY) == 0) {
		devices_possible = fu_engine_get_devices(self->engine, error);
		if (devices_possible == NULL)
			return FALSE;
	} else {
		g_autoptr(FuDevice) device = NULL;
		device = fu_engine_get_device(self->engine, helper->device_id, error);
		if (device == NULL)
			return FALSE;
		devices_possible =
		    fu_engine_get_devices_by_composite_id(self->engine,
							  fu_device_get_composite_id(device),
							  error);
		if (devices_possible == NULL)
			return FALSE;
	}

	/* parse silo */
	helper->silo = fu_engine_get_silo_from_blob(self->engine, helper->blob_cab, error);
	if (helper->silo == NULL)
		return FALSE;

	/* for each component in the silo */
	components =
	    xb_silo_query(helper->silo, "components/component[@type='firmware']", 0, error);
	if (components == NULL)
		return FALSE;
	helper->action_ids = g_ptr_array_new_with_free_func(g_free);
	helper->releases = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	helper->errors = g_ptr_array_new_with_free_func((GDestroyNotify)g_error_free);

	/* do any devices pass the requirements */
	for (guint i = 0; i < components->len; i++) {
		XbNode *component = g_ptr_array_index(components, i);
		for (guint j = 0; j < devices_possible->len; j++) {
			FuDevice *device = g_ptr_array_index(devices_possible, j);
			if (!fu_daemon_install_with_helper_device(helper, component, device, error))
				return FALSE;
		}
	}

	/* order the install tasks by the device priority */
	g_ptr_array_sort(helper->releases, fu_daemon_release_sort_cb);

	/* nothing suitable */
	if (helper->releases->len == 0) {
		GError *error_tmp = fu_common_error_array_get_best(helper->errors);
		g_propagate_error(error, error_tmp);
		return FALSE;
	}

	/* authenticate all things in the action_ids */
	fu_daemon_set_status(self, FWUPD_STATUS_WAITING_FOR_AUTH);
	fu_daemon_authorize_install_queue(g_steal_pointer(&helper));
	return TRUE;
}
#endif /* HAVE_GIO_UNIX */

static FuDaemonSenderItem *
fu_daemon_ensure_sender_item(FuDaemon *self, const gchar *sender)
{
	FuDaemonSenderItem *sender_item = NULL;

	/* operating in point-to-point mode */
	if (sender == NULL)
		sender = "";
	sender_item = g_hash_table_lookup(self->sender_items, sender);
	if (sender_item == NULL) {
		sender_item = g_new0(FuDaemonSenderItem, 1);
		sender_item->hints = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
		g_hash_table_insert(self->sender_items, g_strdup(sender), sender_item);
	}
	return sender_item;
}

static gboolean
fu_daemon_device_id_valid(const gchar *device_id, GError **error)
{
	if (g_strcmp0(device_id, FWUPD_DEVICE_ID_ANY) == 0)
		return TRUE;
	if (device_id != NULL && strlen(device_id) >= 4)
		return TRUE;
	g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "invalid device ID: %s", device_id);
	return FALSE;
}

static gboolean
fu_daemon_schedule_process_quit_cb(gpointer user_data)
{
	FuDaemon *self = FU_DAEMON(user_data);

	g_debug("daemon asked to quit, shutting down");
	self->process_quit_id = 0;
	g_main_loop_quit(self->loop);
	return G_SOURCE_REMOVE;
}

static void
fu_daemon_schedule_process_quit(FuDaemon *self)
{
	/* busy? */
	if (self->update_in_progress) {
		g_warning("asked to quit during a firmware update, ignoring");
		return;
	}

	/* allow the daemon to respond to the request, then quit */
	if (self->process_quit_id != 0)
		g_source_remove(self->process_quit_id);
	self->process_quit_id = g_idle_add(fu_daemon_schedule_process_quit_cb, self);
}

static void
fu_daemon_daemon_method_call(GDBusConnection *connection,
			     const gchar *sender,
			     const gchar *object_path,
			     const gchar *interface_name,
			     const gchar *method_name,
			     GVariant *parameters,
			     GDBusMethodInvocation *invocation,
			     gpointer user_data)
{
	FuDaemon *self = FU_DAEMON(user_data);
	GVariant *val = NULL;
	g_autoptr(FuEngineRequest) request = NULL;
	g_autoptr(GError) error = NULL;

	/* build request */
	request = fu_daemon_create_request(self, sender, &error);
	if (request == NULL) {
		g_dbus_method_invocation_return_gerror(invocation, error);
		return;
	}

	/* activity */
	fu_engine_idle_reset(self->engine);

	if (g_strcmp0(method_name, "GetDevices") == 0) {
		g_autoptr(GPtrArray) devices = NULL;
		g_debug("Called %s()", method_name);
		devices = fu_engine_get_devices(self->engine, &error);
		if (devices == NULL) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}
		val = fu_daemon_device_array_to_variant(self, request, devices, &error);
		if (val == NULL) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}
		g_dbus_method_invocation_return_value(invocation, val);
		return;
	}
	if (g_strcmp0(method_name, "GetPlugins") == 0) {
		g_debug("Called %s()", method_name);
		val = fu_daemon_plugin_array_to_variant(fu_engine_get_plugins(self->engine));
		g_dbus_method_invocation_return_value(invocation, val);
		return;
	}
	if (g_strcmp0(method_name, "GetReleases") == 0) {
		const gchar *device_id;
		g_autoptr(GPtrArray) releases = NULL;
		g_variant_get(parameters, "(&s)", &device_id);
		g_debug("Called %s(%s)", method_name, device_id);
		if (!fu_daemon_device_id_valid(device_id, &error)) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}
		releases = fu_engine_get_releases(self->engine, request, device_id, &error);
		if (releases == NULL) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}
		val = fu_daemon_release_array_to_variant(releases);
		g_dbus_method_invocation_return_value(invocation, val);
		return;
	}
	if (g_strcmp0(method_name, "GetApprovedFirmware") == 0) {
		GVariantBuilder builder;
		GPtrArray *checksums = fu_engine_get_approved_firmware(self->engine);
		g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
		for (guint i = 0; i < checksums->len; i++) {
			const gchar *checksum = g_ptr_array_index(checksums, i);
			g_variant_builder_add_value(&builder, g_variant_new_string(checksum));
		}
		val = g_variant_builder_end(&builder);
		g_dbus_method_invocation_return_value(invocation, g_variant_new_tuple(&val, 1));
		return;
	}
	if (g_strcmp0(method_name, "GetBlockedFirmware") == 0) {
		GVariantBuilder builder;
		GPtrArray *checksums = fu_engine_get_blocked_firmware(self->engine);
		g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
		for (guint i = 0; i < checksums->len; i++) {
			const gchar *checksum = g_ptr_array_index(checksums, i);
			g_variant_builder_add_value(&builder, g_variant_new_string(checksum));
		}
		val = g_variant_builder_end(&builder);
		g_dbus_method_invocation_return_value(invocation, g_variant_new_tuple(&val, 1));
		return;
	}
	if (g_strcmp0(method_name, "GetReportMetadata") == 0) {
		GHashTableIter iter;
		GVariantBuilder builder;
		const gchar *key;
		const gchar *value;
		g_autoptr(GHashTable) metadata = NULL;

		metadata = fu_engine_get_report_metadata(self->engine, &error);
		if (metadata == NULL) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}
		g_variant_builder_init(&builder, G_VARIANT_TYPE("a{ss}"));
		g_hash_table_iter_init(&iter, metadata);
		while (g_hash_table_iter_next(&iter, (gpointer *)&key, (gpointer *)&value)) {
			g_variant_builder_add_value(&builder, g_variant_new("{ss}", key, value));
		}
		val = g_variant_builder_end(&builder);
		g_dbus_method_invocation_return_value(invocation, g_variant_new_tuple(&val, 1));
		return;
	}
	if (g_strcmp0(method_name, "SetApprovedFirmware") == 0) {
		g_autofree gchar *checksums_str = NULL;
		g_auto(GStrv) checksums = NULL;
		g_autoptr(FuMainAuthHelper) helper = NULL;
#ifdef HAVE_POLKIT
		g_autoptr(PolkitSubject) subject = NULL;
#endif /* HAVE_POLKIT */

		g_variant_get(parameters, "(^as)", &checksums);
		checksums_str = g_strjoinv(",", checksums);
		g_debug("Called %s(%s)", method_name, checksums_str);

		/* authenticate */
		fu_daemon_set_status(self, FWUPD_STATUS_WAITING_FOR_AUTH);
		helper = g_new0(FuMainAuthHelper, 1);
		helper->self = self;
		helper->request = g_steal_pointer(&request);
		helper->invocation = g_object_ref(invocation);
		helper->checksums = g_ptr_array_new_with_free_func(g_free);
		for (guint i = 0; checksums[i] != NULL; i++)
			g_ptr_array_add(helper->checksums, g_strdup(checksums[i]));
#ifdef HAVE_POLKIT
		subject = polkit_system_bus_name_new(sender);
		polkit_authority_check_authorization(
		    self->authority,
		    subject,
		    "org.freedesktop.fwupd.set-approved-firmware",
		    NULL,
		    POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
		    NULL,
		    fu_daemon_authorize_set_approved_firmware_cb,
		    g_steal_pointer(&helper));
#else
		fu_daemon_authorize_set_approved_firmware_cb(NULL, NULL, g_steal_pointer(&helper));
#endif /* HAVE_POLKIT */
		return;
	}
	if (g_strcmp0(method_name, "SetBlockedFirmware") == 0) {
		g_autofree gchar *checksums_str = NULL;
		g_auto(GStrv) checksums = NULL;
		g_autoptr(FuMainAuthHelper) helper = NULL;
#ifdef HAVE_POLKIT
		g_autoptr(PolkitSubject) subject = NULL;
#endif
		g_variant_get(parameters, "(^as)", &checksums);
		checksums_str = g_strjoinv(",", checksums);
		g_debug("Called %s(%s)", method_name, checksums_str);

		/* authenticate */
		fu_daemon_set_status(self, FWUPD_STATUS_WAITING_FOR_AUTH);
		helper = g_new0(FuMainAuthHelper, 1);
		helper->self = self;
		helper->request = g_steal_pointer(&request);
		helper->invocation = g_object_ref(invocation);
		helper->checksums = g_ptr_array_new_with_free_func(g_free);
		for (guint i = 0; checksums[i] != NULL; i++)
			g_ptr_array_add(helper->checksums, g_strdup(checksums[i]));
#ifdef HAVE_POLKIT
		subject = polkit_system_bus_name_new(sender);
		polkit_authority_check_authorization(
		    self->authority,
		    subject,
		    "org.freedesktop.fwupd.set-approved-firmware",
		    NULL,
		    POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
		    NULL,
		    fu_daemon_authorize_set_blocked_firmware_cb,
		    g_steal_pointer(&helper));
#else
		fu_daemon_authorize_set_blocked_firmware_cb(NULL, NULL, g_steal_pointer(&helper));
#endif /* HAVE_POLKIT */
		return;
	}
	if (g_strcmp0(method_name, "Quit") == 0) {
		if (!fu_engine_request_has_device_flag(request, FWUPD_DEVICE_FLAG_TRUSTED)) {
			g_dbus_method_invocation_return_error_literal(invocation,
								      FWUPD_ERROR,
								      FWUPD_ERROR_PERMISSION_DENIED,
								      "Permission denied");
			return;
		}
		fu_daemon_schedule_process_quit(self);
		g_dbus_method_invocation_return_value(invocation, NULL);
		return;
	}
	if (g_strcmp0(method_name, "SelfSign") == 0) {
		GVariant *prop_value;
		const gchar *prop_key;
		g_autofree gchar *value = NULL;
		g_autoptr(FuMainAuthHelper) helper = NULL;
#ifdef HAVE_POLKIT
		g_autoptr(PolkitSubject) subject = NULL;
#endif
		g_autoptr(GVariantIter) iter = NULL;

		g_variant_get(parameters, "(sa{sv})", &value, &iter);
		g_debug("Called %s(%s)", method_name, value);

		/* get flags */
		helper = g_new0(FuMainAuthHelper, 1);
		while (g_variant_iter_next(iter, "{&sv}", &prop_key, &prop_value)) {
			g_debug("got option %s", prop_key);
			if (g_strcmp0(prop_key, "add-timestamp") == 0 &&
			    g_variant_get_boolean(prop_value) == TRUE)
				helper->flags |= JCAT_SIGN_FLAG_ADD_TIMESTAMP;
			if (g_strcmp0(prop_key, "add-cert") == 0 &&
			    g_variant_get_boolean(prop_value) == TRUE)
				helper->flags |= JCAT_SIGN_FLAG_ADD_CERT;
			g_variant_unref(prop_value);
		}

		/* authenticate */
		fu_daemon_set_status(self, FWUPD_STATUS_WAITING_FOR_AUTH);
		helper->self = self;
		helper->value = g_steal_pointer(&value);
		helper->request = g_steal_pointer(&request);
		helper->invocation = g_object_ref(invocation);
#ifdef HAVE_POLKIT
		subject = polkit_system_bus_name_new(sender);
		polkit_authority_check_authorization(
		    self->authority,
		    subject,
		    "org.freedesktop.fwupd.self-sign",
		    NULL,
		    POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
		    NULL,
		    fu_daemon_authorize_self_sign_cb,
		    g_steal_pointer(&helper));
#else
		fu_daemon_authorize_self_sign_cb(NULL, NULL, g_steal_pointer(&helper));
#endif /* HAVE_POLKIT */
		return;
	}
	if (g_strcmp0(method_name, "GetDowngrades") == 0) {
		const gchar *device_id;
		g_autoptr(GPtrArray) releases = NULL;
		g_variant_get(parameters, "(&s)", &device_id);
		g_debug("Called %s(%s)", method_name, device_id);
		if (!fu_daemon_device_id_valid(device_id, &error)) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}
		releases = fu_engine_get_downgrades(self->engine, request, device_id, &error);
		if (releases == NULL) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}
		val = fu_daemon_release_array_to_variant(releases);
		g_dbus_method_invocation_return_value(invocation, val);
		return;
	}
	if (g_strcmp0(method_name, "GetUpgrades") == 0) {
		const gchar *device_id;
		g_autoptr(GPtrArray) releases = NULL;
		g_variant_get(parameters, "(&s)", &device_id);
		g_debug("Called %s(%s)", method_name, device_id);
		if (!fu_daemon_device_id_valid(device_id, &error)) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}
		releases = fu_engine_get_upgrades(self->engine, request, device_id, &error);
		if (releases == NULL) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}
		val = fu_daemon_release_array_to_variant(releases);
		g_dbus_method_invocation_return_value(invocation, val);
		return;
	}
	if (g_strcmp0(method_name, "GetRemotes") == 0) {
		g_autoptr(GPtrArray) remotes = NULL;
		g_debug("Called %s()", method_name);
		remotes = fu_engine_get_remotes(self->engine, &error);
		if (remotes == NULL) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}
		val = fu_daemon_remote_array_to_variant(remotes);
		g_dbus_method_invocation_return_value(invocation, val);
		return;
	}
	if (g_strcmp0(method_name, "GetHistory") == 0) {
		g_autoptr(GPtrArray) devices = NULL;
		g_debug("Called %s()", method_name);
		devices = fu_engine_get_history(self->engine, &error);
		if (devices == NULL) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}
		val = fu_daemon_device_array_to_variant(self, request, devices, &error);
		if (val == NULL) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}
		g_dbus_method_invocation_return_value(invocation, val);
		return;
	}
	if (g_strcmp0(method_name, "GetHostSecurityAttrs") == 0) {
		g_autoptr(FuSecurityAttrs) attrs = NULL;
		g_debug("Called %s()", method_name);
#ifndef HAVE_HSI
		g_dbus_method_invocation_return_error_literal(invocation,
							      FWUPD_ERROR,
							      FWUPD_ERROR_NOT_SUPPORTED,
							      "HSI support not enabled");
#else
		if (self->machine_kind != FU_DAEMON_MACHINE_KIND_PHYSICAL) {
			g_dbus_method_invocation_return_error_literal(
			    invocation,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "HSI unavailable for hypervisor");
			return;
		}
		attrs = fu_engine_get_host_security_attrs(self->engine);
		val = fu_security_attrs_to_variant(attrs);
		g_dbus_method_invocation_return_value(invocation, val);
#endif
		return;
	}
	if (g_strcmp0(method_name, "GetHostSecurityEvents") == 0) {
		guint limit = 0;
		g_autoptr(FuSecurityAttrs) attrs = NULL;
		g_variant_get(parameters, "(u)", &limit);
		g_debug("Called %s(%u)", method_name, limit);
#ifndef HAVE_HSI
		g_dbus_method_invocation_return_error_literal(invocation,
							      FWUPD_ERROR,
							      FWUPD_ERROR_NOT_SUPPORTED,
							      "HSI support not enabled");
#else
		attrs = fu_engine_get_host_security_events(self->engine, limit, &error);
		if (attrs == NULL) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}
		val = fu_security_attrs_to_variant(attrs);
		g_dbus_method_invocation_return_value(invocation, val);
#endif
		return;
	}
	if (g_strcmp0(method_name, "ClearResults") == 0) {
		const gchar *device_id;
		g_variant_get(parameters, "(&s)", &device_id);
		g_debug("Called %s(%s)", method_name, device_id);
		if (!fu_engine_clear_results(self->engine, device_id, &error)) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}
		g_dbus_method_invocation_return_value(invocation, NULL);
		return;
	}
	if (g_strcmp0(method_name, "ModifyDevice") == 0) {
		const gchar *device_id;
		const gchar *key = NULL;
		const gchar *value = NULL;

		/* check the id exists */
		g_variant_get(parameters, "(&s&s&s)", &device_id, &key, &value);
		g_debug("Called %s(%s,%s=%s)", method_name, device_id, key, value);
		if (!fu_engine_modify_device(self->engine, device_id, key, value, &error)) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}
		g_dbus_method_invocation_return_value(invocation, NULL);
		return;
	}
	if (g_strcmp0(method_name, "GetResults") == 0) {
		const gchar *device_id = NULL;
		g_autoptr(FwupdDevice) result = NULL;
		g_variant_get(parameters, "(&s)", &device_id);
		g_debug("Called %s(%s)", method_name, device_id);
		if (!fu_daemon_device_id_valid(device_id, &error)) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}
		result = fu_engine_get_results(self->engine, device_id, &error);
		if (result == NULL) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}
		val = fwupd_device_to_variant(result);
		g_dbus_method_invocation_return_value(invocation, g_variant_new_tuple(&val, 1));
		return;
	}
	if (g_strcmp0(method_name, "UpdateMetadata") == 0) {
#ifdef HAVE_GIO_UNIX
		GDBusMessage *message;
		GUnixFDList *fd_list;
		const gchar *remote_id = NULL;
		gint fd_data;
		gint fd_sig;

		g_variant_get(parameters, "(&shh)", &remote_id, &fd_data, &fd_sig);
		g_debug("Called %s(%s,%i,%i)", method_name, remote_id, fd_data, fd_sig);

		/* update the metadata store */
		message = g_dbus_method_invocation_get_message(invocation);
		fd_list = g_dbus_message_get_unix_fd_list(message);
		if (fd_list == NULL || g_unix_fd_list_get_length(fd_list) != 2) {
			g_set_error(&error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "invalid handle");
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}
		fd_data = g_unix_fd_list_get(fd_list, 0, &error);
		if (fd_data < 0) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}
		fd_sig = g_unix_fd_list_get(fd_list, 1, &error);
		if (fd_sig < 0) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}

		/* store new metadata (will close the fds when done) */
		if (!fu_engine_update_metadata(self->engine, remote_id, fd_data, fd_sig, &error)) {
			g_prefix_error(&error, "Failed to update metadata for %s: ", remote_id);
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}
		g_dbus_method_invocation_return_value(invocation, NULL);
#else
		g_set_error(&error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "unsupported feature");
		g_dbus_method_invocation_return_gerror(invocation, error);
#endif /* HAVE_GIO_UNIX */
		return;
	}
	if (g_strcmp0(method_name, "Unlock") == 0) {
		const gchar *device_id = NULL;
		g_autoptr(FuMainAuthHelper) helper = NULL;
#ifdef HAVE_POLKIT
		g_autoptr(PolkitSubject) subject = NULL;
#endif /* HAVE_POLKIT */
		g_variant_get(parameters, "(&s)", &device_id);
		g_debug("Called %s(%s)", method_name, device_id);
		if (!fu_daemon_device_id_valid(device_id, &error)) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}

		/* authenticate */
		fu_daemon_set_status(self, FWUPD_STATUS_WAITING_FOR_AUTH);
		helper = g_new0(FuMainAuthHelper, 1);
		helper->self = self;
		helper->request = g_steal_pointer(&request);
		helper->invocation = g_object_ref(invocation);
		helper->device_id = g_strdup(device_id);
#ifdef HAVE_POLKIT
		subject = polkit_system_bus_name_new(sender);
		polkit_authority_check_authorization(
		    self->authority,
		    subject,
		    "org.freedesktop.fwupd.device-unlock",
		    NULL,
		    POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
		    NULL,
		    fu_daemon_authorize_unlock_cb,
		    g_steal_pointer(&helper));
#else
		fu_daemon_authorize_unlock_cb(NULL, NULL, g_steal_pointer(&helper));
#endif /* HAVE_POLKIT */
		return;
	}
	if (g_strcmp0(method_name, "Activate") == 0) {
		const gchar *device_id = NULL;
		g_autoptr(FuMainAuthHelper) helper = NULL;
#ifdef HAVE_POLKIT
		g_autoptr(PolkitSubject) subject = NULL;
#endif
		g_variant_get(parameters, "(&s)", &device_id);
		g_debug("Called %s(%s)", method_name, device_id);
		if (!fu_daemon_device_id_valid(device_id, &error)) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}

		/* authenticate */
		fu_daemon_set_status(self, FWUPD_STATUS_WAITING_FOR_AUTH);
		helper = g_new0(FuMainAuthHelper, 1);
		helper->self = self;
		helper->request = g_steal_pointer(&request);
		helper->invocation = g_object_ref(invocation);
		helper->device_id = g_strdup(device_id);
#ifdef HAVE_POLKIT
		subject = polkit_system_bus_name_new(sender);
		polkit_authority_check_authorization(
		    self->authority,
		    subject,
		    "org.freedesktop.fwupd.device-activate",
		    NULL,
		    POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
		    NULL,
		    fu_daemon_authorize_activate_cb,
		    g_steal_pointer(&helper));
#else
		fu_daemon_authorize_activate_cb(NULL, NULL, g_steal_pointer(&helper));
#endif /* HAVE_POLKIT */
		return;
	}
	if (g_strcmp0(method_name, "ModifyConfig") == 0) {
		g_autofree gchar *key = NULL;
		g_autofree gchar *value = NULL;
		g_autoptr(FuMainAuthHelper) helper = NULL;
#ifdef HAVE_POLKIT
		g_autoptr(PolkitSubject) subject = NULL;
#endif
		g_variant_get(parameters, "(ss)", &key, &value);
		g_debug("Called %s(%s=%s)", method_name, key, value);

		/* authenticate */
		helper = g_new0(FuMainAuthHelper, 1);
		helper->self = self;
		helper->key = g_steal_pointer(&key);
		helper->value = g_steal_pointer(&value);
		helper->request = g_steal_pointer(&request);
		helper->invocation = g_object_ref(invocation);
#ifdef HAVE_POLKIT
		subject = polkit_system_bus_name_new(sender);
		polkit_authority_check_authorization(
		    self->authority,
		    subject,
		    "org.freedesktop.fwupd.modify-config",
		    NULL,
		    POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
		    NULL,
		    fu_daemon_modify_config_cb,
		    g_steal_pointer(&helper));
#else
		fu_daemon_modify_config_cb(NULL, NULL, g_steal_pointer(&helper));
#endif /* HAVE_POLKIT */
		return;
	}
	if (g_strcmp0(method_name, "ModifyRemote") == 0) {
		const gchar *remote_id = NULL;
		const gchar *key = NULL;
		const gchar *value = NULL;
		g_autoptr(FuMainAuthHelper) helper = NULL;
#ifdef HAVE_POLKIT
		g_autoptr(PolkitSubject) subject = NULL;
#endif
		/* check the id exists */
		g_variant_get(parameters, "(&s&s&s)", &remote_id, &key, &value);
		g_debug("Called %s(%s,%s=%s)", method_name, remote_id, key, value);

		/* create helper object */
		helper = g_new0(FuMainAuthHelper, 1);
		helper->request = g_steal_pointer(&request);
		helper->invocation = g_object_ref(invocation);
		helper->remote_id = g_strdup(remote_id);
		helper->key = g_strdup(key);
		helper->value = g_strdup(value);
		helper->self = self;

		/* authenticate */
		fu_daemon_set_status(self, FWUPD_STATUS_WAITING_FOR_AUTH);
#ifdef HAVE_POLKIT
		subject = polkit_system_bus_name_new(sender);
		polkit_authority_check_authorization(
		    self->authority,
		    subject,
		    "org.freedesktop.fwupd.modify-remote",
		    NULL,
		    POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
		    NULL,
		    fu_daemon_authorize_modify_remote_cb,
		    g_steal_pointer(&helper));
#else
		fu_daemon_authorize_modify_remote_cb(NULL, NULL, g_steal_pointer(&helper));
#endif /* HAVE_POLKIT */
		return;
	}
	if (g_strcmp0(method_name, "VerifyUpdate") == 0) {
		const gchar *device_id = NULL;
		g_autoptr(FuMainAuthHelper) helper = NULL;
#ifdef HAVE_POLKIT
		g_autoptr(PolkitSubject) subject = NULL;
#endif

		/* check the id exists */
		g_variant_get(parameters, "(&s)", &device_id);
		g_debug("Called %s(%s)", method_name, device_id);
		if (!fu_daemon_device_id_valid(device_id, &error)) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}

		/* create helper object */
		helper = g_new0(FuMainAuthHelper, 1);
		helper->request = g_steal_pointer(&request);
		helper->invocation = g_object_ref(invocation);
		helper->device_id = g_strdup(device_id);
		helper->self = self;

		/* authenticate */
#ifdef HAVE_POLKIT
		fu_daemon_set_status(self, FWUPD_STATUS_WAITING_FOR_AUTH);
		subject = polkit_system_bus_name_new(sender);
		polkit_authority_check_authorization(
		    self->authority,
		    subject,
		    "org.freedesktop.fwupd.verify-update",
		    NULL,
		    POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
		    NULL,
		    fu_daemon_authorize_verify_update_cb,
		    g_steal_pointer(&helper));
#else
		fu_daemon_authorize_verify_update_cb(NULL, NULL, g_steal_pointer(&helper));
#endif /* HAVE_POLKIT */
		return;
	}
	if (g_strcmp0(method_name, "Verify") == 0) {
		const gchar *device_id = NULL;
		g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);

		g_variant_get(parameters, "(&s)", &device_id);
		g_debug("Called %s(%s)", method_name, device_id);
		if (!fu_daemon_device_id_valid(device_id, &error)) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}

		/* progress */
		fu_progress_set_profile(progress, g_getenv("FWUPD_VERBOSE") != NULL);
		g_signal_connect(FU_PROGRESS(progress),
				 "percentage-changed",
				 G_CALLBACK(fu_daemon_progress_percentage_changed_cb),
				 self);
		g_signal_connect(FU_PROGRESS(progress),
				 "status-changed",
				 G_CALLBACK(fu_daemon_progress_status_changed_cb),
				 self);

		if (!fu_engine_verify(self->engine, device_id, progress, &error)) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}
		g_dbus_method_invocation_return_value(invocation, NULL);
		return;
	}
	if (g_strcmp0(method_name, "SetFeatureFlags") == 0) {
		FuDaemonSenderItem *sender_item;
		guint64 feature_flags_u64 = 0;

		g_variant_get(parameters, "(t)", &feature_flags_u64);
		g_debug("Called %s(%" G_GUINT64_FORMAT ")", method_name, feature_flags_u64);

		/* old flags for the same sender will be automatically destroyed */
		sender_item = fu_daemon_ensure_sender_item(self, sender);
		sender_item->feature_flags = feature_flags_u64;
		g_dbus_method_invocation_return_value(invocation, NULL);
		return;
	}
	if (g_strcmp0(method_name, "SetHints") == 0) {
		FuDaemonSenderItem *sender_item;
		const gchar *prop_key;
		const gchar *prop_value;
		g_autoptr(GVariantIter) iter = NULL;

		g_variant_get(parameters, "(a{ss})", &iter);
		g_debug("Called %s()", method_name);
		sender_item = fu_daemon_ensure_sender_item(self, sender);
		while (g_variant_iter_next(iter, "{&s&s}", &prop_key, &prop_value)) {
			g_debug("got hint %s=%s", prop_key, prop_value);
			g_hash_table_insert(sender_item->hints,
					    g_strdup(prop_key),
					    g_strdup(prop_value));
		}
		g_dbus_method_invocation_return_value(invocation, NULL);
		return;
	}

	if (g_strcmp0(method_name, "Install") == 0) {
#ifdef HAVE_GIO_UNIX
		GVariant *prop_value;
		const gchar *device_id = NULL;
		const gchar *prop_key;
		gint32 fd_handle = 0;
		gint fd;
		guint64 archive_size_max;
		GDBusMessage *message;
		GUnixFDList *fd_list;
		g_autoptr(FuMainAuthHelper) helper = NULL;
		g_autoptr(GVariantIter) iter = NULL;

		/* check the id exists */
		g_variant_get(parameters, "(&sha{sv})", &device_id, &fd_handle, &iter);
		g_debug("Called %s(%s,%i)", method_name, device_id, fd_handle);
		if (!fu_daemon_device_id_valid(device_id, &error)) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}

		/* create helper object */
		helper = g_new0(FuMainAuthHelper, 1);
		helper->request = g_steal_pointer(&request);
		helper->invocation = g_object_ref(invocation);
		helper->device_id = g_strdup(device_id);
		helper->self = self;

		/* get flags */
		while (g_variant_iter_next(iter, "{&sv}", &prop_key, &prop_value)) {
			g_debug("got option %s", prop_key);
			if (g_strcmp0(prop_key, "offline") == 0 &&
			    g_variant_get_boolean(prop_value) == TRUE)
				helper->flags |= FWUPD_INSTALL_FLAG_OFFLINE;
			if (g_strcmp0(prop_key, "allow-older") == 0 &&
			    g_variant_get_boolean(prop_value) == TRUE)
				helper->flags |= FWUPD_INSTALL_FLAG_ALLOW_OLDER;
			if (g_strcmp0(prop_key, "allow-reinstall") == 0 &&
			    g_variant_get_boolean(prop_value) == TRUE)
				helper->flags |= FWUPD_INSTALL_FLAG_ALLOW_REINSTALL;
			if (g_strcmp0(prop_key, "allow-branch-switch") == 0 &&
			    g_variant_get_boolean(prop_value) == TRUE)
				helper->flags |= FWUPD_INSTALL_FLAG_ALLOW_BRANCH_SWITCH;
			if (g_strcmp0(prop_key, "force") == 0 &&
			    g_variant_get_boolean(prop_value) == TRUE)
				helper->flags |= FWUPD_INSTALL_FLAG_FORCE;
			if (g_strcmp0(prop_key, "ignore-power") == 0 &&
			    g_variant_get_boolean(prop_value) == TRUE)
				helper->flags |= FWUPD_INSTALL_FLAG_IGNORE_POWER;
			if (g_strcmp0(prop_key, "no-history") == 0 &&
			    g_variant_get_boolean(prop_value) == TRUE)
				helper->flags |= FWUPD_INSTALL_FLAG_NO_HISTORY;
			g_variant_unref(prop_value);
		}

		/* get the fd */
		message = g_dbus_method_invocation_get_message(invocation);
		fd_list = g_dbus_message_get_unix_fd_list(message);
		if (fd_list == NULL || g_unix_fd_list_get_length(fd_list) != 1) {
			g_set_error(&error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "invalid handle");
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}
		fd = g_unix_fd_list_get(fd_list, 0, &error);
		if (fd < 0) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}

		/* parse the cab file before authenticating so we can work out
		 * what action ID to use, for instance, if this is trusted --
		 * this will also close the fd when done */
		archive_size_max = fu_engine_get_archive_size_max(self->engine);
		helper->blob_cab = fu_common_get_contents_fd(fd, archive_size_max, &error);
		if (helper->blob_cab == NULL) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}

		/* install all the things in the store */
#ifdef HAVE_POLKIT
		helper->subject = polkit_system_bus_name_new(sender);
#endif /* HAVE_POLKIT */
		if (!fu_daemon_install_with_helper(g_steal_pointer(&helper), &error)) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}
#else
		g_set_error(&error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "unsupported feature");
		g_dbus_method_invocation_return_gerror(invocation, error);
#endif /* HAVE_GIO_UNIX */

		/* async return */
		return;
	}
	if (g_strcmp0(method_name, "GetDetails") == 0) {
#ifdef HAVE_GIO_UNIX
		GDBusMessage *message;
		GUnixFDList *fd_list;
		gint32 fd_handle = 0;
		gint fd;
		g_autoptr(GPtrArray) results = NULL;

		/* get parameters */
		g_variant_get(parameters, "(h)", &fd_handle);
		g_debug("Called %s(%i)", method_name, fd_handle);

		/* get the fd */
		message = g_dbus_method_invocation_get_message(invocation);
		fd_list = g_dbus_message_get_unix_fd_list(message);
		if (fd_list == NULL || g_unix_fd_list_get_length(fd_list) != 1) {
			g_set_error(&error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "invalid handle");
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}
		fd = g_unix_fd_list_get(fd_list, 0, &error);
		if (fd < 0) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}

		/* get details about the file (will close the fd when done) */
		results = fu_engine_get_details(self->engine, request, fd, &error);
		if (results == NULL) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}
		val = fu_daemon_result_array_to_variant(results);
		g_dbus_method_invocation_return_value(invocation, val);
#else
		g_set_error(&error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "unsupported feature");
		g_dbus_method_invocation_return_gerror(invocation, error);
#endif /* HAVE_GIO_UNIX */
		return;
	}
	g_set_error(&error,
		    G_DBUS_ERROR,
		    G_DBUS_ERROR_UNKNOWN_METHOD,
		    "no such method %s",
		    method_name);
	g_dbus_method_invocation_return_gerror(invocation, error);
}

static GVariant *
fu_daemon_daemon_get_property(GDBusConnection *connection_,
			      const gchar *sender,
			      const gchar *object_path,
			      const gchar *interface_name,
			      const gchar *property_name,
			      GError **error,
			      gpointer user_data)
{
	FuDaemon *self = FU_DAEMON(user_data);

	/* activity */
	fu_engine_idle_reset(self->engine);

	if (g_strcmp0(property_name, "DaemonVersion") == 0)
		return g_variant_new_string(SOURCE_VERSION);

	if (g_strcmp0(property_name, "HostBkc") == 0)
		return g_variant_new_string(fu_engine_get_host_bkc(self->engine));

	if (g_strcmp0(property_name, "Tainted") == 0)
		return g_variant_new_boolean(fu_engine_get_tainted(self->engine));

	if (g_strcmp0(property_name, "Status") == 0)
		return g_variant_new_uint32(self->status);

	if (g_strcmp0(property_name, FWUPD_RESULT_KEY_BATTERY_LEVEL) == 0) {
		FuContext *ctx = fu_engine_get_context(self->engine);
		return g_variant_new_uint32(fu_context_get_battery_level(ctx));
	}

	if (g_strcmp0(property_name, FWUPD_RESULT_KEY_BATTERY_THRESHOLD) == 0) {
		FuContext *ctx = fu_engine_get_context(self->engine);
		return g_variant_new_uint32(fu_context_get_battery_threshold(ctx));
	}

	if (g_strcmp0(property_name, "HostProduct") == 0)
		return g_variant_new_string(fu_engine_get_host_product(self->engine));

	if (g_strcmp0(property_name, "HostMachineId") == 0) {
		const gchar *tmp = fu_engine_get_host_machine_id(self->engine);
		if (tmp == NULL) {
			g_set_error(error,
				    G_DBUS_ERROR,
				    G_DBUS_ERROR_NOT_SUPPORTED,
				    "failed to get daemon property %s",
				    property_name);
			return NULL;
		}
		return g_variant_new_string(tmp);
	}

	if (g_strcmp0(property_name, "HostSecurityId") == 0) {
		const gchar *tmp = fu_engine_get_host_security_id(self->engine);
		if (tmp == NULL) {
			g_set_error(error,
				    G_DBUS_ERROR,
				    G_DBUS_ERROR_NOT_SUPPORTED,
				    "failed to get daemon property %s",
				    property_name);
			return NULL;
		}
		return g_variant_new_string(tmp);
	}

	if (g_strcmp0(property_name, "Interactive") == 0)
		return g_variant_new_boolean(isatty(fileno(stdout)) != 0);

	if (g_strcmp0(property_name, "OnlyTrusted") == 0)
		return g_variant_new_boolean(fu_engine_get_only_trusted(self->engine));

	/* return an error */
	g_set_error(error,
		    G_DBUS_ERROR,
		    G_DBUS_ERROR_UNKNOWN_PROPERTY,
		    "failed to get daemon property %s",
		    property_name);
	return NULL;
}

static void
fu_daemon_register_object(FuDaemon *self)
{
	guint registration_id;
	static const GDBusInterfaceVTable interface_vtable = {fu_daemon_daemon_method_call,
							      fu_daemon_daemon_get_property,
							      NULL};

	registration_id =
	    g_dbus_connection_register_object(self->connection,
					      FWUPD_DBUS_PATH,
					      self->introspection_daemon->interfaces[0],
					      &interface_vtable,
					      self,  /* user_data */
					      NULL,  /* user_data_free_func */
					      NULL); /* GError** */
	g_assert(registration_id > 0);
}

static void
fu_daemon_dbus_bus_acquired_cb(GDBusConnection *connection, const gchar *name, gpointer user_data)
{
	FuDaemon *self = FU_DAEMON(user_data);
	g_autoptr(GError) error = NULL;

	self->connection = g_object_ref(connection);
	fu_daemon_register_object(self);

	/* connect to D-Bus directly */
	self->proxy_uid = g_dbus_proxy_new_sync(self->connection,
						G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
						    G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
						NULL,
						"org.freedesktop.DBus",
						"/org/freedesktop/DBus",
						"org.freedesktop.DBus",
						NULL,
						&error);
	if (self->proxy_uid == NULL) {
		g_warning("cannot connect to DBus: %s", error->message);
		return;
	}
}

static void
fu_daemon_dbus_name_acquired_cb(GDBusConnection *connection, const gchar *name, gpointer user_data)
{
	g_debug("acquired name: %s", name);
}

static void
fu_daemon_dbus_name_lost_cb(GDBusConnection *connection, const gchar *name, gpointer user_data)
{
	FuDaemon *self = FU_DAEMON(user_data);
	if (self->update_in_progress) {
		g_warning("name lost during a firmware update, ignoring");
		return;
	}
	g_warning("another service has claimed the dbus name %s", name);
	g_main_loop_quit(self->loop);
}

static void
fu_daemon_dbus_connection_closed_cb(GDBusConnection *connection,
				    gboolean remote_peer_vanished,
				    GError *error,
				    gpointer user_data)
{
	FuDaemon *self = FU_DAEMON(user_data);
	g_debug("client connection closed: %s", error != NULL ? error->message : "unknown");
	g_clear_object(&self->connection);
}

static gboolean
fu_daemon_dbus_new_connection_cb(GDBusServer *server,
				 GDBusConnection *connection,
				 gpointer user_data)
{
	FuDaemon *self = FU_DAEMON(user_data);
	g_set_object(&self->connection, connection);
	g_signal_connect(connection,
			 "closed",
			 G_CALLBACK(fu_daemon_dbus_connection_closed_cb),
			 self);
	fu_daemon_register_object(self);
	return TRUE;
}

static GDBusNodeInfo *
fu_daemon_load_introspection(const gchar *filename, GError **error)
{
	g_autoptr(GBytes) data = NULL;
	g_autofree gchar *path = NULL;

	/* lookup data */
	path = g_build_filename("/org/freedesktop/fwupd", filename, NULL);
	data = g_resource_lookup_data(fu_get_resource(), path, G_RESOURCE_LOOKUP_FLAGS_NONE, error);
	if (data == NULL)
		return NULL;

	/* build introspection from XML */
	return g_dbus_node_info_new_for_xml(g_bytes_get_data(data, NULL), error);
}

void
fu_daemon_set_machine_kind(FuDaemon *self, FuDaemonMachineKind machine_kind)
{
	g_return_if_fail(FU_IS_DAEMON(self));
	self->machine_kind = machine_kind;
}

gboolean
fu_daemon_setup(FuDaemon *self, const gchar *socket_address, GError **error)
{
	const gchar *machine_kind = g_getenv("FWUPD_MACHINE_KIND");

	g_return_val_if_fail(FU_IS_DAEMON(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* allow overriding for development */
	if (machine_kind != NULL) {
		self->machine_kind = fu_daemon_machine_kind_from_string(machine_kind);
		if (self->machine_kind == FU_DAEMON_MACHINE_KIND_UNKNOWN) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_ARGUMENT,
				    "Invalid machine kind specified: %s",
				    machine_kind);
			return FALSE;
		}
	}

	/* load engine */
	self->engine = fu_engine_new(FU_APP_FLAGS_NONE);
	g_signal_connect(FU_ENGINE(self->engine),
			 "changed",
			 G_CALLBACK(fu_daemon_engine_changed_cb),
			 self);
	g_signal_connect(FU_ENGINE(self->engine),
			 "device-added",
			 G_CALLBACK(fu_daemon_engine_device_added_cb),
			 self);
	g_signal_connect(FU_ENGINE(self->engine),
			 "device-removed",
			 G_CALLBACK(fu_daemon_engine_device_removed_cb),
			 self);
	g_signal_connect(FU_ENGINE(self->engine),
			 "device-changed",
			 G_CALLBACK(fu_daemon_engine_device_changed_cb),
			 self);
	g_signal_connect(FU_ENGINE(self->engine),
			 "device-request",
			 G_CALLBACK(fu_daemon_engine_device_request_cb),
			 self);
	g_signal_connect(FU_ENGINE(self->engine),
			 "status-changed",
			 G_CALLBACK(fu_daemon_engine_status_changed_cb),
			 self);
	if (!fu_engine_load(self->engine,
			    FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_HWINFO |
				FU_ENGINE_LOAD_FLAG_REMOTES,
			    error)) {
		g_prefix_error(error, "failed to load engine: ");
		return FALSE;
	}

	/* load introspection from file */
	self->introspection_daemon =
	    fu_daemon_load_introspection(FWUPD_DBUS_INTERFACE ".xml", error);
	if (self->introspection_daemon == NULL) {
		g_prefix_error(error, "failed to load introspection: ");
		return FALSE;
	}

#ifdef HAVE_POLKIT
	/* get authority */
	self->authority = polkit_authority_get_sync(NULL, error);
	if (self->authority == NULL) {
		g_prefix_error(error, "failed to load authority: ");
		return FALSE;
	}
#endif

	/* own the object */
	if (socket_address != NULL) {
		g_autofree gchar *guid = g_dbus_generate_guid();
		g_autoptr(GDBusServer) server = NULL;

		server = g_dbus_server_new_sync(socket_address,
						G_DBUS_SERVER_FLAGS_AUTHENTICATION_ALLOW_ANONYMOUS,
						guid,
						NULL,
						NULL,
						error);
		if (server == NULL) {
			g_prefix_error(error, "failed to create D-Bus server: ");
			return FALSE;
		}
		g_message("using socket address: %s", g_dbus_server_get_client_address(server));
		g_dbus_server_start(server);
		g_signal_connect(server,
				 "new-connection",
				 G_CALLBACK(fu_daemon_dbus_new_connection_cb),
				 self);
	} else {
		self->owner_id = g_bus_own_name(G_BUS_TYPE_SYSTEM,
						FWUPD_DBUS_SERVICE,
						G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
						    G_BUS_NAME_OWNER_FLAGS_REPLACE,
						fu_daemon_dbus_bus_acquired_cb,
						fu_daemon_dbus_name_acquired_cb,
						fu_daemon_dbus_name_lost_cb,
						self,
						NULL);
	}

	/* success */
	return TRUE;
}

static void
fu_daemon_sender_item_free(FuDaemonSenderItem *sender_item)
{
	g_hash_table_unref(sender_item->hints);
	g_free(sender_item);
}

static void
fu_daemon_init(FuDaemon *self)
{
	self->status = FWUPD_STATUS_IDLE;
	self->sender_items = g_hash_table_new_full(g_str_hash,
						   g_str_equal,
						   g_free,
						   (GDestroyNotify)fu_daemon_sender_item_free);
	self->loop = g_main_loop_new(NULL, FALSE);
}

static void
fu_daemon_finalize(GObject *obj)
{
	FuDaemon *self = FU_DAEMON(obj);

	g_hash_table_unref(self->sender_items);
	if (self->process_quit_id != 0)
		g_source_remove(self->process_quit_id);
	if (self->loop != NULL)
		g_main_loop_unref(self->loop);
	if (self->owner_id > 0)
		g_bus_unown_name(self->owner_id);
	if (self->proxy_uid != NULL)
		g_object_unref(self->proxy_uid);
	if (self->engine != NULL)
		g_object_unref(self->engine);
	if (self->connection != NULL)
		g_object_unref(self->connection);
#ifdef HAVE_POLKIT
	if (self->authority != NULL)
		g_object_unref(self->authority);
#endif
	if (self->introspection_daemon != NULL)
		g_dbus_node_info_unref(self->introspection_daemon);

	G_OBJECT_CLASS(fu_daemon_parent_class)->finalize(obj);
}

static void
fu_daemon_class_init(FuDaemonClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_daemon_finalize;
}

FuDaemon *
fu_daemon_new(void)
{
	FuDaemon *self;
	self = g_object_new(FU_TYPE_DAEMON, NULL);
	return FU_DAEMON(self);
}
