/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuAgent"

#include "config.h"

#include <fwupd.h>
#include <glib/gi18n.h>
#ifdef HAVE_GIO_UNIX
#include <glib-unix.h>
#endif
#include <locale.h>
#include <stdlib.h>
#include <unistd.h>

#include "fu-common.h"
#include "fu-util-common.h"
#include "fwupd-device-private.h"
#include "fwupd-enums-private.h"

struct FuUtilPrivate {
	GCancellable		*cancellable;
	GMainLoop		*loop;
	GOptionContext		*context;
	FwupdClient		*client;
};

static gboolean
fu_util_add_devices_json (FuUtilPrivate *priv, JsonBuilder *builder, GError **error)
{
	g_autoptr(GPtrArray) devs = NULL;

	/* get results from daemon */
	devs = fwupd_client_get_devices (priv->client, priv->cancellable, error);
	if (devs == NULL)
		return FALSE;

	json_builder_set_member_name (builder, "Devices");
	json_builder_begin_array (builder);
	for (guint i = 0; i < devs->len; i++) {
		FwupdDevice *dev = g_ptr_array_index (devs, i);
		g_autoptr(GPtrArray) rels = NULL;
		g_autoptr(GError) error_local = NULL;

		/* add all releases that could be applied */
		rels = fwupd_client_get_releases (priv->client,
						  fwupd_device_get_id (dev),
						  priv->cancellable,
						  &error_local);
		if (rels == NULL) {
			g_debug ("not adding releases to device: %s",
				 error_local->message);
		} else {
			for (guint j = 0; j < rels->len; j++) {
				FwupdRelease *rel = g_ptr_array_index (rels, j);
				fwupd_device_add_release (dev, rel);
			}
		}

		/* add to builder */
		json_builder_begin_object (builder);
		fwupd_device_to_json (dev, builder);
		json_builder_end_object (builder);
	}
	json_builder_end_array (builder);
	return TRUE;
}

static gboolean
fu_util_add_updates_json (FuUtilPrivate *priv, JsonBuilder *builder, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;

	/* get devices from daemon */
	devices = fwupd_client_get_devices (priv->client, NULL, error);
	if (devices == NULL)
		return FALSE;
	json_builder_set_member_name (builder, "Devices");
	json_builder_begin_array (builder);
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index (devices, i);
		g_autoptr(GPtrArray) rels = NULL;
		g_autoptr(GError) error_local = NULL;

		/* not going to have results, so save a D-Bus round-trip */
		if (!fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_SUPPORTED))
			continue;

		/* get the releases for this device and filter for validity */
		rels = fwupd_client_get_upgrades (priv->client,
						  fwupd_device_get_id (dev),
						  NULL, &error_local);
		if (rels == NULL) {
			g_debug ("no upgrades: %s", error_local->message);
			continue;
		}
		for (guint j = 0; j < rels->len; j++) {
			FwupdRelease *rel = g_ptr_array_index (rels, j);
			fwupd_device_add_release (dev, rel);
		}

		/* add to builder */
		json_builder_begin_object (builder);
		fwupd_device_to_json (dev, builder);
		json_builder_end_object (builder);
	}
	json_builder_end_array (builder);
	return TRUE;
}

static gboolean
fu_util_get_devices (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autofree gchar *data = NULL;
	g_autoptr(JsonBuilder) builder = NULL;
	g_autoptr(JsonGenerator) json_generator = NULL;
	g_autoptr(JsonNode) json_root = NULL;

	/* check args */
	if (g_strv_length (values) != 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments");
		return FALSE;
	}

	/* create header */
	builder = json_builder_new ();
	json_builder_begin_object (builder);
	if (!fu_util_add_devices_json (priv, builder, error))
		return FALSE;
	json_builder_end_object (builder);

	/* export as a string */
	json_root = json_builder_get_root (builder);
	json_generator = json_generator_new ();
	json_generator_set_pretty (json_generator, TRUE);
	json_generator_set_root (json_generator, json_root);
	data = json_generator_to_data (json_generator, NULL);
	if (data == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Failed to convert to JSON string");
		return FALSE;
	}

	/* just print */
	g_print ("%s\n", data);
	return TRUE;
}

static gboolean
fu_util_get_updates (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autofree gchar *data = NULL;
	g_autoptr(JsonBuilder) builder = NULL;
	g_autoptr(JsonGenerator) json_generator = NULL;
	g_autoptr(JsonNode) json_root = NULL;

	/* check args */
	if (g_strv_length (values) != 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments");
		return FALSE;
	}

	/* create header */
	builder = json_builder_new ();
	json_builder_begin_object (builder);
	if (!fu_util_add_updates_json (priv, builder, error))
		return FALSE;
	json_builder_end_object (builder);

	/* export as a string */
	json_root = json_builder_get_root (builder);
	json_generator = json_generator_new ();
	json_generator_set_pretty (json_generator, TRUE);
	json_generator_set_root (json_generator, json_root);
	data = json_generator_to_data (json_generator, NULL);
	if (data == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Failed to convert to JSON string");
		return FALSE;
	}

	/* just print */
	g_print ("%s\n", data);
	return TRUE;
}

static void
fu_util_ignore_cb (const gchar *log_domain, GLogLevelFlags log_level,
		   const gchar *message, gpointer user_data)
{
}

#ifdef HAVE_GIO_UNIX
static gboolean
fu_util_sigint_cb (gpointer user_data)
{
	FuUtilPrivate *priv = (FuUtilPrivate *) user_data;
	g_debug ("Handling SIGINT");
	g_cancellable_cancel (priv->cancellable);
	return FALSE;
}
#endif

static void
fu_util_private_free (FuUtilPrivate *priv)
{
	if (priv->client != NULL)
		g_object_unref (priv->client);
	g_main_loop_unref (priv->loop);
	g_object_unref (priv->cancellable);
	g_option_context_free (priv->context);
	g_free (priv);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuUtilPrivate, fu_util_private_free)
#pragma clang diagnostic pop

int
main (int argc, char *argv[])
{
	gboolean ret;
	gboolean verbose = FALSE;
	g_autoptr(FuUtilPrivate) priv = g_new0 (FuUtilPrivate, 1);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) cmd_array = fu_util_cmd_array_new ();
	g_autofree gchar *cmd_descriptions = NULL;
	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			/* TRANSLATORS: command line option */
			_("Show extra debugging information"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, FWUPD_LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* ensure D-Bus errors are registered */
	fwupd_error_quark ();

	/* create helper object */
	priv->loop = g_main_loop_new (NULL, FALSE);
	priv->client = fwupd_client_new ();

	/* add commands */
	fu_util_cmd_array_add (cmd_array,
			       "get-devices", NULL,
			       /* TRANSLATORS: command description */
			       _("Get all devices and possible releases"),
			       fu_util_get_devices);
	fu_util_cmd_array_add (cmd_array,
			       "get-updates,get-upgrades", NULL,
			       /* TRANSLATORS: command description */
			       _("Gets the list of updates for connected hardware"),
			       fu_util_get_updates);

	/* sort by command name */
	fu_util_cmd_array_sort (cmd_array);

	/* do stuff on ctrl+c */
	priv->cancellable = g_cancellable_new ();
#ifdef HAVE_GIO_UNIX
	g_unix_signal_add_full (G_PRIORITY_DEFAULT,
				SIGINT, fu_util_sigint_cb,
				priv, NULL);
#endif

	/* get a list of the commands */
	priv->context = g_option_context_new (NULL);
	cmd_descriptions = fu_util_cmd_array_to_string (cmd_array);
	g_option_context_set_summary (priv->context, cmd_descriptions);
	g_option_context_set_description (priv->context,
		"This tool can be used from other tools and from shell scripts.");

	/* TRANSLATORS: program name */
	g_set_application_name (_("Firmware Agent"));
	g_option_context_add_main_entries (priv->context, options, NULL);
	ret = g_option_context_parse (priv->context, &argc, &argv, &error);
	if (!ret) {
		/* TRANSLATORS: the user didn't read the man page */
		g_print ("%s: %s\n", _("Failed to parse arguments"),
			 error->message);
		return EXIT_FAILURE;
	}

	/* set verbose? */
	if (verbose) {
		g_setenv ("G_MESSAGES_DEBUG", "all", FALSE);
	} else {
		g_log_set_handler (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG,
				   fu_util_ignore_cb, NULL);
	}

	/* run the specified command */
	ret = fu_util_cmd_array_run (cmd_array, priv, argv[1], (gchar**) &argv[2], &error);
	if (!ret) {
		if (g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_INVALID_ARGS)) {
			g_autofree gchar *tmp = NULL;
			tmp = g_option_context_get_help (priv->context, TRUE, NULL);
			g_print ("%s\n\n%s", error->message, tmp);
			return EXIT_FAILURE;
		}
		g_print ("%s\n", error->message);
		return EXIT_FAILURE;
	}

	/* success */
	return EXIT_SUCCESS;
}
