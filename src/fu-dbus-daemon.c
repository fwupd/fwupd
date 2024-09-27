/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuMain"

#include "config.h"

#include <fwupdplugin.h>
#ifdef HAVE_GIO_UNIX
#include <gio/gunixfdlist.h>
#include <gio/gunixoutputstream.h>
#endif
#include <glib/gstdio.h>
#include <jcat.h>

#include "fwupd-device-private.h"
#include "fwupd-enums-private.h"
#include "fwupd-remote-private.h"
#include "fwupd-request-private.h"
#include "fwupd-security-attr-private.h"

#include "fu-bios-settings-private.h"
#include "fu-client-list.h"
#include "fu-dbus-daemon.h"
#include "fu-device-private.h"
#include "fu-engine-helper.h"
#include "fu-engine-requirements.h"
#include "fu-polkit-authority.h"
#include "fu-release.h"
#include "fu-security-attrs-private.h"

#ifdef HAVE_GIO_UNIX
#include "fu-unix-seekable-input-stream.h"
#endif

struct _FuDbusDaemon {
	FuDaemon parent_instance;
	GDBusConnection *connection;
	GDBusNodeInfo *introspection_daemon;
	GDBusProxy *proxy_uid;
	FuClientList *client_list;
	guint32 clients_inhibit_id;
	FuPolkitAuthority *authority;
	FwupdStatus status; /* last emitted */
	guint percentage;   /* last emitted */
	guint owner_id;
	GPtrArray *system_inhibits;
};

G_DEFINE_TYPE(FuDbusDaemon, fu_dbus_daemon, FU_TYPE_DAEMON)

#define FU_DAEMON_INSTALL_FLAG_MASK_SAFE                                                           \
	(FWUPD_INSTALL_FLAG_ALLOW_OLDER | FWUPD_INSTALL_FLAG_ALLOW_REINSTALL |                     \
	 FWUPD_INSTALL_FLAG_ALLOW_BRANCH_SWITCH | FWUPD_INSTALL_FLAG_FORCE |                       \
	 FWUPD_INSTALL_FLAG_NO_HISTORY | FWUPD_INSTALL_FLAG_IGNORE_REQUIREMENTS)

static void
fu_dbus_daemon_engine_changed_cb(FuEngine *engine, FuDbusDaemon *self)
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
	fu_daemon_schedule_housekeeping(FU_DAEMON(self));
}

static void
fu_dbus_daemon_engine_device_added_cb(FuEngine *engine, FuDevice *device, FuDbusDaemon *self)
{
	GVariant *val;

	/* not yet connected */
	if (self->connection == NULL)
		return;
	val = fwupd_codec_to_variant(FWUPD_CODEC(device), FWUPD_CODEC_FLAG_NONE);
	g_dbus_connection_emit_signal(self->connection,
				      NULL,
				      FWUPD_DBUS_PATH,
				      FWUPD_DBUS_INTERFACE,
				      "DeviceAdded",
				      g_variant_new_tuple(&val, 1),
				      NULL);
	fu_daemon_schedule_housekeeping(FU_DAEMON(self));
}

static void
fu_dbus_daemon_engine_device_removed_cb(FuEngine *engine, FuDevice *device, FuDbusDaemon *self)
{
	GVariant *val;

	/* not yet connected */
	if (self->connection == NULL)
		return;
	val = fwupd_codec_to_variant(FWUPD_CODEC(device), FWUPD_CODEC_FLAG_NONE);
	g_dbus_connection_emit_signal(self->connection,
				      NULL,
				      FWUPD_DBUS_PATH,
				      FWUPD_DBUS_INTERFACE,
				      "DeviceRemoved",
				      g_variant_new_tuple(&val, 1),
				      NULL);
	fu_daemon_schedule_housekeeping(FU_DAEMON(self));
}

static void
fu_dbus_daemon_engine_device_changed_cb(FuEngine *engine, FuDevice *device, FuDbusDaemon *self)
{
	GVariant *val;

	/* not yet connected */
	if (self->connection == NULL)
		return;
	val = fwupd_codec_to_variant(FWUPD_CODEC(device), FWUPD_CODEC_FLAG_NONE);
	g_dbus_connection_emit_signal(self->connection,
				      NULL,
				      FWUPD_DBUS_PATH,
				      FWUPD_DBUS_INTERFACE,
				      "DeviceChanged",
				      g_variant_new_tuple(&val, 1),
				      NULL);
	fu_daemon_schedule_housekeeping(FU_DAEMON(self));
}

static void
fu_dbus_daemon_engine_device_request_cb(FuEngine *engine, FwupdRequest *request, FuDbusDaemon *self)
{
	GVariant *val;

	/* not yet connected */
	if (self->connection == NULL)
		return;
	val = fwupd_codec_to_variant(FWUPD_CODEC(request), FWUPD_CODEC_FLAG_NONE);
	g_dbus_connection_emit_signal(self->connection,
				      NULL,
				      FWUPD_DBUS_PATH,
				      FWUPD_DBUS_INTERFACE,
				      "DeviceRequest",
				      g_variant_new_tuple(&val, 1),
				      NULL);
}

static void
fu_dbus_daemon_emit_property_changed(FuDbusDaemon *self,
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
fu_dbus_daemon_set_status(FuDbusDaemon *self, FwupdStatus status)
{
	/* sanity check */
	if (self->status == status)
		return;
	self->status = status;

	g_debug("Emitting PropertyChanged('Status'='%s')", fwupd_status_to_string(status));
	fu_dbus_daemon_emit_property_changed(self, "Status", g_variant_new_uint32(status));
}

static void
fu_dbus_daemon_engine_status_changed_cb(FuEngine *engine, FwupdStatus status, FuDbusDaemon *self)
{
	fu_dbus_daemon_set_status(self, status);

	/* engine has gone idle */
	if (status == FWUPD_STATUS_SHUTDOWN)
		fu_daemon_stop(FU_DAEMON(self), NULL);
}

static FuEngineRequest *
fu_dbus_daemon_create_request(FuDbusDaemon *self, const gchar *sender, GError **error)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	FwupdCodecFlags converter_flags = FWUPD_CODEC_FLAG_NONE;
	guint calling_uid = 0;
	g_autoptr(FuClient) client = NULL;
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(sender);
	g_autoptr(GVariant) value = NULL;

	/* if using FWUPD_DBUS_SOCKET... */
	if (sender == NULL) {
		fu_engine_request_set_converter_flags(request, FWUPD_CODEC_FLAG_TRUSTED);
		return g_object_ref(request);
	}

	/* did the client set the list of supported features or any hints */
	client = fu_client_list_get_by_sender(self->client_list, sender);
	if (client != NULL) {
		const gchar *locale = fu_client_lookup_hint(client, "locale");
		if (locale != NULL)
			fu_engine_request_set_locale(request, locale);
		fu_engine_request_set_feature_flags(request, fu_client_get_feature_flags(client));
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
	if (fu_engine_is_uid_trusted(engine, calling_uid))
		converter_flags |= FWUPD_CODEC_FLAG_TRUSTED;
	fu_engine_request_set_converter_flags(request, converter_flags);

	/* success */
	return g_object_ref(request);
}

static GVariant *
fu_dbus_daemon_device_array_to_variant(FuDbusDaemon *self,
				       FuEngineRequest *request,
				       GPtrArray *devices,
				       GError **error)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	FwupdCodecFlags flags = fu_engine_request_get_converter_flags(request);
	if (fu_engine_config_get_show_device_private(fu_engine_get_config(engine)))
		flags |= FWUPD_CODEC_FLAG_TRUSTED;
	return fwupd_codec_array_to_variant(devices, flags);
}

typedef struct {
	GDBusMethodInvocation *invocation;
	FuEngineRequest *request;
	FuProgress *progress;
	FuClient *client;
	glong client_sender_changed_id;
	GPtrArray *releases;
	GPtrArray *action_ids;
	GPtrArray *checksums;
	GPtrArray *errors;
	guint64 flags;
	GInputStream *stream;
	FuDbusDaemon *self;
	gchar *device_id;
	gchar *remote_id;
	gchar *section;
	gchar *key;
	gchar *value;
	FuCabinet *cabinet;
	GHashTable *bios_settings; /* str:str */
	gboolean is_fix;
} FuMainAuthHelper;

static void
fu_dbus_daemon_auth_helper_free(FuMainAuthHelper *helper)
{
	/* always return to IDLE even in event of an auth error */
	fu_dbus_daemon_set_status(helper->self, FWUPD_STATUS_IDLE);

	if (helper->cabinet != NULL)
		g_object_unref(helper->cabinet);
	if (helper->stream != NULL)
		g_object_unref(helper->stream);
	if (helper->request != NULL)
		g_object_unref(helper->request);
	if (helper->progress != NULL)
		g_object_unref(helper->progress);
	if (helper->releases != NULL)
		g_ptr_array_unref(helper->releases);
	if (helper->action_ids != NULL)
		g_ptr_array_unref(helper->action_ids);
	if (helper->checksums != NULL)
		g_ptr_array_unref(helper->checksums);
	if (helper->errors != NULL)
		g_ptr_array_unref(helper->errors);
	if (helper->client_sender_changed_id > 0)
		g_signal_handler_disconnect(helper->client, helper->client_sender_changed_id);
	if (helper->client != NULL)
		g_object_unref(helper->client);
	g_free(helper->device_id);
	g_free(helper->remote_id);
	g_free(helper->section);
	g_free(helper->key);
	g_free(helper->value);
	g_object_unref(helper->invocation);
	if (helper->bios_settings != NULL)
		g_hash_table_unref(helper->bios_settings);
	g_free(helper);
}

static void
fu_dbus_daemon_method_invocation_return_gerror(GDBusMethodInvocation *invocation, GError *error)
{
	fu_error_convert(&error);
	g_dbus_method_invocation_return_gerror(invocation, error);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuMainAuthHelper, fu_dbus_daemon_auth_helper_free)
#pragma clang diagnostic pop

static void
fu_dbus_daemon_authorize_unlock_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(FuMainAuthHelper) helper = (FuMainAuthHelper *)user_data;
	g_autoptr(GError) error = NULL;
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(helper->self));

	/* get result */
	if (!fu_polkit_authority_check_finish(FU_POLKIT_AUTHORITY(source), res, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(helper->invocation, error);
		return;
	}

	/* authenticated */
	if (!fu_engine_unlock(engine, helper->device_id, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(helper->invocation, error);
		return;
	}

	/* success */
	g_dbus_method_invocation_return_value(helper->invocation, NULL);
}

static void
fu_dbus_daemon_authorize_get_bios_settings_cb(GObject *source,
					      GAsyncResult *res,
					      gpointer user_data)
{
	g_autoptr(FuMainAuthHelper) helper = (FuMainAuthHelper *)user_data;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuBiosSettings) attrs = NULL;
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(helper->self));
	FuContext *ctx = fu_engine_get_context(engine);
	GVariant *val = NULL;

	/* get result */
	if (!fu_polkit_authority_check_finish(FU_POLKIT_AUTHORITY(source), res, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(helper->invocation, error);
		return;
	}

	/* authenticated */
	attrs = fu_context_get_bios_settings(ctx);
	val = fwupd_codec_to_variant(FWUPD_CODEC(attrs), FWUPD_CODEC_FLAG_TRUSTED);
	g_dbus_method_invocation_return_value(helper->invocation, val);
}

static void
fu_dbus_daemon_authorize_set_bios_settings_cb(GObject *source,
					      GAsyncResult *res,
					      gpointer user_data)
{
	g_autoptr(FuMainAuthHelper) helper = (FuMainAuthHelper *)user_data;
	g_autoptr(GError) error = NULL;
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(helper->self));

	/* get result */
	if (!fu_polkit_authority_check_finish(FU_POLKIT_AUTHORITY(source), res, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(helper->invocation, error);
		return;
	}

	/* authenticated */
	if (!fu_engine_modify_bios_settings(engine, helper->bios_settings, FALSE, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(helper->invocation, error);
		return;
	}
	/* success */
	g_dbus_method_invocation_return_value(helper->invocation, NULL);
}

static void
fu_dbus_daemon_authorize_set_approved_firmware_cb(GObject *source,
						  GAsyncResult *res,
						  gpointer user_data)
{
	g_autoptr(FuMainAuthHelper) helper = (FuMainAuthHelper *)user_data;
	g_autoptr(GError) error = NULL;
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(helper->self));

	/* get result */
	if (!fu_polkit_authority_check_finish(FU_POLKIT_AUTHORITY(source), res, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(helper->invocation, error);
		return;
	}

	/* success */
	for (guint i = 0; i < helper->checksums->len; i++) {
		const gchar *csum = g_ptr_array_index(helper->checksums, i);
		fu_engine_add_approved_firmware(engine, csum);
	}
	g_dbus_method_invocation_return_value(helper->invocation, NULL);
}

static void
fu_dbus_daemon_authorize_set_blocked_firmware_cb(GObject *source,
						 GAsyncResult *res,
						 gpointer user_data)
{
	g_autoptr(FuMainAuthHelper) helper = (FuMainAuthHelper *)user_data;
	g_autoptr(GError) error = NULL;
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(helper->self));

	/* get result */
	if (!fu_polkit_authority_check_finish(FU_POLKIT_AUTHORITY(source), res, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(helper->invocation, error);
		return;
	}

	/* success */
	if (!fu_engine_set_blocked_firmware(engine, helper->checksums, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(helper->invocation, error);
		return;
	}
	g_dbus_method_invocation_return_value(helper->invocation, NULL);
}

static void
fu_dbus_daemon_authorize_fix_host_security_attr_cb(GObject *source,
						   GAsyncResult *res,
						   gpointer user_data)
{
	g_autoptr(FuMainAuthHelper) helper = (FuMainAuthHelper *)user_data;
	g_autoptr(GError) error = NULL;
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(helper->self));

	/* get result */
	if (!fu_polkit_authority_check_finish(FU_POLKIT_AUTHORITY(source), res, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(helper->invocation, error);
		return;
	}

	/* success */
	if (!fu_engine_fix_host_security_attr(engine, helper->key, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(helper->invocation, error);
		return;
	}
	g_dbus_method_invocation_return_value(helper->invocation, NULL);
}

static void
fu_dbus_daemon_authorize_undo_host_security_attr_cb(GObject *source,
						    GAsyncResult *res,
						    gpointer user_data)
{
	g_autoptr(FuMainAuthHelper) helper = (FuMainAuthHelper *)user_data;
	g_autoptr(GError) error = NULL;
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(helper->self));

	/* get result */
	if (!fu_polkit_authority_check_finish(FU_POLKIT_AUTHORITY(source), res, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(helper->invocation, error);
		return;
	}

	/* success */
	if (!fu_engine_undo_host_security_attr(engine, helper->key, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(helper->invocation, error);
		return;
	}
	g_dbus_method_invocation_return_value(helper->invocation, NULL);
}

static void
fu_dbus_daemon_authorize_self_sign_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(FuMainAuthHelper) helper = (FuMainAuthHelper *)user_data;
	g_autofree gchar *sig = NULL;
	g_autoptr(GError) error = NULL;
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(helper->self));

	/* get result */
	if (!fu_polkit_authority_check_finish(FU_POLKIT_AUTHORITY(source), res, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(helper->invocation, error);
		return;
	}

	/* authenticated */
	sig = fu_engine_self_sign(engine, helper->value, helper->flags, &error);
	if (sig == NULL) {
		fu_dbus_daemon_method_invocation_return_gerror(helper->invocation, error);
		return;
	}

	/* success */
	g_dbus_method_invocation_return_value(helper->invocation, g_variant_new("(s)", sig));
}

static void
fu_dbus_daemon_modify_config_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(FuMainAuthHelper) helper = (FuMainAuthHelper *)user_data;
	g_autoptr(GError) error = NULL;
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(helper->self));

	/* get result */
	if (!fu_polkit_authority_check_finish(FU_POLKIT_AUTHORITY(source), res, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(helper->invocation, error);
		return;
	}

	if (!fu_engine_modify_config(engine, helper->section, helper->key, helper->value, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(helper->invocation, error);
		return;
	}

	/* success */
	g_dbus_method_invocation_return_value(helper->invocation, NULL);
}

static void
fu_dbus_daemon_reset_config_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(FuMainAuthHelper) helper = (FuMainAuthHelper *)user_data;
	g_autoptr(GError) error = NULL;
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(helper->self));

	/* get result */
	if (!fu_polkit_authority_check_finish(FU_POLKIT_AUTHORITY(source), res, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(helper->invocation, error);
		return;
	}
	if (!fu_engine_reset_config(engine, helper->section, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(helper->invocation, error);
		return;
	}

	/* success */
	g_dbus_method_invocation_return_value(helper->invocation, NULL);
}

static void
fu_dbus_daemon_progress_percentage_changed_cb(FuProgress *progress,
					      guint percentage,
					      FuDbusDaemon *self)
{
	/* sanity check */
	if (self->percentage == percentage)
		return;
	self->percentage = percentage;

	g_debug("Emitting PropertyChanged('Percentage'='%u%%')", percentage);
	fu_dbus_daemon_emit_property_changed(self, "Percentage", g_variant_new_uint32(percentage));
}

static void
fu_dbus_daemon_progress_status_changed_cb(FuProgress *progress,
					  FwupdStatus status,
					  FuDbusDaemon *self)
{
	fu_dbus_daemon_set_status(self, status);
}

static void
fu_dbus_daemon_authorize_activate_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(FuMainAuthHelper) helper = (FuMainAuthHelper *)user_data;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(helper->self));

	/* get result */
	if (!fu_polkit_authority_check_finish(FU_POLKIT_AUTHORITY(source), res, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(helper->invocation, error);
		return;
	}

	/* progress */
	fu_progress_set_profile(progress, g_getenv("FWUPD_VERBOSE") != NULL);
	g_signal_connect(FU_PROGRESS(progress),
			 "percentage-changed",
			 G_CALLBACK(fu_dbus_daemon_progress_percentage_changed_cb),
			 helper->self);
	g_signal_connect(FU_PROGRESS(progress),
			 "status-changed",
			 G_CALLBACK(fu_dbus_daemon_progress_status_changed_cb),
			 helper->self);

	/* authenticated */
	if (!fu_engine_activate(engine, helper->device_id, progress, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(helper->invocation, error);
		return;
	}

	/* success */
	g_dbus_method_invocation_return_value(helper->invocation, NULL);
}

static void
fu_dbus_daemon_authorize_verify_update_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(FuMainAuthHelper) helper = (FuMainAuthHelper *)user_data;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(helper->self));

	/* get result */
	if (!fu_polkit_authority_check_finish(FU_POLKIT_AUTHORITY(source), res, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(helper->invocation, error);
		return;
	}

	/* progress */
	fu_progress_set_profile(progress, g_getenv("FWUPD_VERBOSE") != NULL);
	g_signal_connect(FU_PROGRESS(progress),
			 "percentage-changed",
			 G_CALLBACK(fu_dbus_daemon_progress_percentage_changed_cb),
			 helper->self);
	g_signal_connect(FU_PROGRESS(progress),
			 "status-changed",
			 G_CALLBACK(fu_dbus_daemon_progress_status_changed_cb),
			 helper->self);

	/* authenticated */
	if (!fu_engine_verify_update(engine, helper->device_id, progress, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(helper->invocation, error);
		return;
	}

	/* success */
	g_dbus_method_invocation_return_value(helper->invocation, NULL);
}

static void
fu_dbus_daemon_authorize_modify_remote_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(FuMainAuthHelper) helper = (FuMainAuthHelper *)user_data;
	g_autoptr(GError) error = NULL;
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(helper->self));

	/* get result */
	if (!fu_polkit_authority_check_finish(FU_POLKIT_AUTHORITY(source), res, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(helper->invocation, error);
		return;
	}

	/* authenticated */
	if (!fu_engine_modify_remote(engine,
				     helper->remote_id,
				     helper->key,
				     helper->value,
				     &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(helper->invocation, error);
		return;
	}

	/* success */
	g_dbus_method_invocation_return_value(helper->invocation, NULL);
}

static FuPolkitAuthorityCheckFlags
fu_dbus_daemon_engine_request_get_authority_check_flags(FuEngineRequest *request)
{
	FuPolkitAuthorityCheckFlags auth_flags =
	    FU_POLKIT_AUTHORITY_CHECK_FLAG_ALLOW_USER_INTERACTION;
	if (fu_engine_request_has_converter_flag(request, FWUPD_CODEC_FLAG_TRUSTED))
		auth_flags |= FU_POLKIT_AUTHORITY_CHECK_FLAG_USER_IS_TRUSTED;
	return auth_flags;
}

#ifdef HAVE_GIO_UNIX
static void
fu_dbus_daemon_authorize_install_queue(FuMainAuthHelper *helper);

static void
fu_dbus_daemon_authorize_install_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(FuMainAuthHelper) helper = (FuMainAuthHelper *)user_data;
	g_autoptr(GError) error = NULL;

	/* get result */
	if (!fu_polkit_authority_check_finish(FU_POLKIT_AUTHORITY(source), res, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(helper->invocation, error);
		return;
	}

	/* do the next authentication action ID */
	fu_dbus_daemon_authorize_install_queue(g_steal_pointer(&helper));
}

static void
fu_dbus_daemon_authorize_install_queue(FuMainAuthHelper *helper_ref)
{
	FuDbusDaemon *self = helper_ref->self;
	g_autoptr(FuMainAuthHelper) helper = helper_ref;
	g_autoptr(GError) error = NULL;
	gboolean ret;
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(helper->self));

	/* still more things to to authenticate */
	if (helper->action_ids->len > 0) {
		g_autofree gchar *action_id = g_strdup(g_ptr_array_index(helper->action_ids, 0));
		g_autofree gchar *sender = g_strdup(fu_client_get_sender(helper->client));
		g_autoptr(FuEngineRequest) request = g_object_ref(helper->request);
		g_ptr_array_remove_index(helper->action_ids, 0);
		fu_polkit_authority_check(
		    self->authority,
		    sender,
		    action_id,
		    fu_dbus_daemon_engine_request_get_authority_check_flags(request),
		    NULL,
		    fu_dbus_daemon_authorize_install_cb,
		    g_steal_pointer(&helper));
		return;
	}

	/* all authenticated, so install all the things */
	fu_progress_set_profile(helper->progress, g_getenv("FWUPD_VERBOSE") != NULL);
	g_signal_connect(FU_PROGRESS(helper->progress),
			 "percentage-changed",
			 G_CALLBACK(fu_dbus_daemon_progress_percentage_changed_cb),
			 helper->self);
	g_signal_connect(FU_PROGRESS(helper->progress),
			 "status-changed",
			 G_CALLBACK(fu_dbus_daemon_progress_status_changed_cb),
			 helper->self);

	/* all authenticated, so install all the things */
	fu_daemon_set_update_in_progress(FU_DAEMON(self), TRUE);
	ret = fu_engine_install_releases(engine,
					 helper->request,
					 helper->releases,
					 helper->cabinet,
					 helper->progress,
					 helper->flags,
					 &error);
	fu_daemon_set_update_in_progress(FU_DAEMON(self), FALSE);
	if (fu_daemon_get_pending_stop(FU_DAEMON(self))) {
		g_set_error_literal(&error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "daemon was stopped");
		fu_dbus_daemon_method_invocation_return_gerror(helper->invocation, error);
		return;
	}
	if (!ret) {
		fu_dbus_daemon_method_invocation_return_gerror(helper->invocation, error);
		return;
	}

	/* success */
	g_dbus_method_invocation_return_value(helper->invocation, NULL);
}
#endif /* HAVE_GIO_UNIX */

#ifdef HAVE_GIO_UNIX
static gint
fu_dbus_daemon_release_sort_cb(gconstpointer a, gconstpointer b)
{
	FuRelease *release1 = *((FuRelease **)a);
	FuRelease *release2 = *((FuRelease **)b);
	return fu_release_compare(release1, release2);
}

static gboolean
fu_dbus_daemon_install_with_helper_device(FuMainAuthHelper *helper,
					  XbNode *component,
					  FuDevice *device,
					  GError **error)
{
	FuDbusDaemon *self = helper->self;
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) releases = NULL;

	/* is this component valid for the device */
	fu_release_set_device(release, device);
	fu_release_set_request(release, helper->request);
	if (helper->remote_id != NULL) {
		fu_release_set_remote(release,
				      fu_engine_get_remote_by_id(engine, helper->remote_id, NULL));
	}
	if (!fu_release_load(release,
			     helper->cabinet,
			     component,
			     NULL,
			     helper->flags | FWUPD_INSTALL_FLAG_FORCE,
			     &error_local)) {
		g_ptr_array_add(helper->errors, g_steal_pointer(&error_local));
		return TRUE;
	}
	if (!fu_engine_requirements_check(engine,
					  release,
					  helper->flags | FWUPD_INSTALL_FLAG_IGNORE_REQUIREMENTS,
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

	/* sync update message from CAB */
	fu_device_ensure_from_component(device, component);
	fu_device_incorporate_from_component(device, component);

	/* install each intermediate release */
	releases = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_INSTALL_ALL_RELEASES)) {
		g_autoptr(GPtrArray) rels = NULL;
		g_autoptr(XbQuery) query = NULL;

		/* we get this one "for free" */
		g_ptr_array_add(releases, g_object_ref(release));

		query = xb_query_new_full(xb_node_get_silo(component),
					  "releases/release",
					  XB_QUERY_FLAG_FORCE_NODE_CACHE,
					  error);
		if (query == NULL)
			return FALSE;
		rels = xb_node_query_full(component, query, NULL);
		/* add all but the first entry */
		for (guint i = 1; i < rels->len; i++) {
			XbNode *rel = g_ptr_array_index(rels, i);
			g_autoptr(FuRelease) release2 = fu_release_new();
			g_autoptr(GError) error_loop = NULL;
			fu_release_set_device(release2, device);
			fu_release_set_request(release2, helper->request);
			if (!fu_release_load(release2,
					     helper->cabinet,
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
		if (!fu_engine_requirements_check(engine,
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
		if (!fu_engine_check_trust(engine, release_tmp, &error_local)) {
			g_ptr_array_add(helper->errors, g_steal_pointer(&error_local));
			continue;
		}

		/* get the action IDs for the valid device */
		if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED)) {
			const gchar *action_id = fu_release_get_action_id(release_tmp);
			if (!g_ptr_array_find(helper->action_ids, action_id, NULL))
				g_ptr_array_add(helper->action_ids, g_strdup(action_id));
		}
		g_ptr_array_add(helper->releases, g_object_ref(release_tmp));
	}

	/* success */
	return TRUE;
}

static gboolean
fu_dbus_daemon_install_with_helper(FuMainAuthHelper *helper_ref, GError **error)
{
	FuDbusDaemon *self = helper_ref->self;
	g_autoptr(FuMainAuthHelper) helper = helper_ref;
	g_autoptr(GPtrArray) components = NULL;
	g_autoptr(GPtrArray) devices_possible = NULL;
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(helper->self));

	/* get a list of devices that in some way match the device_id */
	if (g_strcmp0(helper->device_id, FWUPD_DEVICE_ID_ANY) == 0) {
		devices_possible = fu_engine_get_devices(engine, error);
		if (devices_possible == NULL)
			return FALSE;
	} else {
		g_autoptr(FuDevice) device = NULL;
		device = fu_engine_get_device(engine, helper->device_id, error);
		if (device == NULL)
			return FALSE;
		devices_possible =
		    fu_engine_get_devices_by_composite_id(engine,
							  fu_device_get_composite_id(device),
							  error);
		if (devices_possible == NULL)
			return FALSE;
	}

	/* parse silo */
	helper->cabinet = fu_engine_build_cabinet_from_stream(engine, helper->stream, error);
	if (helper->cabinet == NULL)
		return FALSE;

	/* for each component in the silo */
	components = fu_cabinet_get_components(helper->cabinet, error);
	if (components == NULL)
		return FALSE;
	helper->action_ids = g_ptr_array_new_with_free_func(g_free);
	helper->releases = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	helper->errors = g_ptr_array_new_with_free_func((GDestroyNotify)g_error_free);
	helper->remote_id = fu_engine_get_remote_id_for_stream(engine, helper->stream);

	/* do any devices pass the requirements */
	for (guint i = 0; i < components->len; i++) {
		XbNode *component = g_ptr_array_index(components, i);
		for (guint j = 0; j < devices_possible->len; j++) {
			FuDevice *device = g_ptr_array_index(devices_possible, j);
			g_debug("testing device %u [%s] with component %u",
				j,
				fu_device_get_id(device),
				i);
			if (!fu_dbus_daemon_install_with_helper_device(helper,
								       component,
								       device,
								       error))
				return FALSE;
		}
	}

	/* order the install tasks by the device priority */
	g_ptr_array_sort(helper->releases, fu_dbus_daemon_release_sort_cb);

	/* nothing suitable */
	if (helper->releases->len == 0) {
		GError *error_tmp = fu_engine_error_array_get_best(helper->errors);
		g_propagate_error(error, error_tmp);
		return FALSE;
	}

	/* authenticate all things in the action_ids */
	fu_dbus_daemon_set_status(self, FWUPD_STATUS_WAITING_FOR_AUTH);
	fu_dbus_daemon_authorize_install_queue(g_steal_pointer(&helper));
	return TRUE;
}
#endif /* HAVE_GIO_UNIX */

static gboolean
fu_dbus_daemon_device_id_valid(const gchar *device_id, GError **error)
{
	if (g_strcmp0(device_id, FWUPD_DEVICE_ID_ANY) == 0)
		return TRUE;
	if (device_id != NULL && strlen(device_id) >= 4)
		return TRUE;
	g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "invalid device ID: %s", device_id);
	return FALSE;
}

typedef struct {
	gchar *id;
	gchar *sender;
	guint watcher_id;
} FuDbusDaemonSystemInhibit;

static void
fu_dbus_daemon_system_inhibit_free(FuDbusDaemonSystemInhibit *inhibit)
{
	g_bus_unwatch_name(inhibit->watcher_id);
	g_free(inhibit->id);
	g_free(inhibit->sender);
	g_free(inhibit);
}

static void
fu_dbus_daemon_ensure_system_inhibit(FuDbusDaemon *self)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	FuContext *ctx = fu_engine_get_context(engine);
	if (self->system_inhibits->len > 0) {
		fu_context_add_flag(ctx, FU_CONTEXT_FLAG_SYSTEM_INHIBIT);
		return;
	}
	fu_context_remove_flag(ctx, FU_CONTEXT_FLAG_SYSTEM_INHIBIT);
}

static void
fu_dbus_daemon_inhibit_name_vanished_cb(GDBusConnection *connection,
					const gchar *name,
					gpointer user_data)
{
	FuDbusDaemon *self = FU_DBUS_DAEMON(user_data);
	for (guint i = 0; i < self->system_inhibits->len; i++) {
		FuDbusDaemonSystemInhibit *inhibit = g_ptr_array_index(self->system_inhibits, i);
		if (g_strcmp0(inhibit->sender, name) == 0) {
			g_debug("removing %s as %s vanished without calling Uninhibit",
				inhibit->id,
				name);
			g_ptr_array_remove_index(self->system_inhibits, i);
			fu_dbus_daemon_ensure_system_inhibit(self);
			break;
		}
	}
}

#ifdef HAVE_GIO_UNIX
static void
fu_dbus_daemon_client_flags_notify_cb(FuClient *client, GParamSpec *pspec, FuMainAuthHelper *helper)
{
	if (!fu_client_has_flag(client, FU_CLIENT_FLAG_ACTIVE)) {
		g_info("%s vanished before completion of install on %s",
		       fu_client_get_sender(client),
		       helper->device_id);
		fu_progress_add_flag(helper->progress, FU_PROGRESS_FLAG_NO_SENDER);
	}
}
#endif

static GInputStream *
fu_dbus_daemon_invocation_get_input_stream(GDBusMethodInvocation *invocation, GError **error)
{
#ifdef HAVE_GIO_UNIX
	GDBusMessage *message;
	GUnixFDList *fd_list;
	gint fd;
	g_autoptr(GInputStream) stream = NULL;

	/* get the fd */
	message = g_dbus_method_invocation_get_message(invocation);
	fd_list = g_dbus_message_get_unix_fd_list(message);
	if (fd_list == NULL || g_unix_fd_list_get_length(fd_list) != 1) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "invalid handle");
		return NULL;
	}
	fd = g_unix_fd_list_get(fd_list, 0, error);
	if (fd < 0)
		return NULL;

	/* get details about the file (will close the fd when done) */
	stream = fu_unix_seekable_input_stream_new(fd, TRUE);
	if (stream == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "invalid stream");
		return NULL;
	}
	return g_steal_pointer(&stream);
#else
	g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "unsupported feature");
	return NULL;
#endif
}

static GOutputStream *
fu_dbus_daemon_invocation_get_output_stream(GDBusMethodInvocation *invocation, GError **error)
{
#ifdef HAVE_GIO_UNIX
	GDBusMessage *message;
	GUnixFDList *fd_list;
	gint fd;
	g_autoptr(GOutputStream) stream = NULL;

	/* get the fd */
	message = g_dbus_method_invocation_get_message(invocation);
	fd_list = g_dbus_message_get_unix_fd_list(message);
	if (fd_list == NULL || g_unix_fd_list_get_length(fd_list) != 1) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "invalid handle");
		return NULL;
	}
	fd = g_unix_fd_list_get(fd_list, 0, error);
	if (fd < 0)
		return NULL;

	/* get details about the file (will close the fd when done) */
	stream = g_unix_output_stream_new(fd, TRUE);
	if (stream == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "invalid stream");
		return NULL;
	}
	return g_steal_pointer(&stream);
#else
	g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "unsupported feature");
	return NULL;
#endif
}

static gboolean
fu_dbus_daemon_hsi_supported(FuDbusDaemon *self, GError **error)
{
#ifdef HAVE_HSI
	g_autofree gchar *sysfsfwdir = NULL;
	g_autofree gchar *xen_privileged_fn = NULL;

	if (g_getenv("UMOCKDEV_DIR") != NULL)
		return TRUE;
	if (fu_daemon_get_machine_kind(FU_DAEMON(self)) == FU_DAEMON_MACHINE_KIND_PHYSICAL)
		return TRUE;

	sysfsfwdir = fu_path_from_kind(FU_PATH_KIND_SYSFSDIR_FW_ATTRIB);
	/* privileged xen can access most hardware */
	xen_privileged_fn =
	    g_build_filename(sysfsfwdir, "hypervisor", "start_flags", "privileged", NULL);
	if (g_file_test(xen_privileged_fn, G_FILE_TEST_EXISTS)) {
		g_autofree gchar *contents = NULL;

		if (g_file_get_contents(xen_privileged_fn, &contents, NULL, NULL)) {
			if (g_strcmp0(contents, "1") == 0)
				return TRUE;
		}
	}

	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "HSI unavailable for hypervisor");
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "HSI support not enabled");

#endif
	return FALSE;
}

static void
fu_dbus_daemon_method_get_devices(FuDbusDaemon *self,
				  GVariant *parameters,
				  FuEngineRequest *request,
				  GDBusMethodInvocation *invocation)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	GVariant *val;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;

	devices = fu_engine_get_devices(engine, &error);
	if (devices == NULL) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}
	val = fu_dbus_daemon_device_array_to_variant(self, request, devices, &error);
	if (val == NULL) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}
	g_dbus_method_invocation_return_value(invocation, val);
}

static void
fu_dbus_daemon_method_get_plugins(FuDbusDaemon *self,
				  GVariant *parameters,
				  FuEngineRequest *request,
				  GDBusMethodInvocation *invocation)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	GVariant *val;

	val = fwupd_codec_array_to_variant(fu_engine_get_plugins(engine), FWUPD_CODEC_FLAG_NONE);
	g_dbus_method_invocation_return_value(invocation, val);
}

static void
fu_dbus_daemon_method_get_releases(FuDbusDaemon *self,
				   GVariant *parameters,
				   FuEngineRequest *request,
				   GDBusMethodInvocation *invocation)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	const gchar *device_id;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) releases = NULL;

	g_variant_get(parameters, "(&s)", &device_id);
	if (!fu_dbus_daemon_device_id_valid(device_id, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}
	releases = fu_engine_get_releases(engine, request, device_id, &error);
	if (releases == NULL) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}
	g_dbus_method_invocation_return_value(
	    invocation,
	    fwupd_codec_array_to_variant(releases, FWUPD_CODEC_FLAG_NONE));
}

static void
fu_dbus_daemon_method_get_approved_firmware(FuDbusDaemon *self,
					    GVariant *parameters,
					    FuEngineRequest *request,
					    GDBusMethodInvocation *invocation)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	GPtrArray *checksums = fu_engine_get_approved_firmware(engine);
	GVariantBuilder builder;
	GVariant *val;

	g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
	for (guint i = 0; i < checksums->len; i++) {
		const gchar *checksum = g_ptr_array_index(checksums, i);
		g_variant_builder_add_value(&builder, g_variant_new_string(checksum));
	}
	val = g_variant_builder_end(&builder);
	g_dbus_method_invocation_return_value(invocation, g_variant_new_tuple(&val, 1));
}

static void
fu_dbus_daemon_method_get_blocked_firmware(FuDbusDaemon *self,
					   GVariant *parameters,
					   FuEngineRequest *request,
					   GDBusMethodInvocation *invocation)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	GPtrArray *checksums = fu_engine_get_blocked_firmware(engine);
	GVariantBuilder builder;
	GVariant *val;

	g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
	for (guint i = 0; i < checksums->len; i++) {
		const gchar *checksum = g_ptr_array_index(checksums, i);
		g_variant_builder_add_value(&builder, g_variant_new_string(checksum));
	}
	val = g_variant_builder_end(&builder);
	g_dbus_method_invocation_return_value(invocation, g_variant_new_tuple(&val, 1));
}

static void
fu_dbus_daemon_method_get_report_metadata(FuDbusDaemon *self,
					  GVariant *parameters,
					  FuEngineRequest *request,
					  GDBusMethodInvocation *invocation)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	const gchar *key;
	const gchar *value;
	GHashTableIter iter;
	GVariantBuilder builder;
	GVariant *val;
	g_autoptr(GError) error = NULL;
	g_autoptr(GHashTable) metadata = NULL;

	metadata = fu_engine_get_report_metadata(engine, &error);
	if (metadata == NULL) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}
	g_variant_builder_init(&builder, G_VARIANT_TYPE("a{ss}"));
	g_hash_table_iter_init(&iter, metadata);
	while (g_hash_table_iter_next(&iter, (gpointer *)&key, (gpointer *)&value)) {
		g_variant_builder_add_value(&builder, g_variant_new("{ss}", key, value));
	}
	val = g_variant_builder_end(&builder);
	g_dbus_method_invocation_return_value(invocation, g_variant_new_tuple(&val, 1));
}

static void
fu_dbus_daemon_method_set_approved_firmware(FuDbusDaemon *self,
					    GVariant *parameters,
					    FuEngineRequest *request,
					    GDBusMethodInvocation *invocation)
{
	g_autofree gchar *checksums_str = NULL;
	g_auto(GStrv) checksums = NULL;
	g_autoptr(FuMainAuthHelper) helper = NULL;

	g_variant_get(parameters, "(^as)", &checksums);
	checksums_str = g_strjoinv(",", checksums);

	/* authenticate */
	fu_dbus_daemon_set_status(self, FWUPD_STATUS_WAITING_FOR_AUTH);
	helper = g_new0(FuMainAuthHelper, 1);
	helper->self = self;
	helper->flags = FWUPD_INSTALL_FLAG_NO_SEARCH;
	helper->request = g_object_ref(request);
	helper->invocation = g_object_ref(invocation);
	helper->checksums = g_ptr_array_new_with_free_func(g_free);
	for (guint i = 0; checksums[i] != NULL; i++)
		g_ptr_array_add(helper->checksums, g_strdup(checksums[i]));
	fu_polkit_authority_check(self->authority,
				  fu_engine_request_get_sender(request),
				  "org.freedesktop.fwupd.set-approved-firmware",
				  fu_dbus_daemon_engine_request_get_authority_check_flags(request),
				  NULL,
				  fu_dbus_daemon_authorize_set_approved_firmware_cb,
				  g_steal_pointer(&helper));
}

static void
fu_dbus_daemon_method_set_blocked_firmware(FuDbusDaemon *self,
					   GVariant *parameters,
					   FuEngineRequest *request,
					   GDBusMethodInvocation *invocation)
{
	g_autofree gchar *checksums_str = NULL;
	g_auto(GStrv) checksums = NULL;
	g_autoptr(FuMainAuthHelper) helper = NULL;

	g_variant_get(parameters, "(^as)", &checksums);
	checksums_str = g_strjoinv(",", checksums);

	/* authenticate */
	fu_dbus_daemon_set_status(self, FWUPD_STATUS_WAITING_FOR_AUTH);
	helper = g_new0(FuMainAuthHelper, 1);
	helper->self = self;
	helper->request = g_object_ref(request);
	helper->invocation = g_object_ref(invocation);
	helper->checksums = g_ptr_array_new_with_free_func(g_free);
	for (guint i = 0; checksums[i] != NULL; i++)
		g_ptr_array_add(helper->checksums, g_strdup(checksums[i]));
	fu_polkit_authority_check(self->authority,
				  fu_engine_request_get_sender(request),
				  "org.freedesktop.fwupd.set-approved-firmware",
				  fu_dbus_daemon_engine_request_get_authority_check_flags(request),
				  NULL,
				  fu_dbus_daemon_authorize_set_blocked_firmware_cb,
				  g_steal_pointer(&helper));
}

static void
fu_dbus_daemon_method_quit(FuDbusDaemon *self,
			   GVariant *parameters,
			   FuEngineRequest *request,
			   GDBusMethodInvocation *invocation)
{
	if (!fu_engine_request_has_converter_flag(request, FWUPD_CODEC_FLAG_TRUSTED)) {
		g_dbus_method_invocation_return_error_literal(invocation,
							      FWUPD_ERROR,
							      FWUPD_ERROR_PERMISSION_DENIED,
							      "Permission denied");
		return;
	}
	fu_daemon_schedule_process_quit(FU_DAEMON(self));
	g_dbus_method_invocation_return_value(invocation, NULL);
}

static void
fu_dbus_daemon_method_self_sign(FuDbusDaemon *self,
				GVariant *parameters,
				FuEngineRequest *request,
				GDBusMethodInvocation *invocation)
{
	GVariant *prop_value;
	const gchar *prop_key;
	g_autofree gchar *value = NULL;
	g_autoptr(FuMainAuthHelper) helper = NULL;
	g_autoptr(GVariantIter) iter = NULL;

	g_variant_get(parameters, "(sa{sv})", &value, &iter);

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
	fu_dbus_daemon_set_status(self, FWUPD_STATUS_WAITING_FOR_AUTH);
	helper->self = self;
	helper->value = g_steal_pointer(&value);
	helper->request = g_object_ref(request);
	helper->invocation = g_object_ref(invocation);
	fu_polkit_authority_check(self->authority,
				  fu_engine_request_get_sender(request),
				  "org.freedesktop.fwupd.self-sign",
				  fu_dbus_daemon_engine_request_get_authority_check_flags(request),
				  NULL,
				  fu_dbus_daemon_authorize_self_sign_cb,
				  g_steal_pointer(&helper));
}

static void
fu_dbus_daemon_method_get_downgrades(FuDbusDaemon *self,
				     GVariant *parameters,
				     FuEngineRequest *request,
				     GDBusMethodInvocation *invocation)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	const gchar *device_id;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) releases = NULL;

	g_variant_get(parameters, "(&s)", &device_id);
	if (!fu_dbus_daemon_device_id_valid(device_id, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}
	releases = fu_engine_get_downgrades(engine, request, device_id, &error);
	if (releases == NULL) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}
	g_dbus_method_invocation_return_value(
	    invocation,
	    fwupd_codec_array_to_variant(releases, FWUPD_CODEC_FLAG_NONE));
}

static void
fu_dbus_daemon_method_get_upgrades(FuDbusDaemon *self,
				   GVariant *parameters,
				   FuEngineRequest *request,
				   GDBusMethodInvocation *invocation)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	const gchar *device_id;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) releases = NULL;

	g_variant_get(parameters, "(&s)", &device_id);
	if (!fu_dbus_daemon_device_id_valid(device_id, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}
	releases = fu_engine_get_upgrades(engine, request, device_id, &error);
	if (releases == NULL) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}
	g_dbus_method_invocation_return_value(
	    invocation,
	    fwupd_codec_array_to_variant(releases, FWUPD_CODEC_FLAG_NONE));
}

static void
fu_dbus_daemon_method_get_remotes(FuDbusDaemon *self,
				  GVariant *parameters,
				  FuEngineRequest *request,
				  GDBusMethodInvocation *invocation)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	g_autoptr(GError) error = NULL;

	g_autoptr(GPtrArray) remotes = NULL;
	remotes = fu_engine_get_remotes(engine, &error);
	if (remotes == NULL) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}
	g_dbus_method_invocation_return_value(
	    invocation,
	    fwupd_codec_array_to_variant(remotes, FWUPD_CODEC_FLAG_NONE));
}

static void
fu_dbus_daemon_method_get_history(FuDbusDaemon *self,
				  GVariant *parameters,
				  FuEngineRequest *request,
				  GDBusMethodInvocation *invocation)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	GVariant *val;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;

	devices = fu_engine_get_history(engine, &error);
	if (devices == NULL) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}
	val = fu_dbus_daemon_device_array_to_variant(self, request, devices, &error);
	if (val == NULL) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}
	g_dbus_method_invocation_return_value(invocation, val);
}

static void
fu_dbus_daemon_method_get_host_security_attrs(FuDbusDaemon *self,
					      GVariant *parameters,
					      FuEngineRequest *request,
					      GDBusMethodInvocation *invocation)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	g_autoptr(GError) error = NULL;

	g_autoptr(FuSecurityAttrs) attrs = NULL;

	if (!fu_dbus_daemon_hsi_supported(self, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}
	attrs = fu_engine_get_host_security_attrs(engine);
	g_dbus_method_invocation_return_value(invocation, fu_security_attrs_to_variant(attrs));
}

static void
fu_dbus_daemon_method_get_host_security_events(FuDbusDaemon *self,
					       GVariant *parameters,
					       FuEngineRequest *request,
					       GDBusMethodInvocation *invocation)
{
#ifdef HAVE_HSI
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	guint limit = 0;
	g_autoptr(FuSecurityAttrs) attrs = NULL;
	g_autoptr(GError) error = NULL;

	g_variant_get(parameters, "(u)", &limit);
	attrs = fu_engine_get_host_security_events(engine, limit, &error);
	if (attrs == NULL) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}
	g_dbus_method_invocation_return_value(invocation, fu_security_attrs_to_variant(attrs));
#else
	g_dbus_method_invocation_return_error_literal(invocation,
						      FWUPD_ERROR,
						      FWUPD_ERROR_NOT_SUPPORTED,
						      "HSI support not enabled");
#endif
}

static void
fu_dbus_daemon_method_clear_results(FuDbusDaemon *self,
				    GVariant *parameters,
				    FuEngineRequest *request,
				    GDBusMethodInvocation *invocation)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	const gchar *device_id;
	g_autoptr(GError) error = NULL;

	g_variant_get(parameters, "(&s)", &device_id);
	if (!fu_engine_clear_results(engine, device_id, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}
	g_dbus_method_invocation_return_value(invocation, NULL);
}

static void
fu_dbus_daemon_method_emulation_load(FuDbusDaemon *self,
				     GVariant *parameters,
				     FuEngineRequest *request,
				     GDBusMethodInvocation *invocation)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	gint32 fd_handle = 0;
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;

	g_variant_get(parameters, "(h)", &fd_handle);

	/* get stream */
	stream = fu_dbus_daemon_invocation_get_input_stream(invocation, &error);
	if (stream == NULL) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}

	/* load data into engine */
	if (!fu_engine_emulation_load(engine, stream, &error)) {
		g_dbus_method_invocation_return_error(invocation,
						      error->domain,
						      error->code,
						      "failed to load emulation data: %s",
						      error->message);
		return;
	}

	/* success */
	g_dbus_method_invocation_return_value(invocation, NULL);
}

static void
fu_dbus_daemon_method_emulation_save(FuDbusDaemon *self,
				     GVariant *parameters,
				     FuEngineRequest *request,
				     GDBusMethodInvocation *invocation)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	gint32 fd_handle = 0;
	g_autoptr(GError) error = NULL;
	g_autoptr(GOutputStream) stream = NULL;

	g_variant_get(parameters, "(h)", &fd_handle);

	/* get stream */
	stream = fu_dbus_daemon_invocation_get_output_stream(invocation, &error);
	if (stream == NULL) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}

	/* save data from engine */
	if (!fu_engine_emulation_save(engine, stream, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}

	/* success */
	g_dbus_method_invocation_return_value(invocation, NULL);
}

static void
fu_dbus_daemon_method_modify_device(FuDbusDaemon *self,
				    GVariant *parameters,
				    FuEngineRequest *request,
				    GDBusMethodInvocation *invocation)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	const gchar *device_id;
	const gchar *key = NULL;
	const gchar *value = NULL;
	g_autoptr(GError) error = NULL;

	/* check the id exists */
	g_variant_get(parameters, "(&s&s&s)", &device_id, &key, &value);
	if (!fu_engine_modify_device(engine, device_id, key, value, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}
	g_dbus_method_invocation_return_value(invocation, NULL);
}

static void
fu_dbus_daemon_method_get_results(FuDbusDaemon *self,
				  GVariant *parameters,
				  FuEngineRequest *request,
				  GDBusMethodInvocation *invocation)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	GVariant *val;
	g_autoptr(GError) error = NULL;

	const gchar *device_id = NULL;
	g_autoptr(FwupdDevice) result = NULL;
	g_variant_get(parameters, "(&s)", &device_id);
	if (!fu_dbus_daemon_device_id_valid(device_id, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}
	result = fu_engine_get_results(engine, device_id, &error);
	if (result == NULL) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}
	val = fwupd_codec_to_variant(FWUPD_CODEC(result), FWUPD_CODEC_FLAG_TRUSTED);
	g_dbus_method_invocation_return_value(invocation, g_variant_new_tuple(&val, 1));
}

static void
fu_dbus_daemon_method_update_metadata(FuDbusDaemon *self,
				      GVariant *parameters,
				      FuEngineRequest *request,
				      GDBusMethodInvocation *invocation)
{
#ifdef HAVE_GIO_UNIX
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	GDBusMessage *message;
	GUnixFDList *fd_list;
	const gchar *remote_id = NULL;
	gint fd_data;
	gint fd_sig;
	g_autoptr(GError) error = NULL;

	g_variant_get(parameters, "(&shh)", &remote_id, &fd_data, &fd_sig);

	/* update the metadata store */
	message = g_dbus_method_invocation_get_message(invocation);
	fd_list = g_dbus_message_get_unix_fd_list(message);
	if (fd_list == NULL || g_unix_fd_list_get_length(fd_list) != 2) {
		g_set_error(&error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "invalid handle");
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}
	fd_data = g_unix_fd_list_get(fd_list, 0, &error);
	if (fd_data < 0) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}
	fd_sig = g_unix_fd_list_get(fd_list, 1, &error);
	if (fd_sig < 0) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}

	/* store new metadata (will close the fds when done) */
	if (!fu_engine_update_metadata(engine, remote_id, fd_data, fd_sig, &error)) {
		g_prefix_error(&error, "Failed to update metadata for %s: ", remote_id);
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}
	g_dbus_method_invocation_return_value(invocation, NULL);
#else
	g_dbus_method_invocation_return_error_literal(invocation,
						      FWUPD_ERROR,
						      FWUPD_ERROR_INTERNAL,
						      "unsupported feature");
#endif /* HAVE_GIO_UNIX */
}

static void
fu_dbus_daemon_method_unlock(FuDbusDaemon *self,
			     GVariant *parameters,
			     FuEngineRequest *request,
			     GDBusMethodInvocation *invocation)
{
	const gchar *device_id = NULL;
	g_autoptr(FuMainAuthHelper) helper = NULL;
	g_autoptr(GError) error = NULL;

	g_variant_get(parameters, "(&s)", &device_id);
	if (!fu_dbus_daemon_device_id_valid(device_id, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}

	/* authenticate */
	fu_dbus_daemon_set_status(self, FWUPD_STATUS_WAITING_FOR_AUTH);
	helper = g_new0(FuMainAuthHelper, 1);
	helper->self = self;
	helper->request = g_object_ref(request);
	helper->invocation = g_object_ref(invocation);
	helper->device_id = g_strdup(device_id);
	fu_polkit_authority_check(self->authority,
				  fu_engine_request_get_sender(request),
				  "org.freedesktop.fwupd.device-unlock",
				  fu_dbus_daemon_engine_request_get_authority_check_flags(request),
				  NULL,
				  fu_dbus_daemon_authorize_unlock_cb,
				  g_steal_pointer(&helper));
}

static void
fu_dbus_daemon_method_activate(FuDbusDaemon *self,
			       GVariant *parameters,
			       FuEngineRequest *request,
			       GDBusMethodInvocation *invocation)
{
	const gchar *device_id = NULL;
	g_autoptr(FuMainAuthHelper) helper = NULL;
	g_autoptr(GError) error = NULL;

	g_variant_get(parameters, "(&s)", &device_id);
	if (!fu_dbus_daemon_device_id_valid(device_id, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}

	/* authenticate */
	fu_dbus_daemon_set_status(self, FWUPD_STATUS_WAITING_FOR_AUTH);
	helper = g_new0(FuMainAuthHelper, 1);
	helper->self = self;
	helper->request = g_object_ref(request);
	helper->invocation = g_object_ref(invocation);
	helper->device_id = g_strdup(device_id);
	fu_polkit_authority_check(self->authority,
				  fu_engine_request_get_sender(request),
				  "org.freedesktop.fwupd.device-activate",
				  fu_dbus_daemon_engine_request_get_authority_check_flags(request),
				  NULL,
				  fu_dbus_daemon_authorize_activate_cb,
				  g_steal_pointer(&helper));
}

static void
fu_dbus_daemon_method_modify_config(FuDbusDaemon *self,
				    GVariant *parameters,
				    FuEngineRequest *request,
				    GDBusMethodInvocation *invocation)
{
	g_autofree gchar *key = NULL;
	g_autofree gchar *section = NULL;
	g_autofree gchar *value = NULL;
	g_autoptr(FuMainAuthHelper) helper = NULL;

	g_variant_get(parameters, "(sss)", &section, &key, &value);

	/* authenticate */
	helper = g_new0(FuMainAuthHelper, 1);
	helper->self = self;
	helper->section = g_steal_pointer(&section);
	helper->key = g_steal_pointer(&key);
	helper->value = g_steal_pointer(&value);
	helper->request = g_object_ref(request);
	helper->invocation = g_object_ref(invocation);
	fu_polkit_authority_check(self->authority,
				  fu_engine_request_get_sender(request),
				  "org.freedesktop.fwupd.modify-config",
				  fu_dbus_daemon_engine_request_get_authority_check_flags(request),
				  NULL,
				  fu_dbus_daemon_modify_config_cb,
				  g_steal_pointer(&helper));
}

static void
fu_dbus_daemon_method_reset_config(FuDbusDaemon *self,
				   GVariant *parameters,
				   FuEngineRequest *request,
				   GDBusMethodInvocation *invocation)
{
	g_autofree gchar *section = NULL;
	g_autoptr(FuMainAuthHelper) helper = NULL;

	g_variant_get(parameters, "(s)", &section);

	/* authenticate */
	helper = g_new0(FuMainAuthHelper, 1);
	helper->self = self;
	helper->section = g_steal_pointer(&section);
	helper->request = g_object_ref(request);
	helper->invocation = g_object_ref(invocation);
	fu_polkit_authority_check(self->authority,
				  fu_engine_request_get_sender(request),
				  "org.freedesktop.fwupd.reset-config",
				  fu_dbus_daemon_engine_request_get_authority_check_flags(request),
				  NULL,
				  fu_dbus_daemon_reset_config_cb,
				  g_steal_pointer(&helper));
}

static void
fu_dbus_daemon_method_modify_remote(FuDbusDaemon *self,
				    GVariant *parameters,
				    FuEngineRequest *request,
				    GDBusMethodInvocation *invocation)
{
	const gchar *remote_id = NULL;
	const gchar *key = NULL;
	const gchar *value = NULL;
	g_autoptr(FuMainAuthHelper) helper = NULL;

	/* check the id exists */
	g_variant_get(parameters, "(&s&s&s)", &remote_id, &key, &value);

	/* create helper object */
	helper = g_new0(FuMainAuthHelper, 1);
	helper->request = g_object_ref(request);
	helper->invocation = g_object_ref(invocation);
	helper->remote_id = g_strdup(remote_id);
	helper->key = g_strdup(key);
	helper->value = g_strdup(value);
	helper->self = self;

	/* authenticate */
	fu_dbus_daemon_set_status(self, FWUPD_STATUS_WAITING_FOR_AUTH);
	fu_polkit_authority_check(self->authority,
				  fu_engine_request_get_sender(request),
				  "org.freedesktop.fwupd.modify-remote",
				  fu_dbus_daemon_engine_request_get_authority_check_flags(request),
				  NULL,
				  fu_dbus_daemon_authorize_modify_remote_cb,
				  g_steal_pointer(&helper));
}

static void
fu_dbus_daemon_method_verify_update(FuDbusDaemon *self,
				    GVariant *parameters,
				    FuEngineRequest *request,
				    GDBusMethodInvocation *invocation)
{
	const gchar *device_id = NULL;
	g_autoptr(FuMainAuthHelper) helper = NULL;
	g_autoptr(GError) error = NULL;

	/* check the id exists */
	g_variant_get(parameters, "(&s)", &device_id);

	if (!fu_dbus_daemon_device_id_valid(device_id, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}

	/* create helper object */
	helper = g_new0(FuMainAuthHelper, 1);
	helper->request = g_object_ref(request);
	helper->invocation = g_object_ref(invocation);
	helper->device_id = g_strdup(device_id);
	helper->self = self;

	/* authenticate */
	fu_dbus_daemon_set_status(self, FWUPD_STATUS_WAITING_FOR_AUTH);
	fu_polkit_authority_check(self->authority,
				  fu_engine_request_get_sender(request),
				  "org.freedesktop.fwupd.verify-update",
				  fu_dbus_daemon_engine_request_get_authority_check_flags(request),
				  NULL,
				  fu_dbus_daemon_authorize_verify_update_cb,
				  g_steal_pointer(&helper));
}

static void
fu_dbus_daemon_method_verify(FuDbusDaemon *self,
			     GVariant *parameters,
			     FuEngineRequest *request,
			     GDBusMethodInvocation *invocation)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	const gchar *device_id = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);

	g_variant_get(parameters, "(&s)", &device_id);
	if (!fu_dbus_daemon_device_id_valid(device_id, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}

	/* progress */
	fu_progress_set_profile(progress, g_getenv("FWUPD_VERBOSE") != NULL);
	g_signal_connect(FU_PROGRESS(progress),
			 "percentage-changed",
			 G_CALLBACK(fu_dbus_daemon_progress_percentage_changed_cb),
			 self);
	g_signal_connect(FU_PROGRESS(progress),
			 "status-changed",
			 G_CALLBACK(fu_dbus_daemon_progress_status_changed_cb),
			 self);

	if (!fu_engine_verify(engine, device_id, progress, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}
	g_dbus_method_invocation_return_value(invocation, NULL);
}

static void
fu_dbus_daemon_method_set_feature_flags(FuDbusDaemon *self,
					GVariant *parameters,
					FuEngineRequest *request,
					GDBusMethodInvocation *invocation)
{
	guint64 feature_flags_u64 = 0;
	g_autoptr(FuClient) client = NULL;

	g_variant_get(parameters, "(t)", &feature_flags_u64);

	/* old flags for the same sender will be automatically destroyed */
	client = fu_client_list_register(self->client_list, fu_engine_request_get_sender(request));
	fu_client_set_feature_flags(client, feature_flags_u64);
	g_dbus_method_invocation_return_value(invocation, NULL);
}

static void
fu_dbus_daemon_method_set_hints(FuDbusDaemon *self,
				GVariant *parameters,
				FuEngineRequest *request,
				GDBusMethodInvocation *invocation)
{
	const gchar *prop_key;
	const gchar *prop_value;
	g_autoptr(FuClient) client = NULL;
	g_autoptr(GVariantIter) iter = NULL;

	g_variant_get(parameters, "(a{ss})", &iter);
	client = fu_client_list_register(self->client_list, fu_engine_request_get_sender(request));
	while (g_variant_iter_next(iter, "{&s&s}", &prop_key, &prop_value)) {
		g_debug("got hint %s=%s", prop_key, prop_value);
		fu_client_insert_hint(client, prop_key, prop_value);
	}
	g_dbus_method_invocation_return_value(invocation, NULL);
}

static void
fu_dbus_daemon_method_inhibit(FuDbusDaemon *self,
			      GVariant *parameters,
			      FuEngineRequest *request,
			      GDBusMethodInvocation *invocation)
{
	FuDbusDaemonSystemInhibit *inhibit;
	const gchar *reason = NULL;

	g_variant_get(parameters, "(&s)", &reason);

	/* watch */
	inhibit = g_new0(FuDbusDaemonSystemInhibit, 1);
	inhibit->sender = g_strdup(fu_engine_request_get_sender(request));
	inhibit->id = g_strdup_printf("dbus-%i", g_random_int_range(1, G_MAXINT - 1));
	inhibit->watcher_id =
	    g_bus_watch_name_on_connection(self->connection,
					   fu_engine_request_get_sender(request),
					   G_BUS_NAME_WATCHER_FLAGS_NONE,
					   NULL,
					   fu_dbus_daemon_inhibit_name_vanished_cb,
					   self,
					   NULL);
	g_ptr_array_add(self->system_inhibits, inhibit);
	fu_dbus_daemon_ensure_system_inhibit(self);
	g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", inhibit->id));
}

static void
fu_dbus_daemon_method_install(FuDbusDaemon *self,
			      GVariant *parameters,
			      FuEngineRequest *request,
			      GDBusMethodInvocation *invocation)
{
#ifdef HAVE_GIO_UNIX
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	GVariant *prop_value;
	const gchar *device_id = NULL;
	const gchar *prop_key;
	gint32 fd_handle = 0;
	g_autoptr(FuMainAuthHelper) helper = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariantIter) iter = NULL;

	/* check the id exists */
	g_variant_get(parameters, "(&sha{sv})", &device_id, &fd_handle, &iter);
	if (!fu_dbus_daemon_device_id_valid(device_id, &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}

	/* create helper object */
	helper = g_new0(FuMainAuthHelper, 1);
	helper->request = g_object_ref(request);
	helper->progress = fu_progress_new(G_STRLOC);
	helper->invocation = g_object_ref(invocation);
	helper->device_id = g_strdup(device_id);
	helper->self = self;

	/* get flags */
	while (g_variant_iter_next(iter, "{&sv}", &prop_key, &prop_value)) {
		g_debug("got option %s", prop_key);
		if (g_strcmp0(prop_key, "install-flags") == 0)
			helper->flags = g_variant_get_uint64(prop_value);
		g_variant_unref(prop_value);
	}

	/* verify the client didn't send "internal" flags like no-search */
	if (helper->flags & ~FU_DAEMON_INSTALL_FLAG_MASK_SAFE) {
		FwupdInstallFlags flags_unsafe = helper->flags & ~FU_DAEMON_INSTALL_FLAG_MASK_SAFE;
		g_set_error(&error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "client sent unsupported flag: 0x%x [%s]",
			    (guint)flags_unsafe,
			    fwupd_install_flags_to_string(flags_unsafe));
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}

	/* get stream */
	helper->stream = fu_dbus_daemon_invocation_get_input_stream(invocation, &error);
	if (helper->stream == NULL) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}

	/* relax these */
	if (fu_engine_config_get_ignore_requirements(fu_engine_get_config(engine)))
		helper->flags |= FWUPD_INSTALL_FLAG_IGNORE_REQUIREMENTS;

	/* install all the things in the store */
	helper->client =
	    fu_client_list_register(self->client_list, fu_engine_request_get_sender(request));
	helper->client_sender_changed_id =
	    g_signal_connect(FU_CLIENT(helper->client),
			     "notify::flags",
			     G_CALLBACK(fu_dbus_daemon_client_flags_notify_cb),
			     helper);
	if (!fu_dbus_daemon_install_with_helper(g_steal_pointer(&helper), &error)) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}
#else
	g_dbus_method_invocation_return_error_literal(invocation,
						      FWUPD_ERROR,
						      FWUPD_ERROR_INTERNAL,
						      "unsupported feature");
#endif /* HAVE_GIO_UNIX */
}

static void
fu_dbus_daemon_method_uninhibit(FuDbusDaemon *self,
				GVariant *parameters,
				FuEngineRequest *request,
				GDBusMethodInvocation *invocation)
{
	const gchar *inhibit_id = NULL;
	gboolean found = FALSE;

	g_variant_get(parameters, "(&s)", &inhibit_id);

	/* find by id, then uninhibit device */
	for (guint i = 0; i < self->system_inhibits->len; i++) {
		FuDbusDaemonSystemInhibit *inhibit = g_ptr_array_index(self->system_inhibits, i);
		if (g_strcmp0(inhibit->id, inhibit_id) == 0) {
			g_ptr_array_remove_index(self->system_inhibits, i);
			fu_dbus_daemon_ensure_system_inhibit(self);
			found = TRUE;
			break;
		}
	}
	if (!found) {
		g_dbus_method_invocation_return_error_literal(invocation,
							      FWUPD_ERROR,
							      FWUPD_ERROR_NOT_FOUND,
							      "Cannot find inhibit ID");
		return;
	}
	g_dbus_method_invocation_return_value(invocation, NULL);
}

static void
fu_dbus_daemon_method_get_details(FuDbusDaemon *self,
				  GVariant *parameters,
				  FuEngineRequest *request,
				  GDBusMethodInvocation *invocation)
{
#ifdef HAVE_GIO_UNIX
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	gint32 fd_handle = 0;
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GPtrArray) results = NULL;

	/* get parameters */
	g_variant_get(parameters, "(h)", &fd_handle);

	/* get stream */
	stream = fu_dbus_daemon_invocation_get_input_stream(invocation, &error);
	if (stream == NULL) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}

	/* get details about the file */
	results = fu_engine_get_details(engine, request, stream, &error);
	if (results == NULL) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}
	g_dbus_method_invocation_return_value(
	    invocation,
	    fwupd_codec_array_to_variant(results, FWUPD_CODEC_FLAG_TRUSTED));
#else
	g_dbus_method_invocation_return_error_literal(invocation,
						      FWUPD_ERROR,
						      FWUPD_ERROR_INTERNAL,
						      "unsupported feature");
#endif /* HAVE_GIO_UNIX */
}

static void
fu_dbus_daemon_method_get_bios_settings(FuDbusDaemon *self,
					GVariant *parameters,
					FuEngineRequest *request,
					GDBusMethodInvocation *invocation)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	gboolean authenticate =
	    fu_engine_request_get_feature_flags(request) & FWUPD_FEATURE_FLAG_ALLOW_AUTHENTICATION;

	if (!authenticate) {
		/* if we cannot authenticate and the peer is not
		 * inherently trusted, only return a non-sensitive
		 * subset of the settings */
		g_autoptr(FuBiosSettings) attrs =
		    fu_context_get_bios_settings(fu_engine_get_context(engine));
		g_dbus_method_invocation_return_value(
		    invocation,
		    fwupd_codec_to_variant(FWUPD_CODEC(attrs),
					   fu_engine_request_get_converter_flags(request)));
	} else {
		g_autoptr(FuMainAuthHelper) helper = NULL;

		/* authenticate */
		fu_dbus_daemon_set_status(self, FWUPD_STATUS_WAITING_FOR_AUTH);
		helper = g_new0(FuMainAuthHelper, 1);
		helper->self = self;
		helper->request = g_object_ref(request);
		helper->invocation = g_object_ref(invocation);
		fu_polkit_authority_check(
		    self->authority,
		    fu_engine_request_get_sender(request),
		    "org.freedesktop.fwupd.get-bios-settings",
		    fu_dbus_daemon_engine_request_get_authority_check_flags(request),
		    NULL,
		    fu_dbus_daemon_authorize_get_bios_settings_cb,
		    g_steal_pointer(&helper));
	}
}

static void
fu_dbus_daemon_method_set_bios_settings(FuDbusDaemon *self,
					GVariant *parameters,
					FuEngineRequest *request,
					GDBusMethodInvocation *invocation)
{
	const gchar *key;
	const gchar *value;
	g_autoptr(FuMainAuthHelper) helper = NULL;
	g_autoptr(GVariantIter) iter = NULL;

	g_variant_get(parameters, "(a{ss})", &iter);

	/* authenticate */
	fu_dbus_daemon_set_status(self, FWUPD_STATUS_WAITING_FOR_AUTH);
	helper = g_new0(FuMainAuthHelper, 1);
	helper->self = self;
	helper->request = g_object_ref(request);
	helper->invocation = g_object_ref(invocation);
	helper->bios_settings = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	while (g_variant_iter_next(iter, "{&s&s}", &key, &value)) {
		g_debug("got setting %s=%s", key, value);
		g_hash_table_insert(helper->bios_settings, g_strdup(key), g_strdup(value));
	}
	fu_polkit_authority_check(self->authority,
				  fu_engine_request_get_sender(request),
				  "org.freedesktop.fwupd.set-bios-settings",
				  fu_dbus_daemon_engine_request_get_authority_check_flags(request),
				  NULL,
				  fu_dbus_daemon_authorize_set_bios_settings_cb,
				  g_steal_pointer(&helper));
}

static void
fu_dbus_daemon_method_fix_host_security_attr(FuDbusDaemon *self,
					     GVariant *parameters,
					     FuEngineRequest *request,
					     GDBusMethodInvocation *invocation)
{
	const gchar *appstream_id = NULL;
	g_autoptr(FuMainAuthHelper) helper = NULL;
	g_variant_get(parameters, "(&s)", &appstream_id);

	/* authenticate */
	fu_dbus_daemon_set_status(self, FWUPD_STATUS_WAITING_FOR_AUTH);
	helper = g_new0(FuMainAuthHelper, 1);
	helper->self = self;
	helper->request = g_object_ref(request);
	helper->invocation = g_object_ref(invocation);
	helper->key = g_strdup(appstream_id);
	helper->is_fix = TRUE;
	fu_polkit_authority_check(self->authority,
				  fu_engine_request_get_sender(request),
				  "org.freedesktop.fwupd.fix-host-security-attr",
				  fu_dbus_daemon_engine_request_get_authority_check_flags(request),
				  NULL,
				  fu_dbus_daemon_authorize_fix_host_security_attr_cb,
				  g_steal_pointer(&helper));
}

static void
fu_dbus_daemon_method_undo_host_security_attr(FuDbusDaemon *self,
					      GVariant *parameters,
					      FuEngineRequest *request,
					      GDBusMethodInvocation *invocation)
{
	const gchar *appstream_id = NULL;
	g_autoptr(FuMainAuthHelper) helper = NULL;

	g_variant_get(parameters, "(&s)", &appstream_id);

	/* authenticate */
	fu_dbus_daemon_set_status(self, FWUPD_STATUS_WAITING_FOR_AUTH);
	helper = g_new0(FuMainAuthHelper, 1);
	helper->self = self;
	helper->request = g_object_ref(request);
	helper->invocation = g_object_ref(invocation);
	helper->key = g_strdup(appstream_id);
	helper->is_fix = FALSE;
	fu_polkit_authority_check(self->authority,
				  fu_engine_request_get_sender(request),
				  "org.freedesktop.fwupd.undo-host-security-attr",
				  fu_dbus_daemon_engine_request_get_authority_check_flags(request),
				  NULL,
				  fu_dbus_daemon_authorize_undo_host_security_attr_cb,
				  g_steal_pointer(&helper));
}

typedef void (*FuDbusDaemonMethodFunc)(FuDbusDaemon *self,
				       GVariant *parameters,
				       FuEngineRequest *request,
				       GDBusMethodInvocation *invocation);

static void
fu_dbus_daemon_method_call(GDBusConnection *connection,
			   const gchar *sender,
			   const gchar *object_path,
			   const gchar *interface_name,
			   const gchar *method_name,
			   GVariant *parameters,
			   GDBusMethodInvocation *invocation,
			   gpointer user_data)
{
	FuDbusDaemon *self = FU_DBUS_DAEMON(user_data);
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	g_autoptr(FuEngineRequest) request = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GString) parameters_str = NULL;
	struct {
		const gchar *name;
		FuDbusDaemonMethodFunc func;
	} method_funcs[] = {
	    {"GetDevices", fu_dbus_daemon_method_get_devices},
	    {"GetPlugins", fu_dbus_daemon_method_get_plugins},
	    {"GetReleases", fu_dbus_daemon_method_get_releases},
	    {"GetApprovedFirmware", fu_dbus_daemon_method_get_approved_firmware},
	    {"GetBlockedFirmware", fu_dbus_daemon_method_get_blocked_firmware},
	    {"GetReportMetadata", fu_dbus_daemon_method_get_report_metadata},
	    {"SetApprovedFirmware", fu_dbus_daemon_method_set_approved_firmware},
	    {"SetBlockedFirmware", fu_dbus_daemon_method_set_blocked_firmware},
	    {"Quit", fu_dbus_daemon_method_quit},
	    {"SelfSign", fu_dbus_daemon_method_self_sign},
	    {"GetDowngrades", fu_dbus_daemon_method_get_downgrades},
	    {"GetUpgrades", fu_dbus_daemon_method_get_upgrades},
	    {"GetRemotes", fu_dbus_daemon_method_get_remotes},
	    {"GetHistory", fu_dbus_daemon_method_get_history},
	    {"GetHostSecurityAttrs", fu_dbus_daemon_method_get_host_security_attrs},
	    {"GetHostSecurityEvents", fu_dbus_daemon_method_get_host_security_events},
	    {"ClearResults", fu_dbus_daemon_method_clear_results},
	    {"EmulationLoad", fu_dbus_daemon_method_emulation_load},
	    {"EmulationSave", fu_dbus_daemon_method_emulation_save},
	    {"ModifyDevice", fu_dbus_daemon_method_modify_device},
	    {"GetResults", fu_dbus_daemon_method_get_results},
	    {"UpdateMetadata", fu_dbus_daemon_method_update_metadata},
	    {"Unlock", fu_dbus_daemon_method_unlock},
	    {"Activate", fu_dbus_daemon_method_activate},
	    {"ModifyConfig", fu_dbus_daemon_method_modify_config},
	    {"ResetConfig", fu_dbus_daemon_method_reset_config},
	    {"ModifyRemote", fu_dbus_daemon_method_modify_remote},
	    {"VerifyUpdate", fu_dbus_daemon_method_verify_update},
	    {"Verify", fu_dbus_daemon_method_verify},
	    {"SetFeatureFlags", fu_dbus_daemon_method_set_feature_flags},
	    {"SetHints", fu_dbus_daemon_method_set_hints},
	    {"Inhibit", fu_dbus_daemon_method_inhibit},
	    {"Uninhibit", fu_dbus_daemon_method_uninhibit},
	    {"Install", fu_dbus_daemon_method_install},
	    {"GetDetails", fu_dbus_daemon_method_get_details},
	    {"GetBiosSettings", fu_dbus_daemon_method_get_bios_settings},
	    {"SetBiosSettings", fu_dbus_daemon_method_set_bios_settings},
	    {"FixHostSecurityAttr", fu_dbus_daemon_method_fix_host_security_attr},
	    {"UndoHostSecurityAttr", fu_dbus_daemon_method_undo_host_security_attr},
	};

	/* build request */
	request = fu_dbus_daemon_create_request(self, sender, &error);
	if (request == NULL) {
		fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		return;
	}

	/* activity */
	fu_engine_idle_reset(engine);

	/* be helpful */
	parameters_str = g_variant_print_string(parameters, NULL, TRUE);
	g_debug("Called %s%s", method_name, parameters_str->str);

	/* call the correct vfunc */
	for (guint i = 0; i < G_N_ELEMENTS(method_funcs); i++) {
		if (g_strcmp0(method_name, method_funcs[i].name) == 0) {
			method_funcs[i].func(self, parameters, request, invocation);
			return;
		}
	}
	g_dbus_method_invocation_return_error(invocation,
					      G_DBUS_ERROR,
					      G_DBUS_ERROR_UNKNOWN_METHOD,
					      "no such method %s",
					      method_name);
}

static GVariant *
fu_dbus_daemon_get_property(GDBusConnection *connection_,
			    const gchar *sender,
			    const gchar *object_path,
			    const gchar *interface_name,
			    const gchar *property_name,
			    GError **error,
			    gpointer user_data)
{
	FuDbusDaemon *self = FU_DBUS_DAEMON(user_data);
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));

	/* activity */
	fu_engine_idle_reset(engine);

	if (g_strcmp0(property_name, "DaemonVersion") == 0)
		return g_variant_new_string(PACKAGE_VERSION);

	if (g_strcmp0(property_name, "HostBkc") == 0)
		return g_variant_new_string(fu_engine_get_host_bkc(engine));

	if (g_strcmp0(property_name, "Tainted") == 0)
		return g_variant_new_boolean(FALSE);

	if (g_strcmp0(property_name, "Status") == 0)
		return g_variant_new_uint32(self->status);

	if (g_strcmp0(property_name, "Percentage") == 0)
		return g_variant_new_uint32(self->percentage);

	if (g_strcmp0(property_name, FWUPD_RESULT_KEY_BATTERY_LEVEL) == 0) {
		FuContext *ctx = fu_engine_get_context(engine);
		return g_variant_new_uint32(fu_context_get_battery_level(ctx));
	}

	if (g_strcmp0(property_name, FWUPD_RESULT_KEY_BATTERY_THRESHOLD) == 0) {
		FuContext *ctx = fu_engine_get_context(engine);
		return g_variant_new_uint32(fu_context_get_battery_threshold(ctx));
	}

	if (g_strcmp0(property_name, "HostVendor") == 0)
		return g_variant_new_string(fu_engine_get_host_vendor(engine));

	if (g_strcmp0(property_name, "HostProduct") == 0)
		return g_variant_new_string(fu_engine_get_host_product(engine));

	if (g_strcmp0(property_name, "HostMachineId") == 0) {
		const gchar *tmp = fu_engine_get_host_machine_id(engine);
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
		const gchar *tmp = fu_engine_get_host_security_id(engine);
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

	if (g_strcmp0(property_name, "OnlyTrusted") == 0) {
		return g_variant_new_boolean(
		    fu_engine_config_get_only_trusted(fu_engine_get_config(engine)));
	}

	/* return an error */
	g_set_error(error,
		    G_DBUS_ERROR,
		    G_DBUS_ERROR_UNKNOWN_PROPERTY,
		    "failed to get daemon property %s",
		    property_name);
	return NULL;
}

static gboolean
fu_dbus_daemon_register_object(FuDbusDaemon *self, GError **error)
{
	guint registration_id;
	static const GDBusInterfaceVTable interface_vtable = {fu_dbus_daemon_method_call,
							      fu_dbus_daemon_get_property,
							      NULL};

	registration_id =
	    g_dbus_connection_register_object(self->connection,
					      FWUPD_DBUS_PATH,
					      self->introspection_daemon->interfaces[0],
					      &interface_vtable,
					      self,  /* user_data */
					      NULL,  /* user_data_free_func */
					      NULL); /* GError** */
	if (registration_id == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "unspecified failure");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_dbus_daemon_client_list_ensure_inhibit(FuDbusDaemon *self)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	g_autoptr(GPtrArray) clients = fu_client_list_get_all(self->client_list);
	g_debug("connected clients: %u", clients->len);
	if (clients->len > 0 && self->clients_inhibit_id == 0) {
		self->clients_inhibit_id =
		    fu_engine_idle_inhibit(engine, FU_IDLE_INHIBIT_TIMEOUT, "connected-clients");
	} else if (clients->len == 0 && self->clients_inhibit_id != 0) {
		fu_engine_idle_uninhibit(engine, self->clients_inhibit_id);
		self->clients_inhibit_id = 0;
	}
}

static void
fu_dbus_daemon_client_list_added_cb(FuClientList *client_list, FuClient *client, gpointer user_data)
{
	FuDbusDaemon *self = FU_DBUS_DAEMON(user_data);
	fu_dbus_daemon_client_list_ensure_inhibit(self);
}

static void
fu_dbus_daemon_client_list_removed_cb(FuClientList *client_list,
				      FuClient *client,
				      gpointer user_data)
{
	FuDbusDaemon *self = FU_DBUS_DAEMON(user_data);
	fu_dbus_daemon_client_list_ensure_inhibit(self);
}

static void
fu_dbus_daemon_set_connection(FuDbusDaemon *self, GDBusConnection *connection)
{
	g_set_object(&self->connection, connection);
	if (connection != NULL) {
		g_autoptr(FuClientList) client_list = fu_client_list_new(connection);
		g_signal_connect(client_list,
				 "added",
				 G_CALLBACK(fu_dbus_daemon_client_list_added_cb),
				 self);
		g_signal_connect(client_list,
				 "removed",
				 G_CALLBACK(fu_dbus_daemon_client_list_removed_cb),
				 self);
		g_set_object(&self->client_list, client_list);
	}
}

static void
fu_dbus_daemon_dbus_bus_acquired_cb(GDBusConnection *connection,
				    const gchar *name,
				    gpointer user_data)
{
	FuDbusDaemon *self = FU_DBUS_DAEMON(user_data);
	g_autoptr(GError) error = NULL;

	fu_dbus_daemon_set_connection(self, connection);
	if (!fu_dbus_daemon_register_object(self, &error)) {
		g_warning("cannot register object: %s", error->message);
		return;
	}

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
fu_dbus_daemon_dbus_name_acquired_cb(GDBusConnection *connection,
				     const gchar *name,
				     gpointer user_data)
{
	g_debug("acquired name: %s", name);
}

static void
fu_dbus_daemon_dbus_name_lost_cb(GDBusConnection *connection, const gchar *name, gpointer user_data)
{
	FuDbusDaemon *self = FU_DBUS_DAEMON(user_data);
	g_warning("another service has claimed the dbus name %s", name);
	fu_daemon_stop(FU_DAEMON(self), NULL);
}

static void
fu_dbus_daemon_dbus_connection_closed_cb(GDBusConnection *connection,
					 gboolean remote_peer_vanished,
					 GError *error,
					 gpointer user_data)
{
	if (remote_peer_vanished)
		g_info("client connection closed: %s", error != NULL ? error->message : "unknown");
}

static gboolean
fu_dbus_daemon_dbus_new_connection_cb(GDBusServer *server,
				      GDBusConnection *connection,
				      gpointer user_data)
{
	FuDbusDaemon *self = FU_DBUS_DAEMON(user_data);
	fu_dbus_daemon_set_connection(self, connection);
	g_signal_connect(connection,
			 "closed",
			 G_CALLBACK(fu_dbus_daemon_dbus_connection_closed_cb),
			 self);
	return fu_dbus_daemon_register_object(self, NULL);
}

static GDBusNodeInfo *
fu_dbus_daemon_load_introspection(const gchar *filename, GError **error)
{
	g_autoptr(GBytes) data = NULL;
	g_autofree gchar *path = NULL;

	/* lookup data */
	path = g_build_filename("/org/freedesktop/fwupd", filename, NULL);
	data = g_resources_lookup_data(path, G_RESOURCE_LOOKUP_FLAGS_NONE, error);
	if (data == NULL)
		return NULL;

	/* build introspection from XML */
	return g_dbus_node_info_new_for_xml(g_bytes_get_data(data, NULL), error);
}

static gboolean
fu_dbus_daemon_setup(FuDaemon *daemon,
		     const gchar *socket_address,
		     FuProgress *progress,
		     GError **error)
{
	FuDbusDaemon *self = FU_DBUS_DAEMON(daemon);
	FuEngine *engine = fu_daemon_get_engine(daemon);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_profile(progress, g_getenv("FWUPD_VERBOSE") != NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 99, "load-engine");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 1, "load-introspection");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 1, "load-authority");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 1, "own-name");

	/* load engine */
	g_signal_connect(FU_ENGINE(engine),
			 "changed",
			 G_CALLBACK(fu_dbus_daemon_engine_changed_cb),
			 self);
	g_signal_connect(FU_ENGINE(engine),
			 "device-added",
			 G_CALLBACK(fu_dbus_daemon_engine_device_added_cb),
			 self);
	g_signal_connect(FU_ENGINE(engine),
			 "device-removed",
			 G_CALLBACK(fu_dbus_daemon_engine_device_removed_cb),
			 self);
	g_signal_connect(FU_ENGINE(engine),
			 "device-changed",
			 G_CALLBACK(fu_dbus_daemon_engine_device_changed_cb),
			 self);
	g_signal_connect(FU_ENGINE(engine),
			 "device-request",
			 G_CALLBACK(fu_dbus_daemon_engine_device_request_cb),
			 self);
	g_signal_connect(FU_ENGINE(engine),
			 "status-changed",
			 G_CALLBACK(fu_dbus_daemon_engine_status_changed_cb),
			 self);
	if (!fu_engine_load(engine,
			    FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_HWINFO |
				FU_ENGINE_LOAD_FLAG_REMOTES | FU_ENGINE_LOAD_FLAG_EXTERNAL_PLUGINS |
				FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS |
				FU_ENGINE_LOAD_FLAG_ENSURE_CLIENT_CERT |
				FU_ENGINE_LOAD_FLAG_DEVICE_HOTPLUG,
			    fu_progress_get_child(progress),
			    error)) {
		g_prefix_error(error, "failed to load engine: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* load introspection from file */
	self->introspection_daemon =
	    fu_dbus_daemon_load_introspection(FWUPD_DBUS_INTERFACE ".xml", error);
	if (self->introspection_daemon == NULL) {
		g_prefix_error(error, "failed to load introspection: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* get authority */
	self->authority = fu_polkit_authority_new();
	if (!fu_polkit_authority_load(self->authority, error))
		return FALSE;
	fu_progress_step_done(progress);

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
				 G_CALLBACK(fu_dbus_daemon_dbus_new_connection_cb),
				 self);
	} else {
		self->owner_id = g_bus_own_name(G_BUS_TYPE_SYSTEM,
						FWUPD_DBUS_SERVICE,
						G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
						    G_BUS_NAME_OWNER_FLAGS_REPLACE,
						fu_dbus_daemon_dbus_bus_acquired_cb,
						fu_dbus_daemon_dbus_name_acquired_cb,
						fu_dbus_daemon_dbus_name_lost_cb,
						self,
						NULL);
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static void
fu_dbus_daemon_init(FuDbusDaemon *self)
{
	self->status = FWUPD_STATUS_IDLE;
	self->system_inhibits =
	    g_ptr_array_new_with_free_func((GDestroyNotify)fu_dbus_daemon_system_inhibit_free);
}

static void
fu_dbus_daemon_finalize(GObject *obj)
{
	FuDbusDaemon *self = FU_DBUS_DAEMON(obj);

	g_ptr_array_unref(self->system_inhibits);
	if (self->client_list != NULL)
		g_object_unref(self->client_list);
	if (self->owner_id > 0)
		g_bus_unown_name(self->owner_id);
	if (self->proxy_uid != NULL)
		g_object_unref(self->proxy_uid);
	if (self->connection != NULL)
		g_object_unref(self->connection);
	if (self->authority != NULL)
		g_object_unref(self->authority);
	if (self->introspection_daemon != NULL)
		g_dbus_node_info_unref(self->introspection_daemon);

	G_OBJECT_CLASS(fu_dbus_daemon_parent_class)->finalize(obj);
}

static void
fu_dbus_daemon_class_init(FuDbusDaemonClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDaemonClass *daemon_class = FU_DAEMON_CLASS(klass);
	object_class->finalize = fu_dbus_daemon_finalize;
	daemon_class->setup = fu_dbus_daemon_setup;
}

FuDaemon *
fu_daemon_new(void)
{
	return FU_DAEMON(g_object_new(FU_TYPE_DBUS_DAEMON, NULL));
}
