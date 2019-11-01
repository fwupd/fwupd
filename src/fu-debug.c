/*
 * Copyright (C) 2010-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuDebug"

#include <config.h>
#include <glib/gi18n.h>
#include <unistd.h>
#include <stdio.h>

#include <fu-debug.h>

typedef struct {
	GOptionGroup	*group;
	gboolean	 verbose;
	gboolean	 console;
	gboolean	 no_timestamp;
	gboolean	 no_domain;
	gchar		**plugin_verbose;
	gchar		**daemon_verbose;
} FuDebug;

static void
fu_debug_free (FuDebug *self)
{
	g_option_group_set_parse_hooks (self->group, NULL, NULL);
	g_option_group_unref (self->group);
	g_strfreev (self->plugin_verbose);
	g_strfreev (self->daemon_verbose);
	g_free (self);
}

static gboolean
fu_debug_filter_cb (FuDebug *self, const gchar *log_domain, GLogLevelFlags log_level)
{
	const gchar *domains = g_getenv ("FWUPD_VERBOSE");
	g_auto(GStrv) domains_str = NULL;

	/* include important things by default only */
	if (domains == NULL) {
		if (log_level == G_LOG_LEVEL_INFO ||
		    log_level == G_LOG_LEVEL_CRITICAL ||
		    log_level == G_LOG_LEVEL_WARNING ||
		    log_level == G_LOG_LEVEL_ERROR) {
			return TRUE;
		}
		return FALSE;
	}

	/* everything */
	if (g_strcmp0 (domains, "*") == 0)
		return TRUE;

	/* filter on domain */
	domains_str = g_strsplit (domains, ",", -1);
	return g_strv_contains ((const gchar * const *) domains_str, log_domain);
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

	/* should ignore */
	if (!fu_debug_filter_cb (self, log_domain, log_level))
		return;

	/* time header */
	if (!self->no_timestamp) {
		tmp = g_strdup_printf ("%02i:%02i:%02i:%04i",
				       g_date_time_get_hour (dt),
				       g_date_time_get_minute (dt),
				       g_date_time_get_second (dt),
				       g_date_time_get_microsecond (dt) / 1000);
	}

	/* pad out domain */
	if (!self->no_domain) {
		/* each file should have set this */
		if (log_domain == NULL)
			log_domain = "FIXME";
		domain = g_string_new (log_domain);
		for (gsize i = domain->len; i < 20; i++)
			g_string_append (domain, " ");
	}

	/* to file */
	if (!self->console) {
		if (tmp != NULL)
			g_printerr ("%s ", tmp);
		if (domain != NULL)
			g_printerr ("%s ", domain->str);
		g_printerr ("%s\n", message);
		return;
	}

	/* to screen */
	switch (log_level) {
	case G_LOG_LEVEL_ERROR:
	case G_LOG_LEVEL_CRITICAL:
	case G_LOG_LEVEL_WARNING:
		/* critical in red */
		if (tmp != NULL)
			g_printerr ("%c[%dm%s ", 0x1B, 32, tmp);
		if (domain != NULL)
			g_printerr ("%s ", domain->str);
		g_printerr ("%c[%dm%s\n%c[%dm", 0x1B, 31, message, 0x1B, 0);
		break;
	default:
		/* debug in blue */
		if (tmp != NULL)
			g_printerr ("%c[%dm%s ", 0x1B, 32, tmp);
		if (domain != NULL)
			g_printerr ("%s ", domain->str);
		g_printerr ("%c[%dm%s\n%c[%dm", 0x1B, 34, message, 0x1B, 0);
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
		  N_("Show debugging information for all domains"), NULL },
		{ "no-timestamp", '\0', 0, G_OPTION_ARG_NONE, &self->no_timestamp,
		  /* TRANSLATORS: turn on all debugging */
		  N_("Do not include timestamp prefix"), NULL },
		{ "no-domain", '\0', 0, G_OPTION_ARG_NONE, &self->no_domain,
		  /* TRANSLATORS: turn on all debugging */
		  N_("Do not include log domain prefix"), NULL },
		{ "plugin-verbose", '\0', 0, G_OPTION_ARG_STRING_ARRAY, &self->plugin_verbose,
		  /* TRANSLATORS: this is for plugin development */
		  N_("Show plugin verbose information"), "PLUGIN-NAME" },
		{ "daemon-verbose", '\0', 0, G_OPTION_ARG_STRING_ARRAY, &self->daemon_verbose,
		  /* TRANSLATORS: this is for daemon development */
		  N_("Show daemon verbose information for a particular domain"), "DOMAIN" },
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
		g_setenv ("FWUPD_VERBOSE", "*", TRUE);
	} else if (self->daemon_verbose != NULL) {
		g_autofree gchar *str = g_strjoinv (",", self->daemon_verbose);
		g_setenv ("FWUPD_VERBOSE", str, TRUE);
	}

	/* redirect all domains to be able to change FWUPD_VERBOSE at runtime */
	g_log_set_default_handler (fu_debug_handler_cb, self);

	/* are we on an actual TTY? */
	self->console = (isatty (fileno (stderr)) == 1);
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

/*(transfer): full */
GOptionGroup *
fu_debug_get_option_group (void)
{
	FuDebug *self = g_new0 (FuDebug, 1);
	self->group = g_option_group_new ("debug",
					  /* TRANSLATORS: for the --verbose arg */
					  _("Debugging Options"),
					  /* TRANSLATORS: for the --verbose arg */
					  _("Show debugging options"),
					  self, (GDestroyNotify) fu_debug_free);
	g_option_group_set_parse_hooks (self->group,
					fu_debug_pre_parse_hook,
					fu_debug_post_parse_hook);
	return g_option_group_ref (self->group);
}
