/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuSpawn"

#include "config.h"

#include "fu-spawn.h"

typedef struct {
	FuSpawnOutputHandler handler_cb;
	gpointer handler_user_data;
	GMainLoop *loop;
	GSource *source;
	GInputStream *stream;
	GCancellable *cancellable;
	guint timeout_id;
} FuSpawnHelper;

static void
fu_spawn_create_pollable_source(FuSpawnHelper *helper);

static gboolean
fu_spawn_source_pollable_cb(GObject *stream, gpointer user_data)
{
	FuSpawnHelper *helper = (FuSpawnHelper *)user_data;
	gchar buffer[1024];
	gssize sz;
	g_auto(GStrv) split = NULL;
	g_autoptr(GError) error = NULL;

	/* read from stream */
	sz = g_pollable_input_stream_read_nonblocking(G_POLLABLE_INPUT_STREAM(stream),
						      buffer,
						      sizeof(buffer) - 1,
						      NULL,
						      &error);
	if (sz < 0) {
		if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK)) {
			g_warning("failed to get read from nonblocking fd: %s", error->message);
		}
		return G_SOURCE_REMOVE;
	}

	/* no read possible */
	if (sz == 0)
		g_main_loop_quit(helper->loop);

	/* emit lines */
	if (helper->handler_cb != NULL) {
		buffer[sz] = '\0';
		split = g_strsplit(buffer, "\n", -1);
		for (guint i = 0; split[i] != NULL; i++) {
			if (split[i][0] == '\0')
				continue;
			helper->handler_cb(split[i], helper->handler_user_data);
		}
	}

	/* set up the source for the next read */
	fu_spawn_create_pollable_source(helper);
	return G_SOURCE_REMOVE;
}

static void
fu_spawn_create_pollable_source(FuSpawnHelper *helper)
{
	if (helper->source != NULL)
		g_source_destroy(helper->source);
	helper->source =
	    g_pollable_input_stream_create_source(G_POLLABLE_INPUT_STREAM(helper->stream),
						  helper->cancellable);
	g_source_attach(helper->source, NULL);
	g_source_set_callback(helper->source,
			      (GSourceFunc)fu_spawn_source_pollable_cb,
			      helper,
			      NULL);
}

static void
fu_spawn_helper_free(FuSpawnHelper *helper)
{
	g_object_unref(helper->cancellable);
	if (helper->stream != NULL)
		g_object_unref(helper->stream);
	if (helper->source != NULL)
		g_source_destroy(helper->source);
	if (helper->loop != NULL)
		g_main_loop_unref(helper->loop);
	if (helper->timeout_id != 0)
		g_source_remove(helper->timeout_id);
	g_free(helper);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuSpawnHelper, fu_spawn_helper_free)
#pragma clang diagnostic pop

#ifndef _WIN32
static gboolean
fu_spawn_timeout_cb(gpointer user_data)
{
	FuSpawnHelper *helper = (FuSpawnHelper *)user_data;
	g_cancellable_cancel(helper->cancellable);
	g_main_loop_quit(helper->loop);
	helper->timeout_id = 0;
	return G_SOURCE_REMOVE;
}

static void
fu_spawn_cancelled_cb(GCancellable *cancellable, FuSpawnHelper *helper)
{
	/* just propagate */
	g_cancellable_cancel(helper->cancellable);
}
#endif

/**
 * fu_spawn_sync:
 * @argv: the argument list to run
 * @handler_cb: (scope call) (nullable): optional #FuSpawnOutputHandler
 * @handler_user_data: (nullable): the user data to pass to @handler_cb
 * @timeout_ms: a timeout in ms, or 0 for no limit
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Runs a subprocess and waits for it to exit. Any output on standard out or
 * standard error will be forwarded to @handler_cb as whole lines.
 *
 * Returns: %TRUE for success
 *
 * Since: 0.9.7
 **/
gboolean
fu_spawn_sync(const gchar *const *argv,
	      FuSpawnOutputHandler handler_cb,
	      gpointer handler_user_data,
	      guint timeout_ms,
	      GCancellable *cancellable,
	      GError **error)
{
	g_autoptr(FuSpawnHelper) helper = NULL;
	g_autoptr(GSubprocess) subprocess = NULL;
	g_autofree gchar *argv_str = NULL;
#ifndef _WIN32
	gulong cancellable_id = 0;
#endif

	g_return_val_if_fail(argv != NULL, FALSE);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* create subprocess */
	argv_str = g_strjoinv(" ", (gchar **)argv);
	g_debug("running '%s'", argv_str);
	subprocess =
	    g_subprocess_newv(argv,
			      G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE,
			      error);
	if (subprocess == NULL)
		return FALSE;

#ifndef _WIN32
	/* watch for process to exit */
	helper = g_new0(FuSpawnHelper, 1);
	helper->handler_cb = handler_cb;
	helper->handler_user_data = handler_user_data;
	helper->loop = g_main_loop_new(NULL, FALSE);
	helper->stream = g_subprocess_get_stdout_pipe(subprocess);

	/* always create a cancellable, and connect up the parent */
	helper->cancellable = g_cancellable_new();
	if (cancellable != NULL) {
		cancellable_id = g_cancellable_connect(cancellable,
						       G_CALLBACK(fu_spawn_cancelled_cb),
						       helper,
						       NULL);
	}

	/* allow timeout */
	if (timeout_ms > 0) {
		helper->timeout_id = g_timeout_add(timeout_ms, fu_spawn_timeout_cb, helper);
	}
	fu_spawn_create_pollable_source(helper);
	g_main_loop_run(helper->loop);
	g_cancellable_disconnect(cancellable, cancellable_id);
#endif
	if (!g_subprocess_wait_check(subprocess, cancellable, error))
		return FALSE;
#ifndef _WIN32
	if (g_cancellable_set_error_if_cancelled(helper->cancellable, error))
		return FALSE;
#endif

	/* success */
	return TRUE;
}
