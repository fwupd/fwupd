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
	FuDaemonMachineKind machine_kind;
	guint housekeeping_id;
	gboolean update_in_progress;
	gboolean pending_stop;
	guint process_quit_id;
} FuDaemonPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuDaemon, fu_daemon, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fu_daemon_get_instance_private(o))

#define FU_DAEMON_HOUSEKEEPING_DELAY 10 /* seconds */

FuEngine *
fu_daemon_get_engine(FuDaemon *self)
{
	FuDaemonPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DAEMON(self), NULL);
	return priv->engine;
}

void
fu_daemon_set_machine_kind(FuDaemon *self, FuDaemonMachineKind machine_kind)
{
	FuDaemonPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_DAEMON(self));
	priv->machine_kind = machine_kind;
}

FuDaemonMachineKind
fu_daemon_get_machine_kind(FuDaemon *self)
{
	FuDaemonPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DAEMON(self), 0);
	return priv->machine_kind;
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
	if (priv->update_in_progress)
		return;
	if (priv->housekeeping_id != 0)
		g_source_remove(priv->housekeeping_id);
	priv->housekeeping_id = g_timeout_add_seconds(FU_DAEMON_HOUSEKEEPING_DELAY,
						      fu_daemon_schedule_housekeeping_cb,
						      self);
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
	FuDaemonPrivate *priv = GET_PRIVATE(self);
	FuEngine *engine = fu_daemon_get_engine(self);
	const gchar *machine_kind = g_getenv("FWUPD_MACHINE_KIND");
	guint timer_max_ms;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GTimer) timer = g_timer_new();

	g_return_val_if_fail(FU_IS_DAEMON(self), FALSE);
	g_return_val_if_fail(FU_IS_ENGINE(engine), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* check that the process manager is preventing access to dangerous system calls */
	if (!fu_daemon_check_syscall_filtering(error))
		return FALSE;

	/* allow overriding for development */
	if (machine_kind != NULL) {
		priv->machine_kind = fu_daemon_machine_kind_from_string(machine_kind);
		if (priv->machine_kind == FU_DAEMON_MACHINE_KIND_UNKNOWN) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid machine kind specified: %s",
				    machine_kind);
			return FALSE;
		}
	}

	/* proxy on */
	if (!klass->setup(self, socket_address, progress, error))
		return FALSE;

	/* how did we do */
	timer_max_ms = fu_config_get_value_u64(FU_CONFIG(fu_engine_get_config(engine)),
					       "fwupd",
					       "IdleInhibitStartupThreshold");
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
fu_daemon_init(FuDaemon *self)
{
	FuDaemonPrivate *priv = GET_PRIVATE(self);
	g_autoptr(FuContext) ctx = fu_context_new();
	priv->engine = fu_engine_new(ctx);
	priv->loop = g_main_loop_new(NULL, FALSE);
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
	if (priv->engine != NULL)
		g_object_unref(priv->engine);

	G_OBJECT_CLASS(fu_daemon_parent_class)->finalize(obj);
}

static void
fu_daemon_class_init(FuDaemonClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_daemon_finalize;
}
