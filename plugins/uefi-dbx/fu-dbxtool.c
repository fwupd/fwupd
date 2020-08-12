/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>
#include <glib/gi18n.h>
#include <glib-unix.h>
#include <locale.h>
#include <stdlib.h>
#include <unistd.h>

#include "fu-common.h"
#include "fu-efivar.h"
#include "fu-uefi-dbx-common.h"
#include "fu-efi-signature-parser.h"

/* custom return code */
#define EXIT_NOTHING_TO_DO		2

static void
fu_util_ignore_cb (const gchar *log_domain, GLogLevelFlags log_level,
		   const gchar *message, gpointer user_data)
{
}

static const gchar *
fu_dbxtool_convert_guid (const gchar *guid)
{
	if (g_strcmp0 (guid, "77fa9abd-0359-4d32-bd60-28f4e78f784b") == 0)
		return "microsoft";
	return guid;
}

static FuEfiSignatureList *
fu_dbxtool_get_siglist_system (GError **error)
{
	gsize bufsz = 0;
	g_autofree guint8 *buf = NULL;
	if (!fu_efivar_get_data (FU_EFIVAR_GUID_SECURITY_DATABASE, "dbx",
				 &buf, &bufsz, NULL, error))
		return FALSE;
	return fu_efi_signature_parser_one (buf, bufsz,
					    FU_EFI_SIGNATURE_PARSER_FLAGS_NONE,
					    error);
}

static FuEfiSignatureList *
fu_dbxtool_get_siglist_local (const gchar *filename, GError **error)
{
	gsize bufsz = 0;
	g_autofree guint8 *buf = NULL;
	if (!g_file_get_contents (filename, (gchar **) &buf, &bufsz, error))
		return FALSE;
	return fu_efi_signature_parser_one (buf, bufsz,
					    FU_EFI_SIGNATURE_PARSER_FLAGS_IGNORE_HEADER,
					    error);
}

int
main (int argc, char *argv[])
{
	gboolean action_apply = FALSE;
	gboolean action_list = FALSE;
	gboolean force = FALSE;
	gboolean verbose = FALSE;
	g_autofree gchar *dbxfile = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GOptionContext) context = NULL;
	g_autofree gchar *tmp = NULL;
	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			/* TRANSLATORS: command line option */
			_("Show extra debugging information"), NULL },
		{ "list", 'l', 0, G_OPTION_ARG_NONE, &action_list,
			/* TRANSLATORS: command line option */
			_("List entries in dbx"), NULL },
		{ "apply", 'a', 0, G_OPTION_ARG_NONE, &action_apply,
			/* TRANSLATORS: command line option */
			_("Apply update files"), NULL },
		{ "dbx", 'd', 0, G_OPTION_ARG_STRING, &dbxfile,
			/* TRANSLATORS: command line option */
			_("Specify the dbx database file"), "FILENAME" },
		{ "force", 'f', 0, G_OPTION_ARG_NONE, &force,
			/* TRANSLATORS: command line option */
			_("Apply update even when not advised"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, FWUPD_LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* get a action_list of the commands */
	context = g_option_context_new (NULL);
	g_option_context_set_description (context,
		/* TRANSLATORS: description of dbxtool */
		_("This tool allows an administrator to apply UEFI dbx updates."));

	/* TRANSLATORS: program name */
	g_set_application_name (_("UEFI dbx Utility"));
	g_option_context_add_main_entries (context, options, NULL);
	if (!g_option_context_parse (context, &argc, &argv, &error)) {
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

	/* list contents, either of the existing system, or an update */
	if (action_list) {
		GPtrArray *sigs;
		g_autoptr(FuEfiSignatureList) dbx = NULL;
		if (dbxfile != NULL) {
			dbx = fu_dbxtool_get_siglist_local (dbxfile, &error);
			if (dbx == NULL) {
				/* TRANSLATORS: could not read existing system data */
				g_printerr ("%s: %s\n", _("Failed to load local dbx"), error->message);
				return EXIT_FAILURE;
			}
		} else {
			dbx = fu_dbxtool_get_siglist_system (&error);
			if (dbx == NULL) {
				/* TRANSLATORS: could not read existing system data */
				g_printerr ("%s: %s\n", _("Failed to load system dbx"), error->message);
				return EXIT_FAILURE;
			}
		}
		sigs = fu_efi_signature_list_get_all (dbx);
		for (guint i = 0; i < sigs->len; i++) {
			FuEfiSignature *sig = g_ptr_array_index (sigs, i);
			g_print ("%4u: {%s} {%s} %s\n", i + 1,
				 fu_dbxtool_convert_guid (fu_efi_signature_get_owner (sig)),
				 fu_efi_signature_kind_to_string (fu_efi_signature_list_get_kind (dbx)),
				 fu_efi_signature_get_checksum (sig));
		}
		return EXIT_SUCCESS;
	}

#ifdef HAVE_GETUID
	/* ensure root user */
	if (getuid () != 0 || geteuid () != 0) {
		/* TRANSLATORS: we're poking around as a power user */
		g_printerr ("%s\n", _("This program may only work correctly as root"));
	}
#endif

	/* apply update */
	if (action_apply) {
		gsize bufsz = 0;
		g_autofree guint8 *buf = NULL;
		g_autoptr(FuEfiSignatureList) dbx_system = NULL;
		g_autoptr(FuEfiSignatureList) dbx_update = NULL;

		if (dbxfile == NULL) {
			/* TRANSLATORS: user did not include a filename parameter */
			g_printerr ("%s\n", _("Filename required"));
			return EXIT_FAILURE;
		}

		/* TRANSLATORS: reading existing dbx from the system */
		g_print ("%s\n", _("Parsing system dbx…"));
		dbx_system = fu_dbxtool_get_siglist_system (&error);
		if (dbx_system == NULL) {
			/* TRANSLATORS: could not read existing system data */
			g_printerr ("%s: %s\n", _("Failed to load system dbx"), error->message);
			return EXIT_FAILURE;
		}

		/* TRANSLATORS: reading new dbx from the update */
		g_print ("%s\n", _("Parsing dbx update…"));
		if (!g_file_get_contents (dbxfile, (gchar **) &buf, &bufsz, &error)) {
			/* TRANSLATORS: could not read file */
			g_printerr ("%s: %s\n", _("Failed to load local dbx"), error->message);
			return EXIT_FAILURE;
		}
		dbx_update = fu_efi_signature_parser_one (buf, bufsz,
							  FU_EFI_SIGNATURE_PARSER_FLAGS_IGNORE_HEADER,
							  &error);
		if (dbx_update == NULL) {
			/* TRANSLATORS: could not parse file */
			g_printerr ("%s: %s\n", _("Failed to parse local dbx"), error->message);
			return EXIT_FAILURE;
		}

		/* check this is a newer dbx update */
		if (!force && fu_efi_signature_list_are_inclusive (dbx_system, dbx_update)) {
			/* TRANSLATORS: same or newer update already applied */
			g_printerr ("%s\n", _("Cannot apply update as this dbx update has already been applied."));
			return EXIT_FAILURE;
		}

		/* check if on live media */
		if (fu_common_is_live_media () && !force) {
			/* TRANSLATORS: the user is using a LiveCD or LiveUSB install disk */
			g_printerr ("%s\n", _("Cannot apply updates on live media"));
			return EXIT_FAILURE;
		}

		/* TRANSLATORS: actually sending the update to the hardware */
		g_print ("%s\n", _("Applying update…"));
		if (!fu_efivar_set_data (FU_EFIVAR_GUID_SECURITY_DATABASE,
					 "dbx", buf, bufsz,
					 FU_EFIVAR_ATTR_APPEND_WRITE |
					 FU_EFIVAR_ATTR_TIME_BASED_AUTHENTICATED_WRITE_ACCESS |
					 FU_EFIVAR_ATTR_RUNTIME_ACCESS |
					 FU_EFIVAR_ATTR_BOOTSERVICE_ACCESS |
					 FU_EFIVAR_ATTR_NON_VOLATILE,
					 &error)) {
			/* TRANSLATORS: dbx file failed to be applied as an update */
			g_printerr ("%s: %s\n", _("Failed to apply update"), error->message);
			return EXIT_FAILURE;
		}

		/* TRANSLATORS: success */
		g_print ("%s\n", _("Done!"));
		return EXIT_SUCCESS;
	}

	/* nothing specified */
	tmp = g_option_context_get_help (context, TRUE, NULL);
	/* TRANSLATORS: user did not tell the tool what to do */
	g_printerr ("%s\n\n%s", _("No action specified!"), tmp);
	return EXIT_FAILURE;
}
