/*
 * Copyright (C) 2015-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>
#include <locale.h>
#include <stdlib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include "fu-history.h"
#include "fu-plugin-private.h"
#include "fu-util-common.h"

int
main (int argc, char *argv[])
{
	gint vercmp;
	guint cnt = 0;
	g_autofree gchar *link = NULL;
	g_autoptr(FuHistory) history = NULL;
	g_autoptr(FwupdClient) client = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) results = NULL;

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* verify this is pointing to our cache */
	link = g_file_read_link (FU_OFFLINE_TRIGGER_FILENAME, NULL);
	if (link == NULL)
		return EXIT_SUCCESS;
	if (g_strcmp0 (link, "/var/lib/fwupd") != 0)
		return EXIT_SUCCESS;

	/* do this first to avoid a loop if this tool segfaults */
	g_unlink (FU_OFFLINE_TRIGGER_FILENAME);

	/* ensure root user */
	if (getuid () != 0 || geteuid () != 0) {
		/* TRANSLATORS: the user needs to stop playing with stuff */
		g_printerr ("%s\n", _("This tool can only be used by the root user"));
		return EXIT_FAILURE;
	}

	/* ensure D-Bus errors are registered */
	fwupd_error_quark ();

	/* get prepared updates */
	history = fu_history_new ();
	results = fu_history_get_devices (history, &error);
	if (results == NULL) {
		/* TRANSLATORS: we could not get the devices to update offline */
		g_printerr ("%s: %s\n", _("Failed to get pending devices"),
			    error->message);
		return EXIT_FAILURE;
	}

	/* connect to the daemon */
	client = fwupd_client_new ();
	if (!fwupd_client_connect (client, NULL, &error)) {
		/* TRANSLATORS: we could not talk to the fwupd daemon */
		g_printerr ("%s: %s\n", _("Failed to connect to daemon"),
			    error->message);
		return EXIT_FAILURE;
	}

	/* apply each update */
	for (guint i = 0; i < results->len; i++) {
		FwupdDevice *dev = g_ptr_array_index (results, i);
		FwupdRelease *rel = fwupd_device_get_release_default (dev);

		/* check not already done */
		if (fwupd_device_get_update_state (dev) != FWUPD_UPDATE_STATE_PENDING)
			continue;

		/* tell the user what's going to happen */
		vercmp = fu_common_vercmp (fwupd_device_get_version (dev),
					   fwupd_release_get_version (rel));
		if (vercmp == 0) {
			/* TRANSLATORS: the first replacement is a display name
			 * e.g. "ColorHugALS" and the second is a version number
			 * e.g. "1.2.3" */
			g_print (_("Reinstalling %s with %s... "),
				 fwupd_device_get_name (dev),
				 fwupd_release_get_version (rel));
		} else if (vercmp > 0) {
			/* TRANSLATORS: the first replacement is a display name
			 * e.g. "ColorHugALS" and the second and third are
			 * version numbers e.g. "1.2.3" */
			g_print (_("Downgrading %s from %s to %s... "),
				 fwupd_device_get_name (dev),
				 fwupd_device_get_version (dev),
				 fwupd_release_get_version (rel));
		} else if (vercmp < 0) {
			/* TRANSLATORS: the first replacement is a display name
			 * e.g. "ColorHugALS" and the second and third are
			 * version numbers e.g. "1.2.3" */
			g_print (_("Updating %s from %s to %s... "),
				 fwupd_device_get_name (dev),
				 fwupd_device_get_version (dev),
				 fwupd_release_get_version (rel));
		}
		if (!fwupd_client_install (client,
					   fwupd_device_get_id (dev),
					   fwupd_release_get_filename (rel),
					   FWUPD_INSTALL_FLAG_ALLOW_REINSTALL,
					   NULL,
					   &error)) {
			/* TRANSLATORS: we could not install for some reason */
			g_printerr ("%s: %s\n", _("Failed to install firmware update"),
				    error->message);
			return EXIT_FAILURE;
		}
		cnt++;
	}

	/* nothing to do */
	if (cnt == 0) {
		/* TRANSLATORS: nothing was updated offline */
		g_printerr ("%s\n", _("No updates were applied"));
		return EXIT_FAILURE;
	}

	/* reboot */
	if (!fu_util_update_reboot (&error)) {
		/* TRANSLATORS: we could not reboot for some reason */
		g_printerr ("%s: %s\n", _("Failed to reboot"), error->message);
		return EXIT_FAILURE;
	}

	/* success */
	g_print ("%s\n", _("Done!"));
	return EXIT_SUCCESS;
}
