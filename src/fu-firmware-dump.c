/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <glib/gi18n.h>
#include <locale.h>
#include <stdlib.h>

#include "fu-context-private.h"
#include "fu-engine.h"

typedef struct {
	gboolean	 verbose;
	gint		 timeout;	/* ms */
	GPtrArray	*array;		/* element-type FuFirmware */
	FuEngine	*engine;
} FuUtil;

extern void HF_ITER(guint8 **buf, gsize *len);

static gboolean
fu_firmware_dump_parse (FuUtil *self,
			FuFirmware *firmware,
			GBytes *fw,
			GError **error)
{
	gboolean ret;
	gdouble elapsed_ms;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GTimer) timer = g_timer_new ();

	/* parse, relaxing all the restrictions */
	ret = fu_firmware_parse (firmware, fw,
				 FWUPD_INSTALL_FLAG_NO_SEARCH |
				 FWUPD_INSTALL_FLAG_IGNORE_VID_PID |
				 FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM,
				 &error_local);

	/* a timeout is more important than the actual parse failure */
	elapsed_ms = g_timer_elapsed (timer, NULL) * 1000;
	if (self->timeout > 0 &&
	    elapsed_ms > self->timeout) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_TIMED_OUT,
			     "%s took %.1fms (more than limit of %ims)",
			     G_OBJECT_TYPE_NAME (firmware),
			     elapsed_ms,
			     self->timeout);
		return FALSE;
	}

	/* success? */
	if (!ret) {
		g_propagate_prefixed_error (error,
					    g_steal_pointer (&error_local),
					    "%s failed in %.0lfms: ",
					    G_OBJECT_TYPE_NAME (firmware),
					    elapsed_ms);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_firmware_dump_iter (FuUtil *self, GBytes *blob, GError **error)
{
	gboolean any_okay = FALSE;
	for (guint i = 0; i < self->array->len; i++) {
		FuFirmware *firmware = g_ptr_array_index (self->array, i);
		g_autoptr(GError) error_local = NULL;
		g_autofree gchar *str = NULL;

		if (!fu_firmware_dump_parse (self, firmware, blob, &error_local)) {
			/* timeout so bail */
			if (g_error_matches (error_local,
					     G_IO_ERROR,
					     G_IO_ERROR_TIMED_OUT)) {
				g_propagate_error (error, g_steal_pointer (&error_local));
				return FALSE;
			}
			g_printerr ("%s\n", error_local->message);
			continue;
		}
		str = fu_firmware_to_string (firmware);
		g_print ("%s", str);
		any_okay = TRUE;
	}
	if (!any_okay) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to parse");
		return FALSE;
	}
	return TRUE;
}

static void
fu_firmware_dump_log_cb (const gchar *log_domain,
			 GLogLevelFlags log_level,
			 const gchar *message,
			 gpointer user_data)
{
	FuUtil *self = (FuUtil *) user_data;
	if (log_level == G_LOG_LEVEL_CRITICAL) {
		g_printerr ("CRITICAL: %s\n", message);
		g_assert_not_reached ();
	}
	if (self->verbose)
		g_printerr ("DEBUG: %s\n", message);
}

static void
fu_util_private_free (FuUtil *self)
{
	if (self->array != NULL)
		g_ptr_array_unref (self->array);
	if (self->engine != NULL)
		g_object_unref (self->engine);
	g_free (self);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuUtil, fu_util_private_free)
#pragma clang diagnostic pop

int
main (int argc, char **argv)
{
	FuContext *ctx;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) firmware_types = NULL;
	g_autoptr(GOptionContext) context = NULL;
	g_autoptr(FuUtil) self = g_new0 (FuUtil, 1);
	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &self->verbose,
			/* TRANSLATORS: command line option */
			_("Show extra debugging information"), NULL },
		{ "timeout", 't', 0, G_OPTION_ARG_INT, &self->timeout,
			/* TRANSLATORS: command line option */
			_("Timeout in milliseconds for each parse"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, FWUPD_LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, options, NULL);
	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		/* TRANSLATORS: the user didn't read the man page */
		g_printerr ("%s: %s\n", _("Failed to parse arguments"),
			    error->message);
		return EXIT_FAILURE;
	}

	/* args */
	if (self->verbose) {
		g_setenv ("G_MESSAGES_DEBUG", "all", FALSE);
		g_setenv ("FWUPD_VERBOSE", "1", FALSE);
	}

	/* crashy mccrash face */
	g_log_set_default_handler (fu_firmware_dump_log_cb, self);

	/* load engine */
	self->engine = fu_engine_new (FU_APP_FLAGS_NO_IDLE_SOURCES);
	if (!fu_engine_load (self->engine, FU_ENGINE_LOAD_FLAG_READONLY, &error)) {
		g_printerr ("Failed to load engine: %s\n", error->message);
		return 1;
	}

	/* get all parser objects */
	ctx = fu_engine_get_context (self->engine);
	firmware_types = fu_context_get_firmware_gtype_ids (ctx);
	self->array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (guint i = 0; i < firmware_types->len; i++) {
		const gchar *id = g_ptr_array_index (firmware_types, i);
		GType gtype = fu_context_get_firmware_gtype_by_id (ctx, id);
		g_ptr_array_add (self->array, g_object_new (gtype, NULL));
	}

	/* no args */
	if (argc >= 2) {
		gint rc = 0;
		for (gint i = 1; i < argc; i++) {
			g_autoptr(GBytes) blob = NULL;
			g_autoptr(GError) error_local = NULL;
			blob = fu_common_get_contents_bytes (argv[i], &error_local);
			if (blob == NULL) {
				g_printerr ("failed to load file %s: %s\n",
					    argv[i], error_local->message);
				rc = 2;
				continue;
			}
			if (!fu_firmware_dump_iter (self, blob, &error_local)) {
				g_printerr ("failed to parse file %s: %s\n",
					    argv[i], error_local->message);
				if (g_error_matches (error_local,
						     G_IO_ERROR,
						     G_IO_ERROR_TIMED_OUT)) {
					return 4;
				}
				rc = 3;
				continue;
			}
		}
		return rc;
	}
	for (;;) {
		gsize len = 0;
		guint8 *buf = NULL;
		g_autoptr(GBytes) blob = NULL;
#ifdef HAVE_HF_ITER
		HF_ITER(&buf, &len);
#else
		g_printerr ("no files or HF_ITER data\n");
		return 1;
#endif
		blob = g_bytes_new_static (buf, len);
		for (guint i = 0; i < self->array->len; i++) {
			FuFirmware *firmware = g_ptr_array_index (self->array, i);
			g_autoptr(GError) error_local = NULL;
			if (!fu_firmware_dump_parse (self, firmware, blob, &error_local)) {
				if (g_error_matches (error_local,
						     G_IO_ERROR,
						     G_IO_ERROR_TIMED_OUT)) {
					g_error ("%s", error_local->message);
				}
				g_assert (error_local != NULL);
				continue;
			}
		}
	}
	return 0;
}
