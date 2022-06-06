/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 *
 * - sc create fwupd start="auto" binPath="C:\Program Files (x86)\fwupd\bin\fwupd.exe"
 * - sc delete fwupd
 */

#define G_LOG_DOMAIN "FuMain"

#include "config.h"

#include <fwupdplugin.h>

#include <windows.h>

#include "fu-daemon.h"
#include "fu-debug.h"

static SERVICE_STATUS gSvcStatus = {.dwServiceType = SERVICE_WIN32_OWN_PROCESS,
				    .dwServiceSpecificExitCode = 0};
static SERVICE_STATUS_HANDLE gSvcStatusHandle = 0;
static FuDaemon *gDaemon = NULL;

static void
fu_main_svc_report_status(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint)
{
	static DWORD dwCheckPoint = 1;

	gSvcStatus.dwCurrentState = dwCurrentState;
	gSvcStatus.dwWin32ExitCode = dwWin32ExitCode;
	gSvcStatus.dwWaitHint = dwWaitHint;

	if (dwCurrentState == SERVICE_START_PENDING)
		gSvcStatus.dwControlsAccepted = 0;
	else
		gSvcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;

	if (dwCurrentState == SERVICE_RUNNING || dwCurrentState == SERVICE_STOPPED)
		gSvcStatus.dwCheckPoint = 0;
	else
		gSvcStatus.dwCheckPoint = dwCheckPoint++;

	SetServiceStatus(gSvcStatusHandle, &gSvcStatus);
}

static gboolean
fu_main_svc_control_stop_cb(gpointer user_data)
{
	FuDaemon *daemon = FU_DAEMON(user_data);
	fu_daemon_stop(daemon);
	return G_SOURCE_REMOVE;
}

static void
fu_main_svc_control_cb(DWORD dwCtrl)
{
	switch (dwCtrl) {
	case SERVICE_CONTROL_STOP:
		fu_main_svc_report_status(SERVICE_STOP_PENDING, NO_ERROR, 0);
		/* there is no user_data, because global state with threads is completely fine */
		g_idle_add(fu_main_svc_control_stop_cb, gDaemon);
		fu_main_svc_report_status(gSvcStatus.dwCurrentState, NO_ERROR, 0);
		break;
	default:
		break;
	}
}

static void
fu_main_svc_main_cb(DWORD dwArgc, LPSTR *lpszArgv)
{
	g_autoptr(FuDaemon) daemon = gDaemon = fu_daemon_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GOptionContext) context = g_option_context_new(NULL);

	/* parse debugging args */
	g_option_context_add_group(context, fu_debug_get_option_group());
	if (!g_option_context_parse(context, (gint *)&dwArgc, &lpszArgv, &error)) {
		g_printerr("Failed to parse command line: %s\n", error->message);
		return;
	}

	gSvcStatusHandle = RegisterServiceCtrlHandlerA((LPSTR) "fwupd", fu_main_svc_control_cb);
	if (gSvcStatusHandle == NULL) {
		g_warning("RegisterServiceCtrlHandlerA failed [%u]", (guint)GetLastError());
		return;
	}

	/* set up the daemon, which includes coldplugging devices -- then run it */
	fu_main_svc_report_status(SERVICE_START_PENDING, NO_ERROR, 1000);
	if (!fu_daemon_setup(daemon, FWUPD_DBUS_P2P_SOCKET_ADDRESS, &error)) {
		g_warning("Failed to load daemon: %s", error->message);
		return;
	}
	fu_main_svc_report_status(SERVICE_RUNNING, NO_ERROR, 0);
	fu_daemon_start(daemon);
	fu_main_svc_report_status(SERVICE_STOPPED, NO_ERROR, 0);
}

static int
fu_main_console(int argc, char *argv[])
{
	g_autoptr(FuDaemon) daemon = gDaemon = fu_daemon_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GOptionContext) context = g_option_context_new(NULL);

	/* parse debugging args */
	g_option_context_add_group(context, fu_debug_get_option_group());
	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		g_printerr("Failed to parse command line: %s\n", error->message);
		return EXIT_FAILURE;
	}

	/* set up the daemon, which includes coldplugging devices -- then run it */
	if (!fu_daemon_setup(daemon, FWUPD_DBUS_P2P_SOCKET_ADDRESS, &error)) {
		g_printerr("Failed to load daemon: %s\n", error->message);
		return EXIT_FAILURE;
	}
	fu_daemon_start(daemon);

	/* success */
	g_message("Daemon ready for requests");
	return EXIT_SUCCESS;
}

int
main(int argc, char *argv[])
{
	SERVICE_TABLE_ENTRYA svc_table[] = {{(LPSTR) "fwupd", fu_main_svc_main_cb}, {NULL, NULL}};
	if (!StartServiceCtrlDispatcherA(svc_table)) {
		/* program is being run as a console application rather than as a service */
		if (GetLastError() == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT)
			return fu_main_console(argc, argv);
		g_printerr("StartServiceCtrlDispatcherA failed [%u]\n", (guint)GetLastError());
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
