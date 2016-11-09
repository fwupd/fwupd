/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010-2015 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <config.h>
#include <glib/gi18n.h>
#include <unistd.h>
#include <stdio.h>

#include <fu-debug.h>

static gboolean _verbose = FALSE;
static gboolean _console = FALSE;

gboolean
fu_debug_is_verbose (void)
{
	/* local first */
	if (_verbose)
		return TRUE;

	/* fall back to env variable */
	if (g_getenv ("VERBOSE") != NULL)
		return TRUE;
	return FALSE;
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
		g_print ("%s\n", message);
		break;
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
	gchar str_time[255];
	time_t the_time;

	/* syslog */
	switch (log_level) {
	case G_LOG_LEVEL_INFO:
		g_print ("%s\n", message);
		break;
	case G_LOG_LEVEL_CRITICAL:
	case G_LOG_LEVEL_ERROR:
	case G_LOG_LEVEL_WARNING:
		g_print ("%s\n", message);
		break;
	default:
		break;
	}

	/* time header */
	time (&the_time);
	strftime (str_time, 254, "%H:%M:%S", localtime (&the_time));

	/* no color please, we're British */
	if (!_console) {
		if (log_level == G_LOG_LEVEL_DEBUG) {
			g_print ("%s\t%s\n",
				 str_time, message);
		} else {
			g_print ("***\n%s\t%s\n***\n",
				 str_time, message);
		}
		return;
	}

	/* critical is also in red */
	if (log_level == G_LOG_LEVEL_CRITICAL ||
	    log_level == G_LOG_LEVEL_ERROR) {
		g_print ("%c[%dm%s\t", 0x1B, 32, str_time);
		g_print ("%c[%dm%s\n%c[%dm", 0x1B, 31, message, 0x1B, 0);
	} else {
		/* debug in blue */
		g_print ("%c[%dm%s\t", 0x1B, 32, str_time);
		g_print ("%c[%dm%s\n%c[%dm", 0x1B, 34, message, 0x1B, 0);
	}
}

static gboolean
fu_debug_pre_parse_hook (GOptionContext *context,
			 GOptionGroup *group,
			 gpointer data,
			 GError **error)
{
	const GOptionEntry main_entries[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &_verbose,
		  /* TRANSLATORS: turn on all debugging */
		  N_("Show debugging information for all files"), NULL },
		{ NULL}
	};

	/* add main entry */
	g_option_context_add_main_entries (context, main_entries, NULL);
	return TRUE;
}

void
fu_debug_destroy (void)
{
}

void
fu_debug_setup (gboolean enabled)
{
	if (enabled) {
		g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR |
					    G_LOG_LEVEL_CRITICAL);
		g_log_set_handler (G_LOG_DOMAIN,
				   G_LOG_LEVEL_ERROR |
				   G_LOG_LEVEL_CRITICAL |
				   G_LOG_LEVEL_DEBUG |
				   G_LOG_LEVEL_WARNING,
				   fu_debug_handler_cb, NULL);
	} else {
		/* hide all debugging */
		g_log_set_handler (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG,
				   fu_debug_ignore_cb, NULL);
	}

	/* are we on an actual TTY? */
	_console = (isatty (fileno (stdout)) == 1);
}

static gboolean
fu_debug_post_parse_hook (GOptionContext *context,
			  GOptionGroup *group,
			  gpointer data,
			  GError **error)
{
	/* verbose? */
	fu_debug_setup (_verbose);
	g_debug ("Verbose debugging %s (on console %i)",
		 _verbose ? "enabled" : "disabled", _console);
	return TRUE;
}

GOptionGroup *
fu_debug_get_option_group (void)
{
	GOptionGroup *group;
	group = g_option_group_new ("debug",
				    /* TRANSLATORS: for the --verbose arg */
				    _("Debugging Options"),
				    /* TRANSLATORS: for the --verbose arg */
				    _("Show debugging options"),
				    NULL, NULL);
	g_option_group_set_parse_hooks (group,
					fu_debug_pre_parse_hook,
					fu_debug_post_parse_hook);
	return group;
}
