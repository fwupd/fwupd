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

typedef enum {
	FU_OFFLINE_FLAG_NONE		= 0,
	FU_OFFLINE_FLAG_ENABLE		= 1 << 0,
	FU_OFFLINE_FLAG_USE_PROGRESS	= 1 << 1,
} FuOfflineFlag;

struct FuUtilPrivate {
	gchar		*splash_cmd;
	GTimer		*splash_timer;
	FuOfflineFlag	 splash_flags;
};

static gboolean
fu_offline_set_splash_progress (FuUtilPrivate *priv, guint percentage, GError **error)
{
	g_autofree gchar *str = g_strdup_printf ("%u", percentage);
	const gchar *argv[] = { priv->splash_cmd, "system-update", "--progress", str, NULL };

	/* call into plymouth if installed */
	if (priv->splash_flags == FU_OFFLINE_FLAG_NONE) {
		/* TRANSLATORS: console message when not using plymouth */
		g_printerr ("%s: %u%%\n", _("Percentage complete"), percentage);
		return TRUE;
	}

	/* fall back to really old mode that should be supported by anything */
	if ((priv->splash_flags & FU_OFFLINE_FLAG_USE_PROGRESS) == 0) {
		argv[1] = "display-message";
		argv[2] = "--text";
	}
	return fu_common_spawn_sync (argv, NULL, NULL, 200, NULL, error);
}

static gboolean
fu_offline_set_splash_mode (FuUtilPrivate *priv, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	const gchar *argv[] = { priv->splash_cmd, "change-mode", "--system-upgrade", NULL };

	/* call into plymouth if installed */
	if (priv->splash_cmd == NULL) {
		/* TRANSLATORS: console message when no Plymouth is installed */
		g_printerr ("%s\n", _("Installing Firmware…"));
		return TRUE;
	}

	/* try the new fancy mode, then fall back to really old mode */
	if (!fu_common_spawn_sync (argv, NULL, NULL, 1500, NULL, &error_local)) {
		argv[2] = "--updates";
		if (!fu_common_spawn_sync (argv, NULL, NULL, 1500, NULL, error)) {
			g_prefix_error (error, "%s: ", error_local->message);
			return FALSE;
		}
		priv->splash_flags = FU_OFFLINE_FLAG_ENABLE;
		return TRUE;
	}

	/* success */
	priv->splash_flags = FU_OFFLINE_FLAG_ENABLE | FU_OFFLINE_FLAG_USE_PROGRESS;
	return TRUE;
}

static gboolean
fu_offline_set_splash_reboot (FuUtilPrivate *priv, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	const gchar *argv[] = { priv->splash_cmd, "change-mode", "--reboot", NULL };

	/* call into plymouth if installed */
	if (priv->splash_flags == FU_OFFLINE_FLAG_NONE) {
		/* TRANSLATORS: console message when not using plymouth */
		g_printerr ("%s\n", _("Rebooting…"));
		return TRUE;
	}

	/* try the new fancy mode, then fall back to really old mode */
	if (!fu_common_spawn_sync (argv, NULL, NULL, 200, NULL, &error_local)) {
		/* fall back to really old mode that should be supported */
		argv[2] = "--shutdown";
		if (!fu_common_spawn_sync (argv, NULL, NULL, 200, NULL, error)) {
			g_prefix_error (error, "%s: ", error_local->message);
			return FALSE;
		}
		return TRUE;
	}

	/* success */
	return TRUE;
}

static void
fu_offline_client_notify_cb (GObject *object, GParamSpec *pspec, gpointer user_data)
{
	FuUtilPrivate *priv = (FuUtilPrivate *) user_data;
	FwupdClient *client = FWUPD_CLIENT (object);

	/* rate limit to 1 second */
	if (g_timer_elapsed (priv->splash_timer, NULL) < 1.f ||
	    fwupd_client_get_percentage (client) < 5)
		return;
	fu_offline_set_splash_progress (priv, fwupd_client_get_percentage (client), NULL);
	g_timer_reset (priv->splash_timer);
}

static void
fu_util_private_free (FuUtilPrivate *priv)
{
	if (priv->splash_timer != NULL)
		g_timer_destroy (priv->splash_timer);
	g_free (priv->splash_cmd);
	g_free (priv);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuUtilPrivate, fu_util_private_free)
#pragma clang diagnostic pop

int
main (int argc, char *argv[])
{
	gint vercmp;
	guint cnt = 0;
	g_autofree gchar *link = NULL;
	g_autofree gchar *target = fu_common_get_path (FU_PATH_KIND_LOCALSTATEDIR_PKG);
	g_autofree gchar *trigger = fu_common_get_path (FU_PATH_KIND_OFFLINE_TRIGGER);
	g_autoptr(FuHistory) history = NULL;
	g_autoptr(FwupdClient) client = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) results = NULL;
	g_autoptr(FuUtilPrivate) priv = g_new0 (FuUtilPrivate, 1);

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, FWUPD_LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* verify this is pointing to our cache */
	link = g_file_read_link (trigger, NULL);
	if (link == NULL)
		return EXIT_SUCCESS;
	if (g_strcmp0 (link, target) != 0)
		return EXIT_SUCCESS;

	/* do this first to avoid a loop if this tool segfaults */
	g_unlink (trigger);

	/* ensure root user */
#ifdef HAVE_GETUID
	if (getuid () != 0 || geteuid () != 0) {
		/* TRANSLATORS: the user needs to stop playing with stuff */
		g_printerr ("%s\n", _("This tool can only be used by the root user"));
		return EXIT_FAILURE;
	}
#endif

	/* find plymouth, but not an error if not found */
	priv->splash_cmd = g_find_program_in_path ("plymouth");
	priv->splash_timer = g_timer_new ();

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
	g_signal_connect (client, "notify::percentage",
			  G_CALLBACK (fu_offline_client_notify_cb), priv);
	if (!fwupd_client_connect (client, NULL, &error)) {
		/* TRANSLATORS: we could not talk to the fwupd daemon */
		g_printerr ("%s: %s\n", _("Failed to connect to daemon"),
			    error->message);
		return EXIT_FAILURE;
	}

	/* set up splash */
	if (!fu_offline_set_splash_mode (priv, &error)) {
		/* TRANSLATORS: we could not talk to plymouth */
		g_printerr ("%s: %s\n", _("Failed to set splash mode"),
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
		vercmp = fu_common_vercmp_full (fwupd_device_get_version (dev),
						fwupd_release_get_version (rel),
						fwupd_device_get_version_format (dev));
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
					   FWUPD_INSTALL_FLAG_ALLOW_REINSTALL |
					   FWUPD_INSTALL_FLAG_ALLOW_OLDER |
					   FWUPD_INSTALL_FLAG_OFFLINE,
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
	fu_offline_set_splash_reboot (priv, NULL);
	if (!fu_util_update_reboot (&error)) {
		/* TRANSLATORS: we could not reboot for some reason */
		g_printerr ("%s: %s\n", _("Failed to reboot"), error->message);
		return EXIT_FAILURE;
	}

	/* success */
	g_print ("%s\n", _("Done!"));
	return EXIT_SUCCESS;
}
