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
#include "fu-engine-installer.h"
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
	guint64 flags;
	FuEngineInstaller *engine_installer;
} FuBinderDaemonAuthHelper;

static void
fu_binder_daemon_auth_helper_free(FuBinderDaemonAuthHelper *helper)
{
	g_object_unref(helper->request);
	g_object_unref(helper->progress);
	g_object_unref(helper->engine_installer);
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

static void
fu_binder_daemon_progress_status_changed_cb(FuProgress *progress,
					    FwupdStatus status,
					    FuBinderDaemon *self)
{
	fu_daemon_set_status(FU_DAEMON(self), status);
}

static gboolean
fu_binder_daemon_authorize_install_queue(FuBinderDaemonAuthHelper *helper, GError **error)
{
	FuBinderDaemon *self = helper->self;
	gboolean ret;
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));

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
					 fu_engine_installer_get_releases(helper->engine_installer),
					 helper->progress,
					 helper->flags,
					 error);
	fu_daemon_set_update_in_progress(FU_DAEMON(self), FALSE);
	if (!ret)
		return FALSE;
	if (fu_daemon_get_pending_stop(FU_DAEMON(self))) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "daemon was stopped");
		return FALSE;
	}

	/* success */
	return TRUE;
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
	g_autoptr(FuInputStream) stream = NULL;

	g_debug("starting install via aidl bridge for device: %s", device_id);

	helper = g_new0(FuBinderDaemonAuthHelper, 1);
	helper->request = fu_binder_daemon_create_request(self);
	helper->progress = fu_progress_new(G_STRLOC);
	helper->engine_installer = fu_engine_installer_new(engine);
	helper->flags = flags;
	helper->self = self;

	fu_engine_installer_set_request(helper->engine_installer, helper->request);

	/* get stream */
	stream = fu_unix_seekable_input_stream_new(fd_handle, TRUE, error);
	if (stream == NULL) {
		g_prefix_error_literal(error, "invalid stream: ");
		return FALSE;
	}

	if (fu_context_get_config_bool(ctx, "IgnoreRequirements"))
		helper->flags |= FWUPD_INSTALL_FLAG_IGNORE_REQUIREMENTS;

	if (!fu_engine_installer_build(helper->engine_installer, device_id, stream, flags, error))
		return FALSE;
	if (!fu_binder_daemon_authorize_install_queue(helper, error))
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
