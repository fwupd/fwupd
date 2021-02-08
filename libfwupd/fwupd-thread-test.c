/*
 * Copyright (C) 2020 Philip Withnall <philip@tecnocode.co.uk>
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include <fwupd.h>

typedef struct {
	GApplication	*app;
	FwupdClient	*client;
	GPtrArray	*worker_threads;
} FuThreadTestSelf;

static gboolean
fwupd_thread_test_exit_idle_cb (gpointer user_data)
{
	FuThreadTestSelf *self = user_data;
	g_application_release (self->app);
	return G_SOURCE_REMOVE;
}

static gpointer
fwupd_thread_test_thread_cb (gpointer user_data)
{
	FuThreadTestSelf *self = user_data;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GMainContext) context = g_main_context_new ();
	g_autoptr(GMainContextPusher) pusher = g_main_context_pusher_new (context);

	g_assert (pusher != NULL);
	g_message ("Calling fwupd_client_get_devices() in thread %p with main context %p",
		   g_thread_self (), g_main_context_get_thread_default ());
	devices = fwupd_client_get_devices (self->client, NULL, &error_local);
	if (devices == NULL)
		g_warning ("%s", error_local->message);
	g_idle_add (fwupd_thread_test_exit_idle_cb, self);
	return NULL;
}

static gboolean
fwupd_thread_test_idle_cb (gpointer user_data)
{
	FuThreadTestSelf *self = user_data;

	g_message ("fwupd_thread_test_idle_cb() in thread %p with main context %p",
		   g_thread_self (), g_main_context_get_thread_default ());

	/* create 'n' threads with a small delay, and 'n-1' references on the app */
	for (guint i = 0; i < 30; i++) {
		g_autofree gchar *thread_str = g_strdup_printf ("worker%02u", i);
		GThread *thread = g_thread_new (thread_str,
						fwupd_thread_test_thread_cb,
						self);
		g_usleep (g_random_int_range (0, 1000));
		g_ptr_array_add (self->worker_threads, thread);
		if (i > 0)
			g_application_hold (self->app);
	}

	return G_SOURCE_REMOVE;
}

static void
fwupd_thread_test_activate_cb (GApplication *app, gpointer user_data)
{
	FuThreadTestSelf *self = user_data;
	g_application_hold (self->app);
	g_idle_add (fwupd_thread_test_idle_cb, self);
}

static gboolean
fwupd_thread_test_has_system_bus (void)
{
	g_autoptr(GDBusConnection) conn = NULL;
	conn = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL);
	return conn != NULL;
}

int
main (void)
{
	gint retval;
	g_autoptr(FwupdClient) client = fwupd_client_new ();
	g_autoptr(GApplication) app = g_application_new ("org.test.Test", G_APPLICATION_FLAGS_NONE);
	g_autoptr(GPtrArray) worker_threads = g_ptr_array_new ();
	FuThreadTestSelf self = { app, client, worker_threads };

	/* only some of the CI targets have a DBus daemon */
	if (!fwupd_thread_test_has_system_bus ()) {
		g_message ("D-Bus system bus unavailable, skipping tests.");
		return 0;
	}

	g_message ("Created FwupdClient in thread %p with main context %p",
		   g_thread_self (), g_main_context_get_thread_default ());
	g_signal_connect (app, "activate",
			  G_CALLBACK (fwupd_thread_test_activate_cb), &self);
	retval = g_application_run (app, 0, NULL);
	for (guint i = 0; i < self.worker_threads->len; i++) {
		GThread *thread = g_ptr_array_index (self.worker_threads, i);
		g_thread_join (thread);
	}

	return retval;
}
