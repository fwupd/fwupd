/*
 * Copyright (C) 2015 Peter Jones <pjones@redhat.com>
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <glib-unix.h>
#include <glib/gi18n.h>
#include <locale.h>
#include <stdlib.h>
#include <unistd.h>

#include "fu-uefi-dbx-common.h"

/* custom return code */
#define EXIT_NOTHING_TO_DO 2

static void
fu_util_ignore_cb(const gchar *log_domain,
		  GLogLevelFlags log_level,
		  const gchar *message,
		  gpointer user_data)
{
}

static FuFirmware *
fu_dbxtool_get_siglist_system(GError **error)
{
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(FuFirmware) dbx = fu_efi_signature_list_new();
	blob = fu_efivar_get_data_bytes(FU_EFIVAR_GUID_SECURITY_DATABASE, "dbx", NULL, error);
	if (blob == NULL)
		return NULL;
	if (!fu_firmware_parse(dbx, blob, FWUPD_INSTALL_FLAG_NO_SEARCH, error))
		return NULL;
	return g_steal_pointer(&dbx);
}

static FuFirmware *
fu_dbxtool_get_siglist_local(const gchar *filename, GError **error)
{
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(FuFirmware) siglist = fu_efi_signature_list_new();
	blob = fu_bytes_get_contents(filename, error);
	if (blob == NULL)
		return NULL;
	if (!fu_firmware_parse(siglist, blob, FWUPD_INSTALL_FLAG_NONE, error))
		return NULL;
	return g_steal_pointer(&siglist);
}

static gboolean
fu_dbxtool_siglist_inclusive(FuFirmware *outer, FuFirmware *inner)
{
	g_autoptr(GPtrArray) sigs = fu_firmware_get_images(inner);
	for (guint i = 0; i < sigs->len; i++) {
		FuEfiSignature *sig = g_ptr_array_index(sigs, i);
		g_autofree gchar *checksum = NULL;
		g_autoptr(FuFirmware) img = NULL;
		checksum = fu_firmware_get_checksum(FU_FIRMWARE(sig), G_CHECKSUM_SHA256, NULL);
		if (checksum == NULL)
			continue;
		img = fu_firmware_get_image_by_checksum(outer, checksum, NULL);
		if (img == NULL)
			return FALSE;
	}
	return TRUE;
}

static const gchar *
fu_dbxtool_guid_to_string(const gchar *guid)
{
	if (g_strcmp0(guid, FU_EFI_SIGNATURE_GUID_ZERO) == 0)
		return "zero";
	if (g_strcmp0(guid, FU_EFI_SIGNATURE_GUID_MICROSOFT) == 0)
		return "microsoft";
	if (g_strcmp0(guid, FU_EFI_SIGNATURE_GUID_OVMF) == 0 ||
	    g_strcmp0(guid, FU_EFI_SIGNATURE_GUID_OVMF_LEGACY) == 0)
		return "ovmf";
	return guid;
}

int
main(int argc, char *argv[])
{
	gboolean action_apply = FALSE;
	gboolean action_list = FALSE;
	gboolean action_version = FALSE;
	gboolean force = FALSE;
	gboolean verbose = FALSE;
	g_autofree gchar *dbxfile = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GOptionContext) context = NULL;
	g_autofree gchar *tmp = NULL;
	const GOptionEntry options[] = {
	    {"verbose",
	     'v',
	     0,
	     G_OPTION_ARG_NONE,
	     &verbose,
	     /* TRANSLATORS: command line option */
	     N_("Show extra debugging information"),
	     NULL},
	    {"version",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &action_version,
	     /* TRANSLATORS: command line option */
	     N_("Show the calculated version of the dbx"),
	     NULL},
	    {"list",
	     'l',
	     0,
	     G_OPTION_ARG_NONE,
	     &action_list,
	     /* TRANSLATORS: command line option */
	     N_("List entries in dbx"),
	     NULL},
	    {"apply",
	     'a',
	     0,
	     G_OPTION_ARG_NONE,
	     &action_apply,
	     /* TRANSLATORS: command line option */
	     N_("Apply update files"),
	     NULL},
	    {"dbx",
	     'd',
	     0,
	     G_OPTION_ARG_STRING,
	     &dbxfile,
	     /* TRANSLATORS: command line option */
	     N_("Specify the dbx database file"),
	     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
	     N_("FILENAME")},
	    {"force",
	     'f',
	     0,
	     G_OPTION_ARG_NONE,
	     &force,
	     /* TRANSLATORS: command line option */
	     N_("Apply update even when not advised"),
	     NULL},
	    {NULL}};

	setlocale(LC_ALL, "");

	bindtextdomain(GETTEXT_PACKAGE, FWUPD_LOCALEDIR);
	bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
	textdomain(GETTEXT_PACKAGE);

	/* get a action_list of the commands */
	context = g_option_context_new(NULL);
	g_option_context_set_description(
	    context,
	    /* TRANSLATORS: description of dbxtool */
	    _("This tool allows an administrator to apply UEFI dbx updates."));

	/* TRANSLATORS: program name */
	g_set_application_name(_("UEFI dbx Utility"));
	g_option_context_add_main_entries(context, options, NULL);
	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		/* TRANSLATORS: the user didn't read the man page */
		g_print("%s: %s\n", _("Failed to parse arguments"), error->message);
		return EXIT_FAILURE;
	}

	/* set verbose? */
	if (verbose) {
		(void)g_setenv("G_MESSAGES_DEBUG", "all", FALSE);
	} else {
		g_log_set_handler(G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, fu_util_ignore_cb, NULL);
	}

	/* list contents, either of the existing system, or an update */
	if (action_list || action_version) {
		guint cnt = 1;
		g_autoptr(FuFirmware) dbx = NULL;
		g_autoptr(GPtrArray) sigs = NULL;
		if (dbxfile != NULL) {
			dbx = fu_dbxtool_get_siglist_local(dbxfile, &error);
			if (dbx == NULL) {
				g_printerr("%s: %s\n",
					   /* TRANSLATORS: could not read existing system data */
					   _("Failed to load local dbx"),
					   error->message);
				return EXIT_FAILURE;
			}
		} else {
			dbx = fu_dbxtool_get_siglist_system(&error);
			if (dbx == NULL) {
				g_printerr("%s: %s\n",
					   /* TRANSLATORS: could not read existing system data */
					   _("Failed to load system dbx"),
					   error->message);
				return EXIT_FAILURE;
			}
		}
		if (action_version) {
			/* TRANSLATORS: the detected version number of the dbx */
			g_print("%s: %s\n", _("Version"), fu_firmware_get_version(dbx));
			return EXIT_SUCCESS;
		}
		sigs = fu_firmware_get_images(FU_FIRMWARE(dbx));
		for (guint i = 0; i < sigs->len; i++) {
			FuEfiSignature *sig = g_ptr_array_index(sigs, i);
			g_autofree gchar *checksum = NULL;
			checksum =
			    fu_firmware_get_checksum(FU_FIRMWARE(sig), G_CHECKSUM_SHA256, NULL);
			g_print("%4u: {%s} {%s} %s\n",
				cnt++,
				fu_dbxtool_guid_to_string(fu_efi_signature_get_owner(sig)),
				fu_efi_signature_kind_to_string(fu_efi_signature_get_kind(sig)),
				checksum);
		}
		g_debug("version: %s", fu_firmware_get_version(FU_FIRMWARE(dbx)));
		return EXIT_SUCCESS;
	}

#ifdef HAVE_GETUID
	/* ensure root user */
	if (getuid() != 0 || geteuid() != 0) {
		/* TRANSLATORS: we're poking around as a power user */
		g_printerr("%s\n", _("This program may only work correctly as root"));
	}
#endif

	/* apply update */
	if (action_apply) {
		g_autoptr(FuFirmware) dbx_system = NULL;
		g_autoptr(FuFirmware) dbx_update = fu_efi_signature_list_new();
		g_autoptr(GBytes) blob = NULL;

		if (dbxfile == NULL) {
			/* TRANSLATORS: user did not include a filename parameter */
			g_printerr("%s\n", _("Filename required"));
			return EXIT_FAILURE;
		}

		/* TRANSLATORS: reading existing dbx from the system */
		g_print("%s\n", _("Parsing system dbx…"));
		dbx_system = fu_dbxtool_get_siglist_system(&error);
		if (dbx_system == NULL) {
			/* TRANSLATORS: could not read existing system data */
			g_printerr("%s: %s\n", _("Failed to load system dbx"), error->message);
			return EXIT_FAILURE;
		}

		/* TRANSLATORS: reading new dbx from the update */
		g_print("%s\n", _("Parsing dbx update…"));
		blob = fu_bytes_get_contents(dbxfile, &error);
		if (blob == NULL) {
			/* TRANSLATORS: could not read file */
			g_printerr("%s: %s\n", _("Failed to load local dbx"), error->message);
			return EXIT_FAILURE;
		}
		if (!fu_firmware_parse(dbx_update, blob, FWUPD_INSTALL_FLAG_NONE, &error)) {
			/* TRANSLATORS: could not parse file */
			g_printerr("%s: %s\n", _("Failed to parse local dbx"), error->message);
			return EXIT_FAILURE;
		}

		/* check this is a newer dbx update */
		if (!force && fu_dbxtool_siglist_inclusive(dbx_system, dbx_update)) {
			g_printerr("%s\n",
				   /* TRANSLATORS: same or newer update already applied */
				   _("Cannot apply as dbx update has already been applied."));
			return EXIT_FAILURE;
		}

		/* check if on live media */
		if (fu_common_is_live_media() && !force) {
			/* TRANSLATORS: the user is using a LiveCD or LiveUSB install disk */
			g_printerr("%s\n", _("Cannot apply updates on live media"));
			return EXIT_FAILURE;
		}

		/* validate this is safe to apply */
		if (!force) {
			/* TRANSLATORS: ESP refers to the EFI System Partition */
			g_print("%s\n", _("Validating ESP contents…"));
			if (!fu_uefi_dbx_signature_list_validate(FU_EFI_SIGNATURE_LIST(dbx_update),
								 &error)) {
				g_printerr("%s: %s\n",
					   /* TRANSLATORS: something with a blocked hash exists
					    * in the users ESP -- which would be bad! */
					   _("Failed to validate ESP contents"),
					   error->message);
				return EXIT_FAILURE;
			}
		}

		/* TRANSLATORS: actually sending the update to the hardware */
		g_print("%s\n", _("Applying update…"));
		if (!fu_efivar_set_data_bytes(
			FU_EFIVAR_GUID_SECURITY_DATABASE,
			"dbx",
			blob,
			FU_EFIVAR_ATTR_APPEND_WRITE |
			    FU_EFIVAR_ATTR_TIME_BASED_AUTHENTICATED_WRITE_ACCESS |
			    FU_EFIVAR_ATTR_RUNTIME_ACCESS | FU_EFIVAR_ATTR_BOOTSERVICE_ACCESS |
			    FU_EFIVAR_ATTR_NON_VOLATILE,
			&error)) {
			/* TRANSLATORS: dbx file failed to be applied as an update */
			g_printerr("%s: %s\n", _("Failed to apply update"), error->message);
			return EXIT_FAILURE;
		}

		/* TRANSLATORS: success */
		g_print("%s\n", _("Done!"));
		return EXIT_SUCCESS;
	}

	/* nothing specified */
	tmp = g_option_context_get_help(context, TRUE, NULL);
	/* TRANSLATORS: user did not tell the tool what to do */
	g_printerr("%s\n\n%s", _("No action specified!"), tmp);
	return EXIT_FAILURE;
}
