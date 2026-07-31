/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuMain"

#include "config.h"

#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#ifdef HAVE_MMAN_H
#include <sys/mman.h>
#endif

#include "fu-context-private.h"
#include "fu-daemon.h"

typedef struct {
	FuEngine *engine;
	GMainLoop *loop;
	guint housekeeping_id;
	gboolean update_in_progress;
	gboolean pending_stop;
	guint process_quit_id;
	FwupdStatus status; /* last emitted */
	guint set_status_id;
	gdouble percentage; /* last emitted */
} FuDaemonPrivate;

enum { PROP_0, PROP_STATUS, PROP_PERCENTAGE, PROP_LAST };

static void
fu_daemon_codec_iface_init(FwupdCodecInterface *iface);

G_DEFINE_TYPE_EXTENDED(FuDaemon,
		       fu_daemon,
		       G_TYPE_OBJECT,
		       0,
		       G_ADD_PRIVATE(FuDaemon)
			   G_IMPLEMENT_INTERFACE(FWUPD_TYPE_CODEC, fu_daemon_codec_iface_init))

#define GET_PRIVATE(o) (fu_daemon_get_instance_private(o))

#define FU_DAEMON_HOUSEKEEPING_DELAY 10 /* seconds */

static void
fu_daemon_set_status_internal(FuDaemon *self, FwupdStatus status)
{
	FuDaemonPrivate *priv = GET_PRIVATE(self);

	/* sanity check */
	if (priv->status == status)
		return;
	priv->status = status;
	g_object_notify(G_OBJECT(self), "status");
}

FwupdStatus
fu_daemon_get_status(FuDaemon *self)
{
	FuDaemonPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DAEMON(self), FWUPD_STATUS_UNKNOWN);
	return priv->status;
}

typedef struct {
	FuDaemon *self;
	FwupdStatus status;
} FuDaemonSetStatusHelper;

static gboolean
fu_daemon_set_status_cb(gpointer user_data)
{
	FuDaemonSetStatusHelper *helper = (FuDaemonSetStatusHelper *)user_data;
	FuDaemon *self = FU_DAEMON(helper->self);
	FuDaemonPrivate *priv = GET_PRIVATE(self);
	priv->set_status_id = 0;
	fu_daemon_set_status_internal(self, helper->status);
	return G_SOURCE_REMOVE;
}

void
fu_daemon_set_status(FuDaemon *self, FwupdStatus status)
{
	FuDaemonPrivate *priv = GET_PRIVATE(self);
	FuDaemonSetStatusHelper *helper;

	g_return_if_fail(FU_IS_DAEMON(self));

	/* cancel anything pending */
	if (priv->set_status_id != 0) {
		g_source_remove(priv->set_status_id);
		priv->set_status_id = 0;
	}

	/* sanity check */
	if (priv->status == status)
		return;

	/* only defer auth prompts to avoid a short-lived spinner */
	if (status != FWUPD_STATUS_WAITING_FOR_AUTH) {
		fu_daemon_set_status_internal(self, status);
		return;
	}

	/* defer waiting-for-auth updates to avoid flickering the UI */
	helper = g_new0(FuDaemonSetStatusHelper, 1);
	helper->self = self;
	helper->status = status;
	priv->set_status_id = g_timeout_add_full(G_PRIORITY_DEFAULT,
						 250, /* ms */
						 fu_daemon_set_status_cb,
						 helper,
						 g_free);
}

void
fu_daemon_set_percentage(FuDaemon *self, gdouble percentage)
{
	FuDaemonPrivate *priv = GET_PRIVATE(self);
	gboolean notify;

	g_return_if_fail(FU_IS_DAEMON(self));

	notify = fwupd_percentage_delta_notify(priv->percentage, percentage);
	priv->percentage = percentage;
	if (notify)
		g_object_notify(G_OBJECT(self), "percentage");
}

gdouble
fu_daemon_get_percentage(FuDaemon *self)
{
	FuDaemonPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DAEMON(self), -1.f);
	return priv->percentage;
}

FuEngine *
fu_daemon_get_engine(FuDaemon *self)
{
	FuDaemonPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DAEMON(self), NULL);
	return priv->engine;
}

void
fu_daemon_set_update_in_progress(FuDaemon *self, gboolean update_in_progress)
{
	FuDaemonPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_DAEMON(self));
	priv->update_in_progress = update_in_progress;
}

gboolean
fu_daemon_get_pending_stop(FuDaemon *self)
{
	FuDaemonPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DAEMON(self), FALSE);
	return priv->pending_stop;
}

static gboolean
fu_daemon_schedule_housekeeping_cb(gpointer user_data)
{
	FuDaemon *self = FU_DAEMON(user_data);
	FuDaemonPrivate *priv = GET_PRIVATE(self);
	FuContext *ctx = fu_engine_get_context(priv->engine);

#ifdef HAVE_MALLOC_TRIM
	/* drop heap except one page */
	malloc_trim(0);
#endif

	/* anything that listens to the context can perform actions now */
	fu_context_housekeeping(ctx);

	/* success */
	priv->housekeeping_id = 0;
	return G_SOURCE_REMOVE;
}

void
fu_daemon_schedule_housekeeping(FuDaemon *self)
{
	FuDaemonPrivate *priv = GET_PRIVATE(self);
	guint delay = FU_DAEMON_HOUSEKEEPING_DELAY;
	if (priv->update_in_progress)
		return;
	if (priv->housekeeping_id != 0)
		g_source_remove(priv->housekeeping_id);
	if (g_getenv("CI") != NULL)
		delay = 1;
	priv->housekeeping_id =
	    g_timeout_add_seconds(delay, fu_daemon_schedule_housekeeping_cb, self);
}

static gboolean
fu_daemon_schedule_process_quit_cb(gpointer user_data)
{
	FuDaemon *self = FU_DAEMON(user_data);
	FuDaemonPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GError) error = NULL;

	g_info("daemon asked to quit, shutting down");
	priv->process_quit_id = 0;

	if (!fu_daemon_stop(self, &error))
		g_warning("failed to stop daemon, will wait: %s\n", error->message);
	return G_SOURCE_REMOVE;
}

void
fu_daemon_schedule_process_quit(FuDaemon *self)
{
	FuDaemonPrivate *priv = GET_PRIVATE(self);

	/* busy? */
	if (priv->update_in_progress) {
		g_warning("asked to quit during a firmware update, ignoring");
		return;
	}

	/* allow the daemon to respond to the request, then quit */
	if (priv->process_quit_id != 0)
		g_source_remove(priv->process_quit_id);
	priv->process_quit_id = g_idle_add(fu_daemon_schedule_process_quit_cb, self);
}

static gboolean
fu_daemon_check_syscall_filtering(GError **error)
{
#ifdef HAVE_MMAN_H
	if (g_getenv("FWUPD_SYSCALL_FILTER") != NULL) {
		const gsize bufsz = 10;
		g_autofree guint8 *buf = g_malloc0(bufsz);
		if (mlock(buf, bufsz) == 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_BROKEN_SYSTEM,
					    "syscall filtering is configured but not working");
			munlock(buf, bufsz);
			return FALSE;
		}
		g_debug("syscall filtering is working");
	}
#endif
	/* success */
	return TRUE;
}

gboolean
fu_daemon_setup(FuDaemon *self, const gchar *socket_address, GError **error)
{
	FuDaemonClass *klass = FU_DAEMON_GET_CLASS(self);
	FuEngine *engine = fu_daemon_get_engine(self);
	FuContext *ctx = fu_engine_get_context(engine);
	guint timer_max_ms;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GTimer) timer = g_timer_new();

	g_return_val_if_fail(FU_IS_DAEMON(self), FALSE);
	g_return_val_if_fail(FU_IS_ENGINE(engine), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* check that the process manager is preventing access to dangerous system calls */
	if (!fu_daemon_check_syscall_filtering(error))
		return FALSE;

	/* proxy on */
	if (!klass->setup(self, socket_address, progress, error))
		return FALSE;

	/* how did we do */
	timer_max_ms = fu_context_get_config_u64(ctx, "IdleInhibitStartupThreshold");
	if (timer_max_ms > 0) {
		guint timer_ms = g_timer_elapsed(timer, NULL) * 1000.f;
		if (timer_ms > timer_max_ms) {
			g_autofree gchar *reason =
			    g_strdup_printf("daemon-startup-%ums-max-%ums", timer_ms, timer_max_ms);
			fu_engine_idle_inhibit(engine, FU_IDLE_INHIBIT_TIMEOUT, reason);
		}
	}

	/* a good place to do the traceback */
	if (fu_progress_get_profile(progress)) {
		g_autofree gchar *str = fu_progress_traceback(progress);
		if (str != NULL)
			g_print("\n%s\n", str);
	}

	/* success */
	return TRUE;
}

gboolean
fu_daemon_start(FuDaemon *self, GError **error)
{
	FuDaemonClass *klass = FU_DAEMON_GET_CLASS(self);
	FuDaemonPrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_IS_DAEMON(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* super useful for debugging */
	if (g_log_get_debug_enabled()) {
		g_autofree gchar *str =
		    fwupd_codec_to_json_string(FWUPD_CODEC(self), FWUPD_CODEC_FLAG_NONE, NULL);
		if (str != NULL)
			g_debug("%s", str);
	}

	/* optional */
	if (klass->start != NULL && !klass->start(self, error))
		return FALSE;

	fu_daemon_schedule_housekeeping(self);
	g_main_loop_run(priv->loop);
	return TRUE;
}

gboolean
fu_daemon_stop(FuDaemon *self, GError **error)
{
	FuDaemonClass *klass = FU_DAEMON_GET_CLASS(self);
	FuDaemonPrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_IS_DAEMON(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (priv->update_in_progress) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "in a firmware update, ignoring");
		priv->pending_stop = TRUE;
		return FALSE;
	}

	/* optional */
	if (klass->stop != NULL && !klass->stop(self, error))
		return FALSE;
	g_main_loop_quit(priv->loop);
	return TRUE;
}

static void
fu_daemon_add_json(FwupdCodec *codec, FwupdJsonObject *json_obj, FwupdCodecFlags flags)
{
	FuDaemon *self = FU_DAEMON(codec);
	FuDaemonPrivate *priv = GET_PRIVATE(self);
	fwupd_json_object_add_string(json_obj, "Version", PACKAGE_VERSION);
	fwupd_json_object_add_string(json_obj, "Status", fwupd_status_to_string(priv->status));
	fwupd_json_object_add_integer(json_obj, "Percentage", (gint64)priv->percentage);
	fwupd_json_object_add_boolean(json_obj, "UpdateInProgress", priv->update_in_progress);
	fwupd_json_object_add_boolean(json_obj, "PendingStop", priv->pending_stop);
	fwupd_codec_to_json(FWUPD_CODEC(priv->engine), json_obj, flags);
}

static void
fu_daemon_codec_iface_init(FwupdCodecInterface *iface)
{
	iface->add_json = fu_daemon_add_json;
}

static void
fu_daemon_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuDaemon *self = FU_DAEMON(object);
	FuDaemonPrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_STATUS:
		g_value_set_uint(value, priv->status);
		break;
	case PROP_PERCENTAGE:
		g_value_set_double(value, priv->percentage);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_daemon_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_daemon_init(FuDaemon *self)
{
	FuDaemonPrivate *priv = GET_PRIVATE(self);
	g_autoptr(FuContext) ctx = fu_context_new();
	priv->engine = fu_engine_new(ctx);
	priv->loop = g_main_loop_new(NULL, FALSE);
	priv->status = FWUPD_STATUS_IDLE;
}

static void
fu_daemon_finalize(GObject *obj)
{
	FuDaemon *self = FU_DAEMON(obj);
	FuDaemonPrivate *priv = GET_PRIVATE(self);

	if (priv->loop != NULL)
		g_main_loop_unref(priv->loop);
	if (priv->housekeeping_id != 0)
		g_source_remove(priv->housekeeping_id);
	if (priv->process_quit_id != 0)
		g_source_remove(priv->process_quit_id);
	if (priv->set_status_id != 0)
		g_source_remove(priv->set_status_id);
	if (priv->engine != NULL)
		g_object_unref(priv->engine);

	G_OBJECT_CLASS(fu_daemon_parent_class)->finalize(obj);
}

static void
fu_daemon_class_init(FuDaemonClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;

	object_class->finalize = fu_daemon_finalize;
	object_class->get_property = fu_daemon_get_property;
	object_class->set_property = fu_daemon_set_property;

	pspec = g_param_spec_uint("status",
				  NULL,
				  NULL,
				  FWUPD_STATUS_UNKNOWN,
				  FWUPD_STATUS_LAST,
				  FWUPD_STATUS_UNKNOWN,
				  G_PARAM_READABLE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_STATUS, pspec);

	pspec = g_param_spec_double("percentage",
				    NULL,
				    NULL,
				    0.0,
				    100.0,
				    0.0,
				    G_PARAM_READABLE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_PERCENTAGE, pspec);
}
