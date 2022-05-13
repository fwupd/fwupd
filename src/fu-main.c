/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuMain"

#include "config.h"

#include <gio/gio.h>
#include <glib/gi18n.h>
#include <locale.h>
#include <stdlib.h>

#ifdef HAVE_GIO_UNIX
#include <glib-unix.h>
#endif
#ifdef HAVE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

#include "fu-daemon.h"
#include "fu-debug.h"

#ifdef HAVE_GIO_UNIX
static gboolean
fu_main_sigterm_cb(gpointer user_data)
{
	FuDaemon *daemon = FU_DAEMON(user_data);
	g_warning("Received SIGTERM");
	fu_daemon_stop(daemon);
	return G_SOURCE_CONTINUE;
}
#endif

static gboolean
fu_main_timed_exit_cb(gpointer user_data)
{
	FuDaemon *daemon = FU_DAEMON(user_data);
	fu_daemon_stop(daemon);
	return G_SOURCE_REMOVE;
}

static void
fu_main_argv_changed_cb(GFileMonitor *monitor,
			GFile *file,
			GFile *other_file,
			GFileMonitorEvent event_type,
			gpointer user_data)
{
	FuDaemon *daemon = FU_DAEMON(user_data);
	g_debug("binary changed, shutting down");
	fu_daemon_stop(daemon);
}

#if GLIB_CHECK_VERSION(2, 63, 3)
static void
fu_main_memory_monitor_warning_cb(GMemoryMonitor *memory_monitor,
				  GMemoryMonitorWarningLevel level,
				  FuDaemon *daemon)
{
	g_debug("OOM event, shutting down");
	fu_daemon_stop(daemon);
}
#endif

static gboolean
fu_main_is_hypervisor(void)
{
	g_autofree gchar *buf = NULL;
	gsize bufsz = 0;
	if (!g_file_get_contents("/proc/cpuinfo", &buf, &bufsz, NULL))
		return FALSE;
	return g_strstr_len(buf, (gssize)bufsz, "hypervisor") != NULL;
}

static gboolean
fu_main_is_container(void)
{
	g_autofree gchar *buf = NULL;
	gsize bufsz = 0;
	if (!g_file_get_contents("/proc/1/cgroup", &buf, &bufsz, NULL))
		return FALSE;
	if (g_strstr_len(buf, (gssize)bufsz, "docker") != NULL)
		return TRUE;
	if (g_strstr_len(buf, (gssize)bufsz, "lxc") != NULL)
		return TRUE;
	return FALSE;
}

int
main(int argc, char *argv[])
{
	gboolean immediate_exit = FALSE;
	gboolean timed_exit = FALSE;
	const gchar *socket_filename = g_getenv("FWUPD_DBUS_SOCKET");
	const GOptionEntry options[] = {
	    {"timed-exit",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &timed_exit,
	     /* TRANSLATORS: exit after we've started up, used for user profiling */
	     N_("Exit after a small delay"),
	     NULL},
	    {"immediate-exit",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &immediate_exit,
	     /* TRANSLATORS: exit straight away, used for automatic profiling */
	     N_("Exit after the engine has loaded"),
	     NULL},
	    {NULL}};
	g_autofree gchar *socket_address = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) argv0_file = g_file_new_for_path(argv[0]);
	g_autoptr(GOptionContext) context = NULL;
	g_autoptr(FuDaemon) daemon = fu_daemon_new();
	g_autoptr(GFileMonitor) argv0_monitor = NULL;
#if GLIB_CHECK_VERSION(2, 63, 3)
	g_autoptr(GMemoryMonitor) memory_monitor = NULL;
#endif

	setlocale(LC_ALL, "");

	bindtextdomain(GETTEXT_PACKAGE, FWUPD_LOCALEDIR);
	bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
	textdomain(GETTEXT_PACKAGE);

	/* TRANSLATORS: program name */
	g_set_application_name(_("Firmware Update Daemon"));
	context = g_option_context_new(NULL);
	g_option_context_add_main_entries(context, options, NULL);
	g_option_context_add_group(context, fu_debug_get_option_group());
	/* TRANSLATORS: program summary */
	g_option_context_set_summary(context, _("Firmware Update D-Bus Service"));
	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		g_printerr("Failed to parse command line: %s\n", error->message);
		return EXIT_FAILURE;
	}

	/* detect the machine kind */
	if (fu_main_is_hypervisor()) {
		fu_daemon_set_machine_kind(daemon, FU_DAEMON_MACHINE_KIND_VIRTUAL);
	} else if (fu_main_is_container()) {
		fu_daemon_set_machine_kind(daemon, FU_DAEMON_MACHINE_KIND_CONTAINER);
	} else {
		fu_daemon_set_machine_kind(daemon, FU_DAEMON_MACHINE_KIND_PHYSICAL);
	}

	/* convert from filename to address, if required */
	if (socket_filename != NULL) {
		if (g_strrstr(socket_filename, "=") == NULL) {
#ifndef HAVE_SYSTEMD
			/* this must be owned by root */
			if (g_file_test(socket_filename, G_FILE_TEST_EXISTS))
				g_unlink(socket_filename);
#endif
			socket_address = g_strdup_printf("unix:path=%s", socket_filename);
		} else {
			socket_address = g_strdup(socket_filename);
		}
	}

	/* set up the daemon, which includes coldplugging devices */
	if (!fu_daemon_setup(daemon, socket_address, &error)) {
		g_printerr("Failed to load daemon: %s\n", error->message);
		return EXIT_FAILURE;
	}

#ifdef HAVE_GIO_UNIX
	g_unix_signal_add_full(G_PRIORITY_DEFAULT, SIGTERM, fu_main_sigterm_cb, daemon, NULL);
#endif /* HAVE_GIO_UNIX */

	/* restart the daemon if the binary gets replaced */
	argv0_monitor = g_file_monitor_file(argv0_file, G_FILE_MONITOR_NONE, NULL, &error);
	g_signal_connect(G_FILE_MONITOR(argv0_monitor),
			 "changed",
			 G_CALLBACK(fu_main_argv_changed_cb),
			 daemon);

#if GLIB_CHECK_VERSION(2, 63, 3)
	/* shut down on low memory event as we can just rescan hardware */
	memory_monitor = g_memory_monitor_dup_default();
	if (memory_monitor != NULL) {
		g_signal_connect(G_MEMORY_MONITOR(memory_monitor),
				 "low-memory-warning",
				 G_CALLBACK(fu_main_memory_monitor_warning_cb),
				 daemon);
	}
#endif

	/* Only timeout and close the mainloop if we have specified it
	 * on the command line */
	if (immediate_exit)
		g_idle_add(fu_main_timed_exit_cb, daemon);
	else if (timed_exit)
		g_timeout_add_seconds(5, fu_main_timed_exit_cb, daemon);

#ifdef HAVE_MALLOC_TRIM
	/* drop heap except one page */
	malloc_trim(4096);
#endif

	/* wait */
	g_message("Daemon ready for requests (locale %s)", g_getenv("LANG"));
	fu_daemon_start(daemon);

#ifdef HAVE_SYSTEMD
	/* notify the service manager */
	sd_notify(0, "STOPPING=1");
#endif

	/* cancel to avoid a deadlock */
	g_file_monitor_cancel(argv0_monitor);

	/* success */
	return EXIT_SUCCESS;
}
