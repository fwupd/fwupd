/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 * Copyright 2024 Colin Kinloch <colin.kinloch@collabora.com>
 * Copyright 2026 Harsha Muttavarapu <harshams@google.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuMain"

#include "config.h"

#include <fwupdplugin.h>

#include <android/binder_ibinder.h>
#include <android/binder_process.h>

#include "fwupd-device-private.h"
#include "fwupd-enums-private.h"

#include "fu-binder-daemon-bridge.h"
#include "fu-binder-daemon.h"
#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-engine-helper.h"
#include "fu-engine-requirements.h"
#include "fu-unix-seekable-input-stream.h"

struct _FuBinderDaemon {
	FuDaemon parent_instance;
	gint binder_fd;
};

G_DEFINE_TYPE(FuBinderDaemon, fu_binder_daemon, FU_TYPE_DAEMON)

typedef struct {
	FuBinderDaemon *self;
	FuEngineRequest *request;
	FuProgress *progress;
	gchar *device_id;
	guint64 flags;
	FuInputStream *stream;
	FuCabinet *cabinet;
	GPtrArray *action_ids;
	GPtrArray *releases;
	GPtrArray *errors;
	gchar *remote_id;
} FuBinderDaemonAuthHelper;

static void
fu_binder_daemon_auth_helper_free(FuBinderDaemonAuthHelper *helper)
{
	if (helper->request)
		g_object_unref(helper->request);
	if (helper->progress)
		g_object_unref(helper->progress);
	if (helper->device_id)
		g_free(helper->device_id);
	if (helper->stream)
		g_object_unref(helper->stream);
	if (helper->cabinet)
		g_object_unref(helper->cabinet);
	if (helper->action_ids)
		g_ptr_array_unref(helper->action_ids);
	if (helper->releases)
		g_ptr_array_unref(helper->releases);
	if (helper->errors)
		g_ptr_array_unref(helper->errors);
	if (helper->remote_id)
		g_free(helper->remote_id);
	g_free(helper);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuBinderDaemonAuthHelper, fu_binder_daemon_auth_helper_free)

typedef struct _FuBinderFdSource {
	GSource source;
	gpointer fd_tag;
} FuBinderFdSource;

static gboolean
fu_binder_daemon_fd_source_check(GSource *source)
{
	FuBinderFdSource *binder_fd_source = (FuBinderFdSource *)source;
	return g_source_query_unix_fd(source, binder_fd_source->fd_tag) & G_IO_IN;
}

static gboolean
fu_binder_daemon_fd_source_dispatch(GSource *source, GSourceFunc callback, gpointer user_data)
{
	(void)ABinderProcess_handlePolledCommands();
	return G_SOURCE_CONTINUE;
}

static GSource *
fu_binder_daemon_fd_source_new(gint fd)
{
	static GSourceFuncs binder_fd_source_funcs = {
	    NULL,
	    fu_binder_daemon_fd_source_check,
	    fu_binder_daemon_fd_source_dispatch,
	};
	GSource *source = g_source_new(&binder_fd_source_funcs, sizeof(FuBinderFdSource));
	FuBinderFdSource *binder_fd_source = (FuBinderFdSource *)source;
	binder_fd_source->fd_tag = g_source_add_unix_fd(source, fd, G_IO_IN | G_IO_ERR);
	return source;
}

static void
fu_binder_daemon_progress_percentage_changed_cb(FuProgress *progress,
						gdouble percentage,
						FuBinderDaemon *self)
{
	fu_daemon_set_percentage(FU_DAEMON(self), percentage);
}

FuEngineRequest *
fu_binder_daemon_create_request(FuBinderDaemon *self)
{
	uid_t uid = AIBinder_getCallingUid();
	pid_t pid = AIBinder_getCallingPid();
	g_autofree gchar *sender = g_strdup_printf("%u:%u", (guint)uid, (guint)pid);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(sender);

	fu_engine_request_set_converter_flags(request, FWUPD_CODEC_FLAG_TRUSTED);

	return g_steal_pointer(&request);
}

static gint
fu_binder_daemon_release_sort_cb(gconstpointer a, gconstpointer b)
{
	FuRelease *release1 = *((FuRelease **)a);
	FuRelease *release2 = *((FuRelease **)b);
	return fu_release_compare(release1, release2);
}

static void
fu_binder_daemon_progress_status_changed_cb(FuProgress *progress,
					    FwupdStatus status,
					    FuBinderDaemon *self)
{
	fu_daemon_set_status(FU_DAEMON(self), status);
}

static gboolean
fu_binder_daemon_install_with_helper_device(FuBinderDaemonAuthHelper *helper,
					    XbNode *component,
					    FuDevice *device,
					    GError **error)
{
	FuBinderDaemon *self = helper->self;
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) releases = NULL;

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
		g_ptr_array_add(helper->errors, g_steal_pointer(&error_local));
		return TRUE;
	}

	fu_device_ensure_from_component(device, component);
	fu_device_incorporate_from_component(device, component);

	if (!fu_release_check_version(release, component, helper->flags, &error_local)) {
		g_ptr_array_add(helper->errors, g_steal_pointer(&error_local));
		return TRUE;
	}

	releases = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_INSTALL_ALL_RELEASES)) {
		g_autoptr(GPtrArray) rels = NULL;
		g_autoptr(XbQuery) query = NULL;

		g_ptr_array_add(releases, g_object_ref(release));

		query = xb_query_new_full(xb_node_get_silo(component),
					  "releases/release",
					  XB_QUERY_FLAG_FORCE_NODE_CACHE,
					  error);
		if (query == NULL)
			return FALSE;

		rels = xb_node_query_full(component, query, NULL);
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

	for (guint i = 0; i < releases->len; i++) {
		FuRelease *release_tmp = g_ptr_array_index(releases, i);
		if (!fu_engine_requirements_check(engine,
						  release_tmp,
						  helper->flags,
						  &error_local)) {
			g_ptr_array_add(helper->errors, g_steal_pointer(&error_local));
			continue;
		}
		if (!fu_engine_check_trust(engine, release_tmp, &error_local)) {
			g_ptr_array_add(helper->errors, g_steal_pointer(&error_local));
			continue;
		}

		if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED)) {
			const gchar *action_id = fu_release_get_action_id(release_tmp);
			if (!g_ptr_array_find(helper->action_ids, action_id, NULL))
				g_ptr_array_add(helper->action_ids, g_strdup(action_id));
		}
		g_ptr_array_add(helper->releases, g_object_ref(release_tmp));
	}

	return TRUE;
}

static gboolean
fu_binder_daemon_install_with_helper(FuBinderDaemonAuthHelper *helper, GError **error)
{
	g_autoptr(GPtrArray) components = NULL;
	g_autoptr(GPtrArray) devices_possible = NULL;
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(helper->self));

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

	helper->cabinet = fu_engine_build_cabinet_from_stream(engine, helper->stream, error);
	if (helper->cabinet == NULL)
		return FALSE;

	components = fu_cabinet_get_components(helper->cabinet, error);
	if (components == NULL)
		return FALSE;

	helper->action_ids = g_ptr_array_new_with_free_func(g_free);
	helper->releases = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	helper->errors = g_ptr_array_new_with_free_func((GDestroyNotify)g_error_free);
	helper->remote_id = fu_engine_get_remote_id_for_stream(engine, helper->stream);

	for (guint i = 0; i < components->len; i++) {
		XbNode *component = g_ptr_array_index(components, i);
		for (guint j = 0; j < devices_possible->len; j++) {
			FuDevice *device = g_ptr_array_index(devices_possible, j);
			if (!fu_binder_daemon_install_with_helper_device(helper,
									 component,
									 device,
									 error))
				return FALSE;
		}
	}

	g_ptr_array_sort(helper->releases, fu_binder_daemon_release_sort_cb);

	if (helper->releases->len == 0) {
		GError *error_tmp = fu_engine_error_array_get_best(helper->errors);
		g_propagate_error(error, error_tmp);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_binder_daemon_authorize_install_queue(FuBinderDaemonAuthHelper *helper_ref, GError **error)
{
	FuBinderDaemon *self = helper_ref->self;
	g_autoptr(FuBinderDaemonAuthHelper) helper = helper_ref;
	gboolean ret;
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(helper->self));

	fu_progress_set_profile(helper->progress, g_getenv("FWUPD_VERBOSE") != NULL);
	g_signal_connect(FU_PROGRESS(helper->progress),
			 "percentage-changed",
			 G_CALLBACK(fu_binder_daemon_progress_percentage_changed_cb),
			 helper->self);
	g_signal_connect(FU_PROGRESS(helper->progress),
			 "status-changed",
			 G_CALLBACK(fu_binder_daemon_progress_status_changed_cb),
			 helper->self);

	fu_daemon_set_update_in_progress(FU_DAEMON(self), TRUE);

	ret = fu_engine_install_releases(engine,
					 helper->request,
					 helper->releases,
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

gboolean
fu_binder_daemon_perform_install_bridge(void *daemon_instance,
					const char *device_id,
					int fd_handle,
					guint64 flags,
					GError **error)
{
	FuBinderDaemon *self = FU_BINDER_DAEMON(daemon_instance);
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	FuContext *ctx = fu_engine_get_context(engine);
	g_autoptr(FuBinderDaemonAuthHelper) helper = NULL;

	g_debug("starting install via aidl bridge for device: %s", device_id);

	helper = g_new0(FuBinderDaemonAuthHelper, 1);
	helper->request = fu_binder_daemon_create_request(self);
	helper->progress = fu_progress_new(G_STRLOC);
	helper->device_id = g_strdup(device_id);
	helper->flags = flags;
	helper->self = self;
	helper->stream = fu_unix_seekable_input_stream_new(fd_handle, TRUE, error);
	if (helper->stream == NULL) {
		g_prefix_error_literal(error, "invalid stream: ");
		return FALSE;
	}

	if (fu_context_get_config_bool(ctx, "IgnoreRequirements"))
		helper->flags |= FWUPD_INSTALL_FLAG_IGNORE_REQUIREMENTS;
	if (!fu_binder_daemon_install_with_helper(helper, error))
		return FALSE;
	if (!fu_binder_daemon_authorize_install_queue(g_steal_pointer(&helper), error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_binder_daemon_engine_changed_cb(FuEngine *engine, FuBinderDaemon *self)
{
	g_debug("emitting: changed");
	fu_binder_bridge_emit_changed(self);
	fu_daemon_schedule_housekeeping(FU_DAEMON(self));
}

static void
fu_binder_daemon_engine_device_added_cb(FuEngine *engine, FuDevice *device, FuBinderDaemon *self)
{
	g_debug("emitting: device_added");
	fu_binder_bridge_emit_device_added(self, FWUPD_DEVICE(device));
	fu_daemon_schedule_housekeeping(FU_DAEMON(self));
}

static void
fu_binder_daemon_engine_device_removed_cb(FuEngine *engine, FuDevice *device, FuBinderDaemon *self)
{
	g_debug("emitting: device_removed");
	fu_binder_bridge_emit_device_removed(self, FWUPD_DEVICE(device));
	fu_daemon_schedule_housekeeping(FU_DAEMON(self));
}

static void
fu_binder_daemon_engine_device_changed_cb(FuEngine *engine, FuDevice *device, FuBinderDaemon *self)
{
	g_debug("emitting: device_changed");
	fu_binder_bridge_emit_device_changed(self, FWUPD_DEVICE(device));
	fu_daemon_schedule_housekeeping(FU_DAEMON(self));
}

static void
fu_binder_daemon_engine_device_request_cb(FuEngine *engine,
					  FwupdRequest *request,
					  FuBinderDaemon *self)
{
	g_debug("emitting: device_request");
	fu_binder_bridge_emit_device_request(self, request);
	fu_daemon_schedule_housekeeping(FU_DAEMON(self));
}

static void
fu_binder_daemon_engine_status_changed_cb(FuEngine *engine,
					  FwupdStatus status,
					  FuBinderDaemon *self)
{
	fu_daemon_set_status(FU_DAEMON(self), status);
	if (status == FWUPD_STATUS_SHUTDOWN)
		fu_daemon_stop(FU_DAEMON(self), NULL);
	fu_binder_bridge_emit_changed(self);
}

static void
fu_binder_daemon_status_notify_cb(FuDaemon *daemon, GParamSpec *pspec, gpointer user_data)
{
	FuBinderDaemon *self = FU_BINDER_DAEMON(daemon);
	fu_binder_bridge_emit_changed(self);
}

static void
fu_binder_daemon_percentage_notify_cb(FuDaemon *daemon, GParamSpec *pspec, gpointer user_data)
{
	FuBinderDaemon *self = FU_BINDER_DAEMON(daemon);
	gdouble percentage = fu_daemon_get_percentage(daemon);
	g_debug("emitting PropertyChanged('Percentage'='%.1f%%')", percentage);
	fu_binder_bridge_emit_changed(self);
}

static void
fu_binder_daemon_init(FuBinderDaemon *self)
{
	g_signal_connect(FU_DAEMON(self),
			 "notify::status",
			 G_CALLBACK(fu_binder_daemon_status_notify_cb),
			 NULL);
	g_signal_connect(FU_DAEMON(self),
			 "notify::percentage",
			 G_CALLBACK(fu_binder_daemon_percentage_notify_cb),
			 NULL);
}

static gboolean
fu_binder_daemon_setup(FuDaemon *daemon, const gchar *address, FuProgress *progress, GError **error)
{
	FuBinderDaemon *self = FU_BINDER_DAEMON(daemon);
	FuEngine *engine = fu_daemon_get_engine(daemon);
	g_autoptr(GSource) source = NULL;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_profile(progress, g_getenv("FWUPD_VERBOSE") != NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 100, "load-engine");

	if (!fu_engine_load(engine,
			    FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_HWINFO |
				FU_ENGINE_LOAD_FLAG_REMOTES | FU_ENGINE_LOAD_FLAG_EXTERNAL_PLUGINS |
				FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS |
				FU_ENGINE_LOAD_FLAG_PATH_STORE_DEFAULTS |
				FU_ENGINE_LOAD_FLAG_DEVICE_HOTPLUG | FU_ENGINE_LOAD_FLAG_HISTORY,
			    fu_progress_get_child(progress),
			    error)) {
		return FALSE;
	}
	fu_progress_step_done(progress);

	g_signal_connect(engine, "changed", G_CALLBACK(fu_binder_daemon_engine_changed_cb), self);
	g_signal_connect(engine,
			 "device-added",
			 G_CALLBACK(fu_binder_daemon_engine_device_added_cb),
			 self);
	g_signal_connect(engine,
			 "device-removed",
			 G_CALLBACK(fu_binder_daemon_engine_device_removed_cb),
			 self);
	g_signal_connect(engine,
			 "device-changed",
			 G_CALLBACK(fu_binder_daemon_engine_device_changed_cb),
			 self);
	g_signal_connect(engine,
			 "device-request",
			 G_CALLBACK(fu_binder_daemon_engine_device_request_cb),
			 self);
	g_signal_connect(engine,
			 "status-changed",
			 G_CALLBACK(fu_binder_daemon_engine_status_changed_cb),
			 self);

	fu_binder_daemon_setup_aidl_service(self);

	ABinderProcess_setupPolling(&self->binder_fd);
	source = fu_binder_daemon_fd_source_new(self->binder_fd);
	g_source_attach(source, NULL);

	return TRUE;
}

static void
fu_binder_daemon_finalize(GObject *obj)
{
	G_OBJECT_CLASS(fu_binder_daemon_parent_class)->finalize(obj);
}

static void
fu_binder_daemon_class_init(FuBinderDaemonClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDaemonClass *daemon_class = FU_DAEMON_CLASS(klass);
	object_class->finalize = fu_binder_daemon_finalize;
	daemon_class->setup = fu_binder_daemon_setup;
}

/* nocheck:name */
FuDaemon *
fu_daemon_new(void)
{
	return FU_DAEMON(g_object_new(FU_TYPE_BINDER_DAEMON, NULL));
}
