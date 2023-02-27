/*
 * Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuDebug"

#include "config.h"

#include <fu-debug.h>
#include <glib/gi18n.h>
#include <stdio.h>
#include <unistd.h>

#ifdef _WIN32
#include <fwupd-windows.h>
#include <windows.h>
#endif

typedef struct {
	GOptionGroup *group;
	GLogLevelFlags log_level;
	gboolean console;
	gboolean no_timestamp;
	gboolean no_domain;
	gchar **daemon_verbose;
#ifdef _WIN32
	HANDLE event_source;
#endif
} FuDebug;

static const gchar *
fu_debug_log_level_to_string(GLogLevelFlags log_level)
{
	if (log_level == G_LOG_LEVEL_ERROR)
		return "error";
	if (log_level == G_LOG_LEVEL_CRITICAL)
		return "critical";
	if (log_level == G_LOG_LEVEL_WARNING)
		return "warning";
	if (log_level == G_LOG_LEVEL_MESSAGE)
		return "message";
	if (log_level == G_LOG_LEVEL_INFO)
		return "info";
	if (log_level == G_LOG_LEVEL_DEBUG)
		return "debug";
	return NULL;
}

static void
fu_debug_free(FuDebug *self)
{
	g_option_group_set_parse_hooks(self->group, NULL, NULL);
	g_option_group_unref(self->group);
	g_strfreev(self->daemon_verbose);
#ifdef _WIN32
	DeregisterEventSource(self->event_source);
#endif
	g_free(self);
}

static gboolean
fu_debug_filter_cb(FuDebug *self, const gchar *log_domain, GLogLevelFlags log_level)
{
	/* trivial */
	if (log_level <= self->log_level)
		return TRUE;

	/* filter on domain */
	if (self->daemon_verbose != NULL && log_domain != NULL)
		return g_strv_contains((const char *const *)self->daemon_verbose, log_domain);

	/* nope */
	return FALSE;
}

#ifdef _WIN32
static void
fu_debug_handler_win32(FuDebug *self, GLogLevelFlags log_level, const gchar *msg)
{
	WORD ev_type = 0x0;

	/* nothing to do */
	if (self->event_source == NULL)
		return;

	/* map levels */
	switch (log_level) {
	case G_LOG_LEVEL_INFO:
	case G_LOG_LEVEL_MESSAGE:
		ev_type = EVENTLOG_INFORMATION_TYPE;
		break;
	case G_LOG_LEVEL_WARNING:
		ev_type = EVENTLOG_WARNING_TYPE;
		break;
	case G_LOG_LEVEL_ERROR:
	case G_LOG_LEVEL_CRITICAL:
		ev_type = EVENTLOG_ERROR_TYPE;
		break;
	default:
		return;
		break;
	}

	/* add to log */
	ReportEventA(self->event_source,
		     ev_type,
		     FWUPD_CATEGORY_GENERIC,
		     FWUPD_MESSAGE_GENERIC,
		     NULL,
		     1,
		     0,
		     (const char **)&msg,
		     NULL);
}
#endif

static void
fu_debug_handler_cb(const gchar *log_domain,
		    GLogLevelFlags log_level,
		    const gchar *message,
		    gpointer user_data)
{
	FuDebug *self = (FuDebug *)user_data;
	g_autofree gchar *timestamp = NULL;
	g_autoptr(GString) domain = NULL;

#ifdef _WIN32
	/* use Windows event log */
	fu_debug_handler_win32(self, log_level, message);
#endif

	/* should ignore */
	if (!fu_debug_filter_cb(self, log_domain, log_level))
		return;

	/* time header */
	if (!self->no_timestamp) {
		g_autoptr(GDateTime) dt = g_date_time_new_now_utc();
		timestamp = g_strdup_printf("%02i:%02i:%02i.%03i",
					    g_date_time_get_hour(dt),
					    g_date_time_get_minute(dt),
					    g_date_time_get_second(dt),
					    g_date_time_get_microsecond(dt) / 1000);
	}

	/* pad out domain */
	if (!self->no_domain) {
		/* each file should have set this */
		if (log_domain == NULL)
			log_domain = "FIXME";
		domain = g_string_new(log_domain);
		for (gsize i = domain->len; i < 20; i++)
			g_string_append(domain, " ");
	}

	/* to file */
	if (!self->console) {
		g_autofree gchar *ascii_message = g_str_to_ascii(message, NULL);
		if (timestamp != NULL)
			g_printerr("%s ", timestamp);
		if (domain != NULL)
			g_printerr("%s ", domain->str);
		g_printerr("%s\n", ascii_message);
		return;
	}

	/* plain output */
	if (g_getenv("NO_COLOR") != NULL) {
		if (timestamp != NULL)
			g_printerr("%s ", timestamp);
		if (domain != NULL)
			g_printerr("%s ", domain->str);
		g_printerr("%s\n", message);
		return;
	}

	/* to screen */
	switch (log_level) {
	case G_LOG_LEVEL_ERROR:
	case G_LOG_LEVEL_CRITICAL:
	case G_LOG_LEVEL_WARNING:
		/* critical in red */
		if (timestamp != NULL)
			g_printerr("%c[%dm%s ", 0x1B, 32, timestamp);
		if (domain != NULL)
			g_printerr("%s ", domain->str);
		g_printerr("%c[%dm%s\n%c[%dm", 0x1B, 31, message, 0x1B, 0);
		break;
	default:
		/* debug in blue */
		if (timestamp != NULL)
			g_printerr("%c[%dm%s ", 0x1B, 32, timestamp);
		if (domain != NULL)
			g_printerr("%s ", domain->str);
		g_printerr("%c[%dm%s\n%c[%dm", 0x1B, 34, message, 0x1B, 0);
		break;
	}
}

static gboolean
fu_debug_verbose_arg_cb(const gchar *option_name,
			const gchar *value,
			gpointer user_data,
			GError **error)
{
	FuDebug *self = (FuDebug *)user_data;
	if (self->log_level == G_LOG_LEVEL_MESSAGE) {
		self->log_level = G_LOG_LEVEL_INFO;
		return TRUE;
	}
	if (self->log_level == G_LOG_LEVEL_INFO) {
		self->log_level = G_LOG_LEVEL_DEBUG;
		return TRUE;
	}
	g_set_error_literal(error,
			    G_OPTION_ERROR,
			    G_OPTION_ERROR_FAILED,
			    "No further debug level supported");
	return FALSE;
}

static gboolean
fu_debug_pre_parse_hook(GOptionContext *context, GOptionGroup *group, gpointer data, GError **error)
{
	FuDebug *self = (FuDebug *)data;
	const GOptionEntry entries[] = {
	    {"verbose",
	     'v',
	     G_OPTION_FLAG_NO_ARG,
	     G_OPTION_ARG_CALLBACK,
	     (GOptionArgFunc)fu_debug_verbose_arg_cb,
	     /* TRANSLATORS: turn on all debugging */
	     N_("Show debugging information for all domains"),
	     NULL},
	    {"no-timestamp",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &self->no_timestamp,
	     /* TRANSLATORS: turn on all debugging */
	     N_("Do not include timestamp prefix"),
	     NULL},
	    {"no-domain",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &self->no_domain,
	     /* TRANSLATORS: turn on all debugging */
	     N_("Do not include log domain prefix"),
	     NULL},
	    {"daemon-verbose",
	     '\0',
	     0,
	     G_OPTION_ARG_STRING_ARRAY,
	     &self->daemon_verbose,
	     /* TRANSLATORS: this is for daemon development */
	     N_("Show daemon verbose information for a particular domain"),
	     "DOMAIN"},
	    {NULL}};

	/* set from FuConfig */
	if (g_strcmp0(g_getenv("FWUPD_VERBOSE"), "*") == 0)
		self->log_level = G_LOG_LEVEL_DEBUG;

	g_option_group_add_entries(group, entries);
	return TRUE;
}

static gboolean
fu_debug_post_parse_hook(GOptionContext *context,
			 GOptionGroup *group,
			 gpointer data,
			 GError **error)
{
	FuDebug *self = (FuDebug *)data;

	/* for compat */
	if (self->log_level == G_LOG_LEVEL_DEBUG)
		(void)g_setenv("FWUPD_VERBOSE", "1", TRUE);

	/* redirect all domains */
	g_log_set_default_handler(fu_debug_handler_cb, self);

	/* are we on an actual TTY? */
	self->console = (isatty(fileno(stderr)) == 1);
	g_info("verbose to %s (on console %i)",
	       fu_debug_log_level_to_string(self->log_level),
	       self->console);

	return TRUE;
}

#ifdef _WIN32
static void
fu_debug_setup_event_source(FuDebug *self)
{
	HKEY key;
	gchar msgfile[MAX_PATH];
	DWORD dwData = EVENTLOG_ERROR_TYPE | EVENTLOG_WARNING_TYPE | EVENTLOG_INFORMATION_TYPE;

	if (RegCreateKeyExA(HKEY_LOCAL_MACHINE,
			    "SYSTEM\\CurrentControlSet\\Services\\"
			    "EventLog\\Application\\fwupd",
			    0,
			    NULL,
			    REG_OPTION_NON_VOLATILE,
			    KEY_SET_VALUE,
			    NULL,
			    &key,
			    NULL) != ERROR_SUCCESS) {
		g_warning("RegCreateKeyExA failed [%u]", (guint)GetLastError());
		return;
	}
	GetModuleFileNameA(NULL, msgfile, MAX_PATH);
	RegSetValueExA(key,
		       "EventMessageFile",
		       0,
		       REG_EXPAND_SZ,
		       (BYTE *)msgfile,
		       strlen(msgfile) + 1);
	RegSetValueExA(key, "TypesSupported", 0, REG_DWORD, (BYTE *)&dwData, sizeof(dwData));
	RegCloseKey(key);

	/* good to go */
	self->event_source = RegisterEventSourceA(NULL, "fwupd");
}
#endif

/*(transfer): full */
GOptionGroup *
fu_debug_get_option_group(void)
{
	FuDebug *self = g_new0(FuDebug, 1);
	self->log_level = G_LOG_LEVEL_MESSAGE;
	self->group = g_option_group_new("debug",
					 /* TRANSLATORS: for the --verbose arg */
					 _("Debugging Options"),
					 /* TRANSLATORS: for the --verbose arg */
					 _("Show debugging options"),
					 self,
					 (GDestroyNotify)fu_debug_free);
	g_option_group_set_parse_hooks(self->group,
				       fu_debug_pre_parse_hook,
				       fu_debug_post_parse_hook);
#ifdef _WIN32
	fu_debug_setup_event_source(self);
#endif
	return g_option_group_ref(self->group);
}
