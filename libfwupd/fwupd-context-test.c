/*
 * Copyright (C) 2020 Philip Withnall <pwithnall@endlessos.org>
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include <fwupd.h>

typedef struct {
	GApplication	*app;
	FwupdClient	*client;
	GThread		*main_thread;
	GThread		*worker_thread;
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
	g_autoptr(GMainContext) context = g_main_context_new ();
	g_autoptr(GMainContextPusher) pusher = g_main_context_pusher_new (context);

	g_assert (pusher != NULL);
	g_message ("Calling fwupd_client_get_devices() in thread %p with main context %p",
		   g_thread_self (), g_main_context_get_thread_default ());
	if (!fwupd_client_connect (self->client, NULL, &error_local))
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
	self->worker_thread = g_thread_new ("worker00",
					    fwupd_thread_test_thread_cb,
					    self);
	return G_SOURCE_REMOVE;
}

static void
fwupd_thread_test_activate_cb (GApplication *app, gpointer user_data)
{
	FuThreadTestSelf *self = user_data;
	g_application_hold (self->app);
	g_idle_add (fwupd_thread_test_idle_cb, self);
}

static void
fwupd_thread_test_notify_cb (GObject *object, GParamSpec *pspec, gpointer user_data)
{
	FuThreadTestSelf *self = user_data;
	g_message ("fwupd_thread_test_notify_cb() in thread %p with main context %p",
		   g_thread_self (), g_main_context_get_thread_default ());
	g_assert (g_thread_self () == self->main_thread);
	g_assert (g_main_context_get_thread_default () == NULL);
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
	g_autoptr(GApplication) app = g_application_new ("org.fwupd.ContextTest", G_APPLICATION_FLAGS_NONE);
	g_autoptr(GThread) worker_thread = NULL;
	FuThreadTestSelf self = {
		.app = app,
		.client = client,
		.worker_thread = worker_thread,
		.main_thread = g_thread_self (),
	};

	/* only some of the CI targets have a DBus daemon */
	if (!fwupd_thread_test_has_system_bus ()) {
		g_message ("D-Bus system bus unavailable, skipping tests.");
		return 0;
	}
	g_message ("Created FwupdClient in thread %p with main context %p",
		   g_thread_self (), g_main_context_get_thread_default ());
	g_signal_connect (client, "notify::status",
			  G_CALLBACK (fwupd_thread_test_notify_cb), &self);
	g_signal_connect (app, "activate",
			  G_CALLBACK (fwupd_thread_test_activate_cb), &self);
	retval = g_application_run (app, 0, NULL);
	if (self.worker_thread != NULL)
		g_thread_join (g_steal_pointer (&self.worker_thread));

	return retval;
}
