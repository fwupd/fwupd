/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include <config.h>
#include <glib/gi18n.h>
#include <unistd.h>
#include <stdio.h>

#include <fu-debug.h>

typedef struct {
	gboolean	 verbose;
	gboolean	 console;
	gchar		**plugin_verbose;
} FuDebug;

static void
fu_debug_free (FuDebug *self)
{
	g_strfreev (self->plugin_verbose);
	g_free (self);
}

static void
fu_debug_ignore_cb (const gchar *log_domain,
		    GLogLevelFlags log_level,
		    const gchar *message,
		    gpointer user_data)
{
	/* syslog */
	switch (log_level) {
	case G_LOG_LEVEL_INFO:
	case G_LOG_LEVEL_CRITICAL:
	case G_LOG_LEVEL_ERROR:
	case G_LOG_LEVEL_WARNING:
		g_print ("%s\n", message);
		break;
	default:
		break;
	}
}

static void
fu_debug_handler_cb (const gchar *log_domain,
		     GLogLevelFlags log_level,
		     const gchar *message,
		     gpointer user_data)
{
	FuDebug *self = (FuDebug *) user_data;
	g_autofree gchar *tmp = NULL;
	g_autoptr(GDateTime) dt = g_date_time_new_now_utc ();
	g_autoptr(GString) domain = NULL;

	/* time header */
	tmp = g_strdup_printf ("%02i:%02i:%02i:%04i",
			       g_date_time_get_hour (dt),
			       g_date_time_get_minute (dt),
			       g_date_time_get_second (dt),
			       g_date_time_get_microsecond (dt) / 1000);

	/* make these shorter */
	if (g_str_has_prefix (log_domain, "FuPlugin"))
		log_domain += 8;

	/* pad out domain */
	domain = g_string_new (log_domain);
	for (gsize i = domain->len; i < 3; i++)
		g_string_append (domain, " ");

	/* to file */
	if (!self->console) {
		if (tmp != NULL)
			g_print ("%s ", tmp);
		g_print ("%s ", domain->str);
		g_print ("%s\n", message);
		return;
	}

	/* to screen */
	switch (log_level) {
	case G_LOG_LEVEL_ERROR:
	case G_LOG_LEVEL_CRITICAL:
	case G_LOG_LEVEL_WARNING:
		/* critical in red */
		if (tmp != NULL)
			g_print ("%c[%dm%s ", 0x1B, 32, tmp);
		g_print ("%s ", domain->str);
		g_print ("%c[%dm%s\n%c[%dm", 0x1B, 31, message, 0x1B, 0);
		break;
	default:
		/* debug in blue */
		if (tmp != NULL)
			g_print ("%c[%dm%s ", 0x1B, 32, tmp);
		g_print ("%s ", domain->str);
		g_print ("%c[%dm%s\n%c[%dm", 0x1B, 34, message, 0x1B, 0);
		break;
	}
}

static gboolean
fu_debug_pre_parse_hook (GOptionContext *context,
			 GOptionGroup *group,
			 gpointer data,
			 GError **error)
{
	FuDebug *self = (FuDebug *) data;
	const GOptionEntry main_entries[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &self->verbose,
		  /* TRANSLATORS: turn on all debugging */
		  N_("Show debugging information for all files"), NULL },
		{ "plugin-verbose", '\0', 0, G_OPTION_ARG_STRING_ARRAY, &self->plugin_verbose,
		  /* TRANSLATORS: this is for plugin development */
		  N_("Show plugin verbose information"), "PLUGIN-NAME" },
		{ NULL}
	};

	/* add main entry */
	g_option_context_add_main_entries (context, main_entries, NULL);
	return TRUE;
}

static gboolean
fu_debug_post_parse_hook (GOptionContext *context,
			  GOptionGroup *group,
			  gpointer data,
			  GError **error)
{
	FuDebug *self = (FuDebug *) data;

	/* verbose? */
	if (self->verbose) {
		g_setenv ("FWUPD_VERBOSE", "1", TRUE);
		g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR |
					    G_LOG_LEVEL_CRITICAL);
		g_log_set_default_handler (fu_debug_handler_cb, self);
	} else {
		/* hide all debugging */
		g_log_set_handler (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG,
				   fu_debug_ignore_cb, self);
	}

	/* are we on an actual TTY? */
	self->console = (isatty (fileno (stdout)) == 1);
	g_debug ("Verbose debugging %s (on console %i)",
		 self->verbose ? "enabled" : "disabled", self->console);

	/* allow each plugin to be extra verbose */
	if (self->plugin_verbose != NULL) {
		for (guint i = 0; self->plugin_verbose[i] != NULL; i++) {
			g_autofree gchar *name_caps = NULL;
			g_autofree gchar *varname = NULL;
			name_caps = g_ascii_strup (self->plugin_verbose[i], -1);
			varname = g_strdup_printf ("FWUPD_%s_VERBOSE", name_caps);
			g_debug ("setting %s=1", varname);
			g_setenv (varname, "1", TRUE);
		}
	}
	return TRUE;
}

GOptionGroup *
fu_debug_get_option_group (void)
{
	GOptionGroup *group;
	FuDebug *self = g_new0 (FuDebug, 1);
	group = g_option_group_new ("debug",
				    /* TRANSLATORS: for the --verbose arg */
				    _("Debugging Options"),
				    /* TRANSLATORS: for the --verbose arg */
				    _("Show debugging options"),
				    self, (GDestroyNotify) fu_debug_free);
	g_option_group_set_parse_hooks (group,
					fu_debug_pre_parse_hook,
					fu_debug_post_parse_hook);
	return group;
}
