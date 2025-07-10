/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 * Copyright 2024 Colin Kinloch <colin.kinloch@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuMain"

#include "config.h"

#include <glib.h>
// AOSP binder service management
#include <fwupdplugin.h>

#include <android/binder_ibinder.h>
#include <android/binder_manager.h>
#include <android/binder_parcel.h>
#include <android/binder_process.h>
#include <android/binder_status.h>
#include <android/persistable_bundle.h>

#include "fwupd-device-private.h"
#include "fwupd-enums-private.h"

#include "fu-device-private.h"
#include "fu-engine-helper.h"
#include "fu-engine-request.h"
#include "fu-engine-requirements.h"
#include "gparcelable.h"
#ifdef HAVE_GIO_UNIX
#include "fu-unix-seekable-input-stream.h"
#endif

#include "fu-binder-aidl.h"
#include "fu-binder-daemon.h"

/* this can be tested using:
 * ./build/release/binder-client -v -d /dev/hwbinder -n devices@1.0/org.freedesktop.fwupd
 */

G_DEFINE_AUTOPTR_CLEANUP_FUNC(AStatus, AStatus_delete)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(AParcel, AParcel_delete)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(APersistableBundle, APersistableBundle_delete)

struct _FuBinderDaemon {
	FuDaemon parent_instance;
	gboolean async;
	gulong presence_id;
	AIBinder_Class *binder_class;
	AIBinder_Class *listener_binder_class;
	AIBinder *binder;
	gint binder_fd;
	FwupdStatus status; /* last emitted */
	guint percentage;   /* last emitted */
	GPtrArray *event_listener_binders;
};

G_DEFINE_TYPE(FuBinderDaemon, fu_binder_daemon, FU_TYPE_DAEMON)

static void
fu_binder_daemon_set_status(FuBinderDaemon *self, FwupdStatus status);

typedef struct {
	// GDBusMethodInvocation *invocation;
	FuEngineRequest *request;
	FuProgress *progress;
	// FuClient *client;
	glong client_sender_changed_id;
	GPtrArray *releases;
	GPtrArray *action_ids;
	GPtrArray *checksums;
	GPtrArray *errors;
	guint64 flags;
	GInputStream *stream;
	FuBinderDaemon *self;
	gchar *device_id;
	gchar *remote_id;
	gchar *section;
	gchar *key;
	gchar *value;
	gint32 handle;
	FuCabinet *cabinet;
	GHashTable *bios_settings; /* str:str */
	gboolean is_fix;
} FuMainAuthHelper;

static void
fu_dbus_daemon_auth_helper_free(FuMainAuthHelper *helper)
{
	/* always return to IDLE even in event of an auth error */
	fu_binder_daemon_set_status(helper->self, FWUPD_STATUS_IDLE);

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
	// if (helper->client_sender_changed_id > 0)
	//	g_signal_handler_disconnect(helper->client, helper->client_sender_changed_id);
	// if (helper->client != NULL)
	//	g_object_unref(helper->client);
	g_free(helper->device_id);
	g_free(helper->remote_id);
	g_free(helper->section);
	g_free(helper->key);
	g_free(helper->value);
	// g_object_unref(helper->invocation);
	if (helper->bios_settings != NULL)
		g_hash_table_unref(helper->bios_settings);
	g_free(helper);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuMainAuthHelper, fu_dbus_daemon_auth_helper_free)

static binder_status_t
fu_binder_daemon_method_invocation_return_error(AParcel *out, GError *error)
{
	g_autoptr(AStatus) status = NULL;

	fu_error_convert(&error);

	g_return_val_if_fail(error != NULL, STATUS_BAD_VALUE);

	status = AStatus_fromServiceSpecificErrorWithMessage(error->code, error->message);

	return AParcel_writeStatusHeader(out, status);
}

static binder_status_t
fu_binder_daemon_method_invocation_return_error_literal(AParcel *out,
							gint code,
							const gchar *message)
{
	g_autoptr(AStatus) status = NULL;

	g_return_val_if_fail(out != NULL, STATUS_UNEXPECTED_NULL);
	g_return_val_if_fail(message != NULL, STATUS_UNEXPECTED_NULL);

	status = AStatus_fromServiceSpecificErrorWithMessage(code, message);

	return AParcel_writeStatusHeader(out, status);
}

static binder_status_t
fu_binder_daemon_method_invocation_return_variant(AParcel *out, GVariant *value, GError **error)
{
	gint out_start = AParcel_getDataPosition(out);

	AParcel_writeStatusHeader(out, AStatus_newOk());

	// TODO: Improve error checking
	if (value) {
		if (gp_parcel_write_variant(out, value, error) != STATUS_OK) {
			AParcel_setDataPosition(out, out_start);
			if (*error) {
				return fu_binder_daemon_method_invocation_return_error(out, *error);
			} else {
				return fu_binder_daemon_method_invocation_return_error_literal(
				    out,
				    FWUPD_ERROR_INTERNAL,
				    "failed to encode parcel, no error");
			}
		}
	}

	return STATUS_OK;
}

static GVariant *
fu_binder_daemon_device_array_to_variant(FuBinderDaemon *self,
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

static binder_status_t
fu_binder_daemon_method_get_devices(FuBinderDaemon *self,
				    FuEngineRequest *request,
				    const AParcel *in,
				    AParcel *out,
				    GError **error)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	g_autoptr(GVariant) val = NULL;
	g_autoptr(GPtrArray) devices = NULL;

	devices = fu_engine_get_devices(engine, error);
	if (devices == NULL) {
		fu_binder_daemon_method_invocation_return_error(out, *error);
		return STATUS_OK;
	}
	val = fu_binder_daemon_device_array_to_variant(self, request, devices, error);
	if (val == NULL) {
		fu_binder_daemon_method_invocation_return_error(out, *error);
		return STATUS_OK;
	}

	return fu_binder_daemon_method_invocation_return_variant(out, val, error);
}

static binder_status_t
fu_binder_daemon_method_get_upgrades(FuBinderDaemon *self,
				     FuEngineRequest *request,
				     const AParcel *in,
				     AParcel *out,
				     GError **error)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	const GVariantType *param_type = G_VARIANT_TYPE("(s)");
	g_autoptr(GVariant) parameters = NULL;
	g_autoptr(GPtrArray) releases = NULL;
	g_auto(GVariantBuilder) builder;
	const gchar *device_id;

	g_variant_builder_init(&builder, param_type);

	if (!gp_parcel_to_variant(&builder, in, param_type, error)) {
		fu_binder_daemon_method_invocation_return_error(out, *error);
		return STATUS_OK;
	}

	parameters = g_variant_builder_end(&builder);

	g_variant_get(parameters, "(&s)", &device_id);
	if (!fu_daemon_device_id_valid(device_id, error)) {
		fu_binder_daemon_method_invocation_return_error(out, *error);
		return STATUS_OK;
	}
	releases = fu_engine_get_upgrades(engine, request, device_id, error);
	if (releases == NULL) {
		fu_binder_daemon_method_invocation_return_error(out, *error);
		return STATUS_OK;
	}

	return fu_binder_daemon_method_invocation_return_variant(
	    out,
	    fwupd_codec_array_to_variant(releases, FWUPD_CODEC_FLAG_NONE),
	    error);
}

static binder_status_t
fu_binder_daemon_method_get_remotes(FuBinderDaemon *self,
				    FuEngineRequest *request,
				    const AParcel *in,
				    AParcel *out,
				    GError **error)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	g_autoptr(GVariant) tuple_remotes_array = NULL;
	g_autoptr(GPtrArray) remotes = fu_engine_get_remotes(engine, error);

	if (remotes == NULL) {
		fu_binder_daemon_method_invocation_return_error(out, *error);
		return STATUS_OK;
	}

	tuple_remotes_array = fwupd_codec_array_to_variant(remotes, FWUPD_CODEC_FLAG_NONE);

	return fu_binder_daemon_method_invocation_return_variant(out, tuple_remotes_array, error);
}

static binder_status_t
fu_binder_daemon_method_get_properties(FuBinderDaemon *self,
				       FuEngineRequest *request,
				       const AParcel *in,
				       AParcel *out,
				       GError **error)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	const GVariantType *param_type = G_VARIANT_TYPE("(as)");
	g_autoptr(GVariant) parameters = NULL;
	g_autoptr(GVariant) properties = NULL;
	g_auto(GStrv) property_names = NULL;
	g_auto(GVariantBuilder) builder;
	g_auto(GVariantDict) vardict = G_VARIANT_DICT_INIT(NULL);

	/* activity */
	fu_engine_idle_reset(engine);

	g_variant_builder_init(&builder, param_type);

	if (!gp_parcel_to_variant(&builder, in, param_type, error)) {
		fu_binder_daemon_method_invocation_return_error(out, *error);
		return STATUS_OK;
	}

	parameters = g_variant_builder_end(&builder);

	if (parameters == NULL) {
		fu_binder_daemon_method_invocation_return_error_literal(
		    out,
		    FWUPD_ERROR_INTERNAL,
		    "could not decode parameters");
		return STATUS_OK;
	}

	g_debug("getProperties parameters: %s", g_variant_print(parameters, TRUE));

	g_variant_get(parameters, "(^as)", &property_names);

	for (guint i = 0; property_names[i] != NULL; i++) {
		const gchar *property_name = property_names[i];
		g_autoptr(GVariant) value = NULL;

		if (g_strcmp0(property_name, "DaemonVersion") == 0)
			value = g_variant_new_string(PACKAGE_VERSION);

		if (g_strcmp0(property_name, "HostBkc") == 0)
			value = g_variant_new_string(fu_engine_get_host_bkc(engine));

		if (g_strcmp0(property_name, "Tainted") == 0)
			value = g_variant_new_boolean(FALSE);

		if (g_strcmp0(property_name, "Status") == 0)
			value = g_variant_new_uint32(self->status);

		if (g_strcmp0(property_name, "Percentage") == 0)
			value = g_variant_new_uint32(self->percentage);

		if (g_strcmp0(property_name, FWUPD_RESULT_KEY_BATTERY_LEVEL) == 0) {
			FuContext *ctx = fu_engine_get_context(engine);
			value = g_variant_new_uint32(fu_context_get_battery_level(ctx));
		}

		if (g_strcmp0(property_name, FWUPD_RESULT_KEY_BATTERY_THRESHOLD) == 0) {
			FuContext *ctx = fu_engine_get_context(engine);
			value = g_variant_new_uint32(fu_context_get_battery_threshold(ctx));
		}

		if (g_strcmp0(property_name, "HostVendor") == 0)
			value = g_variant_new_string(fu_engine_get_host_vendor(engine));

		if (g_strcmp0(property_name, "HostProduct") == 0)
			value = g_variant_new_string(fu_engine_get_host_product(engine));

		if (g_strcmp0(property_name, "HostMachineId") == 0) {
			const gchar *tmp = fu_engine_get_host_machine_id(engine);
			if (tmp == NULL) {
				g_set_error(error,
					    G_DBUS_ERROR,
					    G_DBUS_ERROR_NOT_SUPPORTED,
					    "failed to get daemon property %s",
					    property_name);
				fu_binder_daemon_method_invocation_return_error(out, *error);
				return STATUS_OK;
			}
			value = g_variant_new_string(tmp);
		}

		if (g_strcmp0(property_name, "HostSecurityId") == 0) {
#ifdef HAVE_HSI
			g_autofree gchar *tmp = fu_engine_get_host_security_id(engine, NULL);
			value = g_variant_new_string(tmp);
#else
			g_set_error(error,
				    G_DBUS_ERROR,
				    G_DBUS_ERROR_NOT_SUPPORTED,
				    "failed to get daemon property %s",
				    property_name);
			fu_binder_daemon_method_invocation_return_error(out, *error);
			return STATUS_OK;
#endif
		}

		if (g_strcmp0(property_name, "Interactive") == 0)
			value = g_variant_new_boolean(isatty(fileno(stdout)) != 0);

		if (g_strcmp0(property_name, "OnlyTrusted") == 0) {
			value = g_variant_new_boolean(
			    fu_engine_config_get_only_trusted(fu_engine_get_config(engine)));
		}

		if (value) {
			g_debug("property %s: %s", property_name, g_variant_print(value, TRUE));
			g_variant_dict_insert_value(&vardict,
						    property_name,
						    g_steal_pointer(&value));
		} else {
			/* return an error */
			g_set_error(error,
				    G_DBUS_ERROR,
				    G_DBUS_ERROR_UNKNOWN_PROPERTY,
				    "failed to get daemon property %s",
				    property_name);
			fu_binder_daemon_method_invocation_return_error(out, *error);
			return STATUS_OK;
		}
	}

	properties = g_variant_dict_end(&vardict);

	g_debug("properties %s", g_variant_print(properties, TRUE));

	return fu_binder_daemon_method_invocation_return_variant(out, properties, error);
}

static void
fu_binder_daemon_emit_property_changed(FuBinderDaemon *self,
				       const gchar *property_name,
				       GVariant *property_value)
{
	g_autoptr(GVariant) val = NULL;
	g_autoptr(GError) error = NULL;
	binder_status_t nstatus = STATUS_OK;
	g_auto(GVariantBuilder) builder;

	/* build the dict */
	g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add(&builder, "{sv}", property_name, property_value);

	if (self->event_listener_binders->len == 0)
		return;

	val = g_variant_new("(a{sv})", &builder);

	for (guint i = 0; i < self->event_listener_binders->len; i++) {
		AParcel *in = NULL;
		g_autoptr(AParcel) out = NULL;
		AIBinder *event_listener_binder =
		    g_ptr_array_index(self->event_listener_binders, i);

		nstatus = AIBinder_prepareTransaction(event_listener_binder, &in);
		if (val)
			gp_parcel_write_variant(in, val, &error);
		nstatus = AIBinder_transact(event_listener_binder,
					    FWUPD_BINDER_LISTENER_CALL_ON_PROPERTIES_CHANGED,
					    &in,
					    &out,
					    FLAG_ONEWAY);
		if (nstatus != STATUS_OK) {
			AStatus *status = AStatus_fromStatus(nstatus);
			g_warning("Failed to transact property change %s",
				  AStatus_getDescription(status));
		}
	}
}

static void
fu_binder_daemon_set_status(FuBinderDaemon *self, FwupdStatus status)
{
	/* sanity check */
	if (self->status == status)
		return;
	self->status = status;

	g_debug("Emitting PropertyChanged('Status'='%s')", fwupd_status_to_string(status));
	fu_binder_daemon_emit_property_changed(self, "Status", g_variant_new_uint32(status));
}

static void
fu_binder_daemon_progress_percentage_changed_cb(FuProgress *progress,
						guint percentage,
						FuBinderDaemon *self)
{
	/* sanity check */
	if (self->percentage == percentage)
		return;
	self->percentage = percentage;

	g_debug("Emitting PropertyChanged('Percentage'='%u%%')", percentage);
	fu_binder_daemon_emit_property_changed(self,
					       "Percentage",
					       g_variant_new_uint32(percentage));
}

static void
fu_binder_daemon_progress_status_changed_cb(FuProgress *progress,
					    FwupdStatus status,
					    FuBinderDaemon *self)
{
	fu_binder_daemon_set_status(self, status);
}

#ifdef HAVE_GIO_UNIX
static gboolean
fu_dbus_daemon_authorize_install_queue(FuMainAuthHelper *helper_ref, GError **error)
{
	FuBinderDaemon *self = helper_ref->self;
	g_autoptr(FuMainAuthHelper) helper = helper_ref;
	gboolean ret;
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(helper->self));

	/* all authenticated, so install all the things */
	fu_progress_set_profile(helper->progress, g_getenv("FWUPD_VERBOSE") != NULL);
	g_signal_connect(FU_PROGRESS(helper->progress),
			 "percentage-changed",
			 G_CALLBACK(fu_binder_daemon_progress_percentage_changed_cb),
			 helper->self);
	g_signal_connect(FU_PROGRESS(helper->progress),
			 "status-changed",
			 G_CALLBACK(fu_binder_daemon_progress_status_changed_cb),
			 helper->self);

	/* all authenticated, so install all the things */
	fu_daemon_set_update_in_progress(FU_DAEMON(self), TRUE);
	ret = fu_engine_install_releases(engine,
					 helper->request,
					 helper->releases,
					 helper->cabinet,
					 helper->progress,
					 helper->flags,
					 error);
	fu_daemon_set_update_in_progress(FU_DAEMON(self), FALSE);
	if (fu_daemon_get_pending_stop(FU_DAEMON(self))) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "daemon was stopped");
		return FALSE;
	}

	return ret;
}
#endif /* HAVE_GIO_UNIX */

// TODO: dedupe, This is taken from FuDbusDaemon
//  It maybe requires a generic alternative to FuMainAuthHelper
#ifdef HAVE_GIO_UNIX
static gint
fu_binder_daemon_release_sort_cb(gconstpointer a, gconstpointer b)
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
	FuBinderDaemon *self = helper->self;
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

	/* post-ensure checks */
	if (!fu_release_check_version(release, component, helper->flags, &error_local)) {
		g_ptr_array_add(helper->errors, g_steal_pointer(&error_local));
		return TRUE;
	}

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
fu_dbus_daemon_install_with_helper(FuMainAuthHelper *helper, GError **error)
{
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
	g_ptr_array_sort(helper->releases, fu_binder_daemon_release_sort_cb);

	/* nothing suitable */
	if (helper->releases->len == 0) {
		GError *error_tmp = fu_engine_error_array_get_best(helper->errors);
		g_propagate_error(error, error_tmp);
		return FALSE;
	}

	/* authenticate all things in the action_ids */
	// fu_binder_daemon_set_status(self, FWUPD_STATUS_WAITING_FOR_AUTH);
	// fu_dbus_daemon_authorize_install_queue(g_steal_pointer(&helper));
	return TRUE;
}
#endif /* HAVE_GIO_UNIX */

static binder_status_t
fu_binder_daemon_method_update_metadata(FuBinderDaemon *self,
					FuEngineRequest *request,
					const AParcel *in,
					AParcel *out,
					GError **error)
{
#ifdef HAVE_GIO_UNIX
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	const GVariantType *param_type = G_VARIANT_TYPE("(shh)");
	g_autoptr(GVariant) parameters = NULL;
	const char *remote_id = NULL;
	gint32 fd_data = 0;
	gint32 fd_sig = 0;

	g_auto(GVariantBuilder) builder;
	g_variant_builder_init(&builder, param_type);

	if (!gp_parcel_to_variant(&builder, in, param_type, error)) {
		fu_binder_daemon_method_invocation_return_error(out, *error);
		return STATUS_OK;
	}

	parameters = g_variant_builder_end(&builder);

	if (parameters == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "could not decode parameters");
		return STATUS_BAD_VALUE;
	}

	g_debug("updateMetadata params %s", g_variant_print(parameters, TRUE));

	g_variant_get(parameters, "(&shh)", &remote_id, &fd_data, &fd_sig);

	/* store new metadata (will close the fds when done) */
	if (!fu_engine_update_metadata(engine, remote_id, fd_data, fd_sig, error)) {
		g_prefix_error(error, "Failed to update metadata for %s: ", remote_id);
		fu_binder_daemon_method_invocation_return_error(out, *error);
		return STATUS_OK;
	}

	return fu_binder_daemon_method_invocation_return_variant(out, NULL, error);
#else
	fu_binder_daemon_method_invocation_return_error_literal(out,
								FWUPD_ERROR_INTERNAL,
								"unsupported feature");
	return STATUS_OK;
#endif
}

static binder_status_t
fu_binder_daemon_method_install(FuBinderDaemon *self,
				FuEngineRequest *request,
				const AParcel *in,
				AParcel *out,
				GError **error)
{
#ifdef HAVE_GIO_UNIX
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	const GVariantType *param_type = G_VARIANT_TYPE("(sha{sv})");
	g_autoptr(GVariant) parameters = NULL;
	g_autoptr(AStatus) status = NULL;
	GVariant *prop_value;
	const char *device_id = NULL;
	const gchar *prop_key;
	gint32 fd_handle = 0;
	g_autoptr(FuMainAuthHelper) helper = NULL;
	g_autoptr(GInputStream) stream = NULL;
	// g_autoptr(FuMainAuthHelper) helper = NULL;
	g_autoptr(GVariantIter) iter = NULL;

	g_auto(GVariantBuilder) builder;
	g_variant_builder_init(&builder, param_type);

	if (!gp_parcel_to_variant(&builder, in, param_type, error)) {
		fu_binder_daemon_method_invocation_return_error(out, *error);
		return STATUS_OK;
	}

	parameters = g_variant_builder_end(&builder);

	if (parameters == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "could not decode parameters");
		return STATUS_BAD_VALUE;
	}

	g_debug("install params %s", g_variant_print(parameters, TRUE));

	g_variant_get(parameters, "(&sha{sv})", &device_id, &fd_handle, &iter);

	/* create helper object */
	helper = g_new0(FuMainAuthHelper, 1);
	helper->request = g_object_ref(request);
	helper->progress = fu_progress_new(G_STRLOC);
	// helper->invocation = g_object_ref(invocation);
	helper->device_id = g_strdup(device_id);
	helper->self = self;

	/* get flags */
	while (g_variant_iter_next(iter, "{&sv}", &prop_key, &prop_value)) {
		g_debug("got option %s", prop_key);
		if (g_strcmp0(prop_key, "install-flags") == 0)
			helper->flags = g_variant_get_int64(prop_value);

		/* these are all set by libfwupd < 2.0.x; parse for compatibility */
		if (g_strcmp0(prop_key, "allow-older") == 0 &&
		    g_variant_get_boolean(prop_value) == TRUE)
			helper->flags |= FWUPD_INSTALL_FLAG_ALLOW_OLDER;
		if (g_strcmp0(prop_key, "allow-reinstall") == 0 &&
		    g_variant_get_boolean(prop_value) == TRUE)
			helper->flags |= FWUPD_INSTALL_FLAG_ALLOW_REINSTALL;
		if (g_strcmp0(prop_key, "allow-branch-switch") == 0 &&
		    g_variant_get_boolean(prop_value) == TRUE)
			helper->flags |= FWUPD_INSTALL_FLAG_ALLOW_BRANCH_SWITCH;

		g_variant_unref(prop_value);
	}

	// TODO: Helper
	helper->stream = fu_unix_seekable_input_stream_new(fd_handle, TRUE);
	if (helper->stream == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "invalid stream");
		fu_binder_daemon_method_invocation_return_error(out, *error);
		g_warning("stream error %s", (*error)->message);
		return STATUS_OK;
	}

	/* relax these */
	if (fu_engine_config_get_ignore_requirements(fu_engine_get_config(engine)))
		helper->flags |= FWUPD_INSTALL_FLAG_IGNORE_REQUIREMENTS;

	// TODO: fu_client_list_register notify::flags (watch for death of client)
	if (!fu_dbus_daemon_install_with_helper(helper, error)) {
		if (error && *error) {
			fu_binder_daemon_method_invocation_return_error(out, *error);
			g_warning("install setup error: %s", (*error)->message);
			return STATUS_OK;
		} else {
			g_warning("install setup error without error");
		}
		return EX_SERVICE_SPECIFIC;
	}

	// TODO: Finish binder transaction before installing firmware?
	//   Currently we cause the app to stall
	//   What does this mean for client events? onPropertiesChanged

	/* authenticate all things in the action_ids */
	// fu_binder_daemon_set_status(self, FWUPD_STATUS_WAITING_FOR_AUTH);
	if (!fu_dbus_daemon_authorize_install_queue(g_steal_pointer(&helper), error)) {
		if (error && *error) {
			fu_binder_daemon_method_invocation_return_error(out, *error);
			g_warning("install error %s", (*error)->message);
			return STATUS_OK;
		} else {
			g_warning("install error without error");
		}
		return STATUS_UNKNOWN_ERROR;
	}

	return fu_binder_daemon_method_invocation_return_variant(out, NULL, error);
#else
	fu_binder_daemon_method_invocation_return_error_literal(out,
								FWUPD_ERROR_INTERNAL,
								"unsupported feature");
	return STATUS_OK;
#endif
}

static void *
listener_on_create(void *arg)
{
	return arg;
}
static void
listener_on_destroy(void *arg)
{
	// TODO: Clean up ???
}

static binder_status_t
listener_on_transact(AIBinder *binder, transaction_code_t code, const AParcel *in, AParcel *out)
{
	return STATUS_UNKNOWN_TRANSACTION;
}

typedef struct ListenerDeathCookie {
	FuBinderDaemon *daemon;
	AIBinder *listener_binder;
} ListenerDeathCookie;

G_DEFINE_AUTOPTR_CLEANUP_FUNC(ListenerDeathCookie, g_free)

static void
listener_on_binder_died(void *cookie)
{
	ListenerDeathCookie *death_cookie = cookie;
	g_warning("listener is dead %p %d",
		  cookie,
		  death_cookie->daemon->event_listener_binders->len);

	g_ptr_array_remove(death_cookie->daemon->event_listener_binders,
			   death_cookie->listener_binder);
}

static void
listener_death_recipient_on_unlinked(void *cookie)
{
	g_warning("put the cookie dawn %p", cookie);
	g_free(cookie);
}

static binder_status_t
fu_binder_daemon_method_add_event_listener(FuBinderDaemon *self,
					   FuEngineRequest *request,
					   const AParcel *in,
					   AParcel *out,
					   GError **error)
{
	AIBinder *event_listener_remote_object = NULL;
	g_autoptr(ListenerDeathCookie) death_cookie = NULL;
	AIBinder_DeathRecipient *death_recipient;
	binder_status_t nstatus = STATUS_OK;
	nstatus = AParcel_readStrongBinder(in, &event_listener_remote_object);
	if (nstatus != STATUS_OK) {
		AStatus *status = AStatus_fromStatus(nstatus);
		g_warning("failed to read strong binder %s", AStatus_getDescription(status));
	}
	g_info("strong binder  %p\n", event_listener_remote_object);

	AIBinder_associateClass(event_listener_remote_object, self->listener_binder_class);

	g_ptr_array_add(self->event_listener_binders, event_listener_remote_object);

	death_cookie = g_malloc0(sizeof(ListenerDeathCookie));
	death_cookie->listener_binder = event_listener_remote_object;
	death_cookie->daemon = self;

	death_recipient = AIBinder_DeathRecipient_new(listener_on_binder_died);
	AIBinder_DeathRecipient_setOnUnlinked(death_recipient,
					      listener_death_recipient_on_unlinked);
	AIBinder_linkToDeath(event_listener_remote_object,
			     death_recipient,
			     g_steal_pointer(&death_cookie));

	return STATUS_OK;
}

static FuEngineRequest *
fu_binder_daemon_create_request(FuBinderDaemon *self,
				uid_t calling_uid,
				uid_t calling_pid,
				GError **error)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	FwupdCodecFlags converter_flags = FWUPD_CODEC_FLAG_NONE;
	g_autofree gchar *sender = g_strdup_printf("%d:%d", calling_uid, calling_pid);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(sender);

	// TODO: fu_engine_is_uid_pid_trusted?
	if (fu_engine_is_uid_trusted(engine, calling_uid))
		converter_flags |= FWUPD_CODEC_FLAG_TRUSTED;

	// TODO: Check if sender is trusted
	converter_flags |= FWUPD_CODEC_FLAG_TRUSTED;

	fu_engine_request_set_converter_flags(request, converter_flags);

	return g_steal_pointer(&request);
}

typedef binder_status_t (*FuBinderDaemonMethodFunc)(FuBinderDaemon *self,
						    FuEngineRequest *request,
						    const AParcel *in,
						    AParcel *out,
						    GError **error);

static void
fu_binder_daemon_send_codec_event(FuBinderDaemon *self, FwupdCodec *codec, guint32 transaction_id)
{
	AIBinder *event_listener_binder;
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;
	binder_status_t nstatus = STATUS_OK;

	if (self->event_listener_binders->len == 0)
		return;

	if (codec)
		val = fwupd_codec_to_variant(codec, FWUPD_CODEC_FLAG_NONE);

	for (guint i = 0; i < self->event_listener_binders->len; i++) {
		AParcel *in = NULL;
		g_autoptr(AParcel) out = NULL;

		event_listener_binder = g_ptr_array_index(self->event_listener_binders, i);
		nstatus = AIBinder_prepareTransaction(event_listener_binder, &in);
		if (val)
			gp_parcel_write_variant(in, val, &error);
		nstatus = AIBinder_transact(event_listener_binder,
					    transaction_id,
					    &in,
					    &out,
					    FLAG_ONEWAY);
		if (nstatus != STATUS_OK) {
			AStatus *status = AStatus_fromStatus(nstatus);
			g_warning("Failed to transact codec %s", AStatus_getDescription(status));
		}
	}
}

static void
fu_binder_daemon_engine_changed_cb(FuEngine *engine, FuBinderDaemon *self)
{
	g_debug("changed cb");
	fu_binder_daemon_send_codec_event(self, NULL, FWUPD_BINDER_LISTENER_CALL_ON_CHANGED);
	fu_daemon_schedule_housekeeping(FU_DAEMON(self));
}

static void
fu_binder_daemon_engine_device_added_cb(FuEngine *engine, FuDevice *device, FuBinderDaemon *self)
{
	g_debug("added cb");
	fu_binder_daemon_send_codec_event(self,
					  FWUPD_CODEC(device),
					  FWUPD_BINDER_LISTENER_CALL_ON_DEVICE_ADDED);
	fu_daemon_schedule_housekeeping(FU_DAEMON(self));
}

static void
fu_binder_daemon_engine_device_removed_cb(FuEngine *engine, FuDevice *device, FuBinderDaemon *self)
{
	g_debug("removed cb");
	fu_binder_daemon_send_codec_event(self,
					  FWUPD_CODEC(device),
					  FWUPD_BINDER_LISTENER_CALL_ON_DEVICE_REMOVED);
	fu_daemon_schedule_housekeeping(FU_DAEMON(self));
}

static void
fu_binder_daemon_engine_device_changed_cb(FuEngine *engine, FuDevice *device, FuBinderDaemon *self)
{
	g_debug("changed cb");
	fu_binder_daemon_send_codec_event(self,
					  FWUPD_CODEC(device),
					  FWUPD_BINDER_LISTENER_CALL_ON_DEVICE_CHANGED);
	fu_daemon_schedule_housekeeping(FU_DAEMON(self));
}

static void
fu_binder_daemon_engine_device_request_cb(FuEngine *engine,
					  FwupdRequest *request,
					  FuBinderDaemon *self)
{
	g_debug("request cb");
	fu_binder_daemon_send_codec_event(self,
					  FWUPD_CODEC(request),
					  FWUPD_BINDER_LISTENER_CALL_ON_DEVICE_REQUEST);
	fu_daemon_schedule_housekeeping(FU_DAEMON(self));
}

static void
fu_binder_daemon_engine_status_changed_cb(FuEngine *engine,
					  FwupdStatus status,
					  FuBinderDaemon *self)
{
	fu_binder_daemon_set_status(self, status);

	/* engine has gone idle */
	if (status == FWUPD_STATUS_SHUTDOWN)
		fu_daemon_stop(FU_DAEMON(self), NULL);
}

static gboolean
fu_binder_daemon_stop(FuDaemon *daemon, GError **error)
{
	// FuBinderDaemon *self = FU_BINDER_DAEMON(daemon);

	// TODO: Clean up self->event_listener_binders

	return TRUE;
}

static gboolean
fu_binder_daemon_start(FuDaemon *daemon, GError **error)
{
	// FuBinderDaemon *self = FU_BINDER_DAEMON(daemon);

	// TODO: Start service here

	// TODO: die
	// AIBinder_linkToDeath(AIBinder *binder, AIBinder_DeathRecipient *recipient, void *cookie)

	// Daemon *daemon = AIBinder_getUserData(self->binder);

	// TODO: Handle reconnecting service daemon

	return TRUE;
}

typedef struct _FuBinderFdSource {
	GSource source;
	gpointer fd_tag;
} FuBinderFdSource;

static gboolean
binder_fd_source_check(GSource *source)
{
	FuBinderFdSource *binder_fd_source = (FuBinderFdSource *)source;
	return g_source_query_unix_fd(source, binder_fd_source->fd_tag) & G_IO_IN;
}

static gboolean
binder_fd_source_dispatch(GSource *source, GSourceFunc callback, gpointer user_data)
{
	binder_status_t nstatus = ABinderProcess_handlePolledCommands();

	if (nstatus != STATUS_OK) {
		AStatus *status = AStatus_fromStatus(nstatus);
		g_warning("failed to handle polled commands %s", AStatus_getDescription(status));
	}

	return G_SOURCE_CONTINUE;
}

static GSourceFuncs binder_fd_source_funcs = {
    NULL,
    binder_fd_source_check,
    binder_fd_source_dispatch,
};

static gboolean
fu_binder_daemon_setup(FuDaemon *daemon,
		       const gchar *socket_address,
		       FuProgress *progress,
		       GError **error)
{
	FuBinderDaemon *self = FU_BINDER_DAEMON(daemon);
	FuEngine *engine = fu_daemon_get_engine(daemon);
	binder_status_t nstatus = STATUS_OK;
	g_autoptr(GSource) source = NULL;
	FuBinderFdSource *binder_fd_source;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_profile(progress, g_getenv("FWUPD_VERBOSE") != NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 99, "load-engine");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 1, "create-sm");

	/* load engine */
	g_signal_connect(FU_ENGINE(engine),
			 "changed",
			 G_CALLBACK(fu_binder_daemon_engine_changed_cb),
			 self);
	g_signal_connect(FU_ENGINE(engine),
			 "device-added",
			 G_CALLBACK(fu_binder_daemon_engine_device_added_cb),
			 self);
	g_signal_connect(FU_ENGINE(engine),
			 "device-removed",
			 G_CALLBACK(fu_binder_daemon_engine_device_removed_cb),
			 self);
	g_signal_connect(FU_ENGINE(engine),
			 "device-changed",
			 G_CALLBACK(fu_binder_daemon_engine_device_changed_cb),
			 self);
	g_signal_connect(FU_ENGINE(engine),
			 "device-request",
			 G_CALLBACK(fu_binder_daemon_engine_device_request_cb),
			 self);
	g_signal_connect(FU_ENGINE(engine),
			 "status-changed",
			 G_CALLBACK(fu_binder_daemon_engine_status_changed_cb),
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

	g_info("waiting for SM");
	// Do we need service started here???
	// TODO: Wait for service manager?
	self->binder = AIBinder_new(self->binder_class, self);

	nstatus = AServiceManager_addService(self->binder, BINDER_SERVICE_NAME);
	if (nstatus != STATUS_OK) {
		AStatus *status = AStatus_fromStatus(nstatus);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to add service %s",
			    AStatus_getDescription(status));
		return FALSE;
	}

	if (AServiceManager_checkService(BINDER_SERVICE_NAME) == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to verify service");
		return FALSE;
	}

	ABinderProcess_setupPolling(&self->binder_fd);

	source = g_source_new(&binder_fd_source_funcs, sizeof(FuBinderFdSource));
	binder_fd_source = (FuBinderFdSource *)source;
	binder_fd_source->fd_tag =
	    g_source_add_unix_fd(source, self->binder_fd, G_IO_IN | G_IO_ERR);
	g_source_attach(source, NULL);

	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static void *
binder_class_on_create(void *user_data)
{
	// Daemon *daemon = calloc(1, sizeof(Daemon));
	// return daemon;
	return user_data;
}

static void
binder_class_on_destroy(void *user_data)
{
	// Daemon *daemon = user_data;
	// free(daemon);
	g_warning("binder on_destroy %p", user_data);
}

static binder_status_t
binder_class_on_transact(AIBinder *binder, transaction_code_t code, const AParcel *in, AParcel *out)
{
	FuBinderDaemon *daemon = AIBinder_getUserData(binder);
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(daemon));
	g_autoptr(FuEngineRequest) request = NULL;
	g_autoptr(GError) error = NULL;
	uid_t binder_caller_uid = 0;
	pid_t binder_caller_pid = 0;

	static const FuBinderDaemonMethodFunc method_funcs[] = {
	    NULL,
	    fu_binder_daemon_method_get_devices,	/* getDevices */
	    fu_binder_daemon_method_install,		/* install */
	    fu_binder_daemon_method_add_event_listener, /* addEventListener */
	    fu_binder_daemon_method_get_upgrades,	/* getUpgrades */
	    fu_binder_daemon_method_get_properties,	/* getProperties */
	    fu_binder_daemon_method_get_remotes,	/* getRemotes */
	    fu_binder_daemon_method_update_metadata,	/* updateMetadata */
	};

	g_debug("binder transaction is %u", code);
	// TODO: debug log in and out
	if (!in)
		g_debug("  in parcel exists");

	/* build request */
	binder_caller_uid = AIBinder_getCallingUid();
	// AIBinder docs warns to be aware that PIDs can be reused by other processes after death
	// And that oneway calls set PID to 0
	binder_caller_pid = AIBinder_getCallingPid();
	request =
	    fu_binder_daemon_create_request(daemon, binder_caller_uid, binder_caller_pid, &error);

	if (request == NULL) {
		fu_binder_daemon_method_invocation_return_error(out, error);
		return STATUS_OK;
	}

	/* activity */
	fu_engine_idle_reset(engine);

	if (code <= 0 || code >= G_N_ELEMENTS(method_funcs)) {
		g_warning("transaction code %d out of range", code);
		return STATUS_INVALID_OPERATION;
	}

	return method_funcs[code](daemon, request, in, out, &error);
}

static void
fu_binder_daemon_init(FuBinderDaemon *self)
{
	self->event_listener_binders =
	    g_ptr_array_new_with_free_func((GDestroyNotify)AIBinder_decStrong);
	// TODO: lifetime? and reconnection
	self->binder_class = AIBinder_Class_define(BINDER_DEFAULT_IFACE,
						   binder_class_on_create,
						   binder_class_on_destroy,
						   binder_class_on_transact);
	// TODO: AIBinder_Class_setTransactionCodeToFunctionNameMap?

	self->listener_binder_class = AIBinder_Class_define(BINDER_EVENT_LISTENER_IFACE,
							    listener_on_create,
							    listener_on_destroy,
							    listener_on_transact);
}

static void
fu_binder_daemon_finalize(GObject *obj)
{
	FuBinderDaemon *self = FU_BINDER_DAEMON(obj);

	if (self->binder != NULL)
		AIBinder_decStrong(self->binder);

	G_OBJECT_CLASS(fu_binder_daemon_parent_class)->finalize(obj);
}

static void
fu_binder_daemon_class_init(FuBinderDaemonClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDaemonClass *daemon_class = FU_DAEMON_CLASS(klass);
	object_class->finalize = fu_binder_daemon_finalize;
	daemon_class->setup = fu_binder_daemon_setup;
	daemon_class->start = fu_binder_daemon_start;
	daemon_class->stop = fu_binder_daemon_stop;
}

FuDaemon *
fu_daemon_new(void)
{
	return FU_DAEMON(g_object_new(FU_TYPE_BINDER_DAEMON, NULL));
}
