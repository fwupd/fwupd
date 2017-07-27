/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
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

#include "config.h"

#include <fwupd.h>
#include <appstream-glib.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <glib/gi18n.h>
#include <glib-unix.h>
#include <gudev/gudev.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <libsoup/soup.h>
#include <unistd.h>

#include "fu-hwids.h"
#include "fu-pending.h"
#include "fu-plugin-private.h"
#include "fwupd-common-private.h"

#ifndef GUdevClient_autoptr
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GUdevClient, g_object_unref)
#endif

/* this is only valid in this file */
#define FWUPD_ERROR_INVALID_ARGS	(FWUPD_ERROR_LAST+1)

/* custom return code */
#define EXIT_NOTHING_TO_DO		2

typedef struct {
	GCancellable		*cancellable;
	GMainLoop		*loop;
	GOptionContext		*context;
	GPtrArray		*cmd_array;
	FwupdInstallFlags	 flags;
	FwupdClient		*client;
} FuUtilPrivate;

typedef gboolean (*FuUtilPrivateCb)	(FuUtilPrivate	*util,
					 gchar		**values,
					 GError		**error);

typedef struct {
	gchar		*name;
	gchar		*arguments;
	gchar		*description;
	FuUtilPrivateCb	 callback;
} FuUtilItem;

static void
fu_util_item_free (FuUtilItem *item)
{
	g_free (item->name);
	g_free (item->arguments);
	g_free (item->description);
	g_free (item);
}

/*
 * fu_sort_command_name_cb:
 */
static gint
fu_sort_command_name_cb (FuUtilItem **item1, FuUtilItem **item2)
{
	return g_strcmp0 ((*item1)->name, (*item2)->name);
}

static void
fu_util_add (GPtrArray *array,
	     const gchar *name,
	     const gchar *arguments,
	     const gchar *description,
	     FuUtilPrivateCb callback)
{
	g_auto(GStrv) names = NULL;

	g_return_if_fail (name != NULL);
	g_return_if_fail (description != NULL);
	g_return_if_fail (callback != NULL);

	/* add each one */
	names = g_strsplit (name, ",", -1);
	for (guint i = 0; names[i] != NULL; i++) {
		FuUtilItem *item = g_new0 (FuUtilItem, 1);
		item->name = g_strdup (names[i]);
		if (i == 0) {
			item->description = g_strdup (description);
		} else {
			/* TRANSLATORS: this is a command alias, e.g. 'get-devices' */
			item->description = g_strdup_printf (_("Alias to %s"),
							     names[0]);
		}
		item->arguments = g_strdup (arguments);
		item->callback = callback;
		g_ptr_array_add (array, item);
	}
}

static gchar *
fu_util_get_descriptions (GPtrArray *array)
{
	gsize len;
	const gsize max_len = 35;
	GString *string;

	/* print each command */
	string = g_string_new ("");
	for (guint i = 0; i < array->len; i++) {
		FuUtilItem *item = g_ptr_array_index (array, i);
		g_string_append (string, "  ");
		g_string_append (string, item->name);
		len = strlen (item->name) + 2;
		if (item->arguments != NULL) {
			g_string_append (string, " ");
			g_string_append (string, item->arguments);
			len += strlen (item->arguments) + 1;
		}
		if (len < max_len) {
			for (gsize j = len; j < max_len + 1; j++)
				g_string_append_c (string, ' ');
			g_string_append (string, item->description);
			g_string_append_c (string, '\n');
		} else {
			g_string_append_c (string, '\n');
			for (gsize j = 0; j < max_len + 1; j++)
				g_string_append_c (string, ' ');
			g_string_append (string, item->description);
			g_string_append_c (string, '\n');
		}
	}

	/* remove trailing newline */
	if (string->len > 0)
		g_string_set_size (string, string->len - 1);

	return g_string_free (string, FALSE);
}

static gboolean
fu_util_run (FuUtilPrivate *priv, const gchar *command, gchar **values, GError **error)
{
	/* find command */
	for (guint i = 0; i < priv->cmd_array->len; i++) {
		FuUtilItem *item = g_ptr_array_index (priv->cmd_array, i);
		if (g_strcmp0 (item->name, command) == 0)
			return item->callback (priv, values, error);
	}

	/* not found */
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_ARGS,
			     /* TRANSLATORS: error message */
			     _("Command not found"));
	return FALSE;
}

static const gchar *
fu_util_status_to_string (FwupdStatus status)
{
	switch (status) {
	case FWUPD_STATUS_IDLE:
		/* TRANSLATORS: daemon is inactive */
		return _("Idle…");
		break;
	case FWUPD_STATUS_DECOMPRESSING:
		/* TRANSLATORS: decompressing the firmware file */
		return _("Decompressing…");
		break;
	case FWUPD_STATUS_LOADING:
		/* TRANSLATORS: parsing the firmware information */
		return _("Loading…");
		break;
	case FWUPD_STATUS_DEVICE_RESTART:
		/* TRANSLATORS: restarting the device to pick up new F/W */
		return _("Restarting device…");
		break;
	case FWUPD_STATUS_DEVICE_WRITE:
		/* TRANSLATORS: writing to the flash chips */
		return _("Writing…");
		break;
	case FWUPD_STATUS_DEVICE_VERIFY:
		/* TRANSLATORS: verifying we wrote the firmware correctly */
		return _("Verifying…");
		break;
	case FWUPD_STATUS_SCHEDULING:
		/* TRANSLATORS: scheduing an update to be done on the next boot */
		return _("Scheduling…");
		break;
	case FWUPD_STATUS_DOWNLOADING:
		/* TRANSLATORS: downloading from a remote server */
		return _("Downloading…");
		break;
	default:
		break;
	}

	/* TRANSLATORS: currect daemon status is unknown */
	return _("Unknown");
}

static void
fu_util_display_percentage (FwupdStatus status, guint percentage)
{
	const gchar *title;
	const guint progressbar_len = 40;
	const guint title_len = 25;
	guint i;
	static guint to_erase = 0;
	g_autoptr(GString) str = g_string_new (NULL);

	/* erase previous line */
	for (i = 0; i < to_erase; i++)
		g_print ("\b");

	/* add status */
	if (status == FWUPD_STATUS_IDLE) {
		if (to_erase > 0)
			g_print ("\n");
		to_erase = 0;
		return;
	}
	title = fu_util_status_to_string (status);
	g_string_append (str, title);
	for (i = str->len; i < title_len; i++)
		g_string_append (str, " ");

	/* add progressbar */
	if (percentage > 0) {
		g_string_append (str, "[");
		for (i = 0; i < progressbar_len * percentage / 100; i++)
			g_string_append (str, "*");
		for (i = i + 1; i < progressbar_len; i++)
			g_string_append (str, " ");
		g_string_append (str, "]");
	}

	/* dump to screen */
	g_print ("%s", str->str);
	to_erase = str->len;
}

static void
fu_util_client_notify_cb (GObject *object,
			  GParamSpec *pspec,
			  FuUtilPrivate *priv)
{
	fu_util_display_percentage (fwupd_client_get_status (priv->client),
				    fwupd_client_get_percentage (priv->client));
}

static void
fu_util_print_data (const gchar *title, const gchar *msg)
{
	gsize title_len;
	g_auto(GStrv) lines = NULL;

	if (msg == NULL)
		return;
	g_print ("%s:", title);

	/* pad */
	title_len = strlen (title) + 1;
	lines = g_strsplit (msg, "\n", -1);
	for (guint j = 0; lines[j] != NULL; j++) {
		for (gsize i = title_len; i < 25; i++)
			g_print (" ");
		g_print ("%s\n", lines[j]);
		title_len = 0;
	}
}

static guint
fu_util_prompt_for_number (guint maxnum)
{
	gint retval;
	guint answer = 0;

	do {
		char buffer[64];

		/* swallow the \n at end of line too */
		if (!fgets (buffer, sizeof (buffer), stdin))
			break;
		if (strlen (buffer) == sizeof (buffer) - 1)
			continue;

		/* get a number */
		retval = sscanf (buffer, "%u", &answer);

		/* positive */
		if (retval == 1 && answer > 0 && answer <= maxnum)
			break;

		/* TRANSLATORS: the user isn't reading the question */
		g_print (_("Please enter a number from 1 to %u: "), maxnum);
	} while (TRUE);
	return answer;
}

static FwupdDevice *
fu_util_prompt_for_device (FuUtilPrivate *priv, GError **error)
{
	FwupdDevice *dev;
	guint idx;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) devices_filtered = NULL;

	/* get devices from daemon */
	devices = fwupd_client_get_devices_simple (priv->client, NULL, error);
	if (devices == NULL)
		return NULL;

	/* filter results */
	devices_filtered = g_ptr_array_new ();
	for (guint i = 0; i < devices->len; i++) {
		dev = g_ptr_array_index (devices, i);
		if (!fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_SUPPORTED))
			continue;
		g_ptr_array_add (devices_filtered, dev);
	}

	/* nothing */
	if (devices_filtered->len == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOTHING_TO_DO,
				     "No supported devices");
		return NULL;
	}

	/* exactly one */
	if (devices_filtered->len == 1) {
		dev = g_ptr_array_index (devices_filtered, 0);
		return g_object_ref (dev);
	}

	/* TRANSLATORS: get interactive prompt */
	g_print ("%s\n", _("Choose a device:"));
	for (guint i = 0; i < devices_filtered->len; i++) {
		dev = g_ptr_array_index (devices_filtered, i);
		g_print ("%u.\t%s (%s)\n",
			 i + 1,
			 fwupd_device_get_id (dev),
			 fwupd_device_get_name (dev));
	}
	idx = fu_util_prompt_for_number (devices_filtered->len);
	dev = g_ptr_array_index (devices_filtered, idx - 1);
	return g_object_ref (dev);
}

static gboolean
fu_util_get_devices (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GPtrArray) devs = NULL;

	/* get results from daemon */
	devs = fwupd_client_get_devices_simple (priv->client, NULL, error);
	if (devs == NULL)
		return FALSE;

	/* print */
	if (devs->len == 0) {
		/* TRANSLATORS: nothing attached that can be upgraded */
		g_print ("%s\n", _("No hardware detected with firmware update capability"));
		return TRUE;
	}

	for (guint i = 0; i < devs->len; i++) {
		g_autofree gchar *tmp = NULL;
		FwupdDevice *dev = g_ptr_array_index (devs, i);
		tmp = fwupd_device_to_string (dev);
		g_print ("%s\n", tmp);
	}

	return TRUE;
}

static gboolean
fu_util_install_with_fallback (FuUtilPrivate *priv, const gchar *id,
			       const gchar *filename, GError **error)
{
	g_autoptr(GError) error_local = NULL;

	/* install with flags chosen by the user */
	if (fwupd_client_install (priv->client, id, filename, priv->flags,
				  NULL, &error_local))
		return TRUE;

	/* some other failure */
	if ((priv->flags & FWUPD_INSTALL_FLAG_OFFLINE) > 0 ||
	    !g_error_matches (error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_propagate_error (error, error_local);
		error_local = NULL;
		return FALSE;
	}

	/* TRANSLATOR: the plugin only supports offline */
	g_print ("%s...\n", _("Retrying as an offline update"));
	priv->flags |= FWUPD_INSTALL_FLAG_OFFLINE;
	return fwupd_client_install (priv->client, id, filename, priv->flags,
				     NULL, error);
}

static gboolean
fu_util_install (FuUtilPrivate *priv, gchar **values, GError **error)
{
	const gchar *id;

	/* handle both forms */
	if (g_strv_length (values) == 1) {
		id = FWUPD_DEVICE_ID_ANY;
	} else if (g_strv_length (values) == 2) {
		id = values[1];
	} else {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments: expected 'filename' [id]");
		return FALSE;
	}

	/* install with flags chosen by the user then falling back to offline */
	return fu_util_install_with_fallback (priv, id, values[0], error);
}

static gboolean
fu_util_get_details (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GPtrArray) array = NULL;

	/* check args */
	if (g_strv_length (values) != 1) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments: expected 'filename'");
		return FALSE;
	}
	array = fwupd_client_get_details_local (priv->client, values[0], NULL, error);
	if (array == NULL)
		return FALSE;
	for (guint i = 0; i < array->len; i++) {
		FwupdResult *res = g_ptr_array_index (array, i);
		g_autofree gchar *tmp = NULL;
		tmp = fwupd_result_to_string (res);
		g_print ("%s", tmp);
	}
	return TRUE;
}

static void
fu_util_offline_update_reboot (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GDBusConnection) connection = NULL;
	g_autoptr(GVariant) val = NULL;

	connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
	if (connection == NULL)
		return;

#ifdef HAVE_SYSTEMD
	/* reboot using systemd */
	val = g_dbus_connection_call_sync (connection,
					   "org.freedesktop.systemd1",
					   "/org/freedesktop/systemd1",
					   "org.freedesktop.systemd1.Manager",
					   "Reboot",
					   NULL,
					   NULL,
					   G_DBUS_CALL_FLAGS_NONE,
					   -1,
					   NULL,
					   &error);
#elif HAVE_CONSOLEKIT
	/* reboot using ConsoleKit */
	val = g_dbus_connection_call_sync (connection,
					   "org.freedesktop.ConsoleKit",
					   "/org/freedesktop/ConsoleKit/Manager",
					   "org.freedesktop.ConsoleKit.Manager",
					   "Restart",
					   NULL,
					   NULL,
					   G_DBUS_CALL_FLAGS_NONE,
					   -1,
					   NULL,
					   &error);
#else
	g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "No supported backend compiled in to perform the operation.");
#endif

	if (val == NULL)
		g_print ("Failed to reboot: %s\n", error->message);
}

static gboolean
fu_util_install_prepared (FuUtilPrivate *priv, gchar **values, GError **error)
{
	gint vercmp;
	guint cnt = 0;
	g_autofree gchar *link = NULL;
	g_autoptr(GPtrArray) results = NULL;
	g_autoptr(FuPending) pending = NULL;

	/* verify this is pointing to our cache */
	link = g_file_read_link (FU_OFFLINE_TRIGGER_FILENAME, NULL);
	if (link == NULL) {
		g_debug ("No %s, exiting", FU_OFFLINE_TRIGGER_FILENAME);
		return TRUE;
	}
	if (g_strcmp0 (link, "/var/lib/fwupd") != 0) {
		g_debug ("Another framework set up the trigger, exiting");
		return TRUE;
	}

	/* do this first to avoid a loop if this tool segfaults */
	g_unlink (FU_OFFLINE_TRIGGER_FILENAME);

	if (g_strv_length (values) != 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments: none expected");
		return FALSE;
	}

	/* ensure root user */
	if (getuid () != 0 || geteuid () != 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "This function can only be used as root");
		return FALSE;
	}

	/* get prepared updates */
	pending = fu_pending_new ();
	results = fu_pending_get_devices (pending, error);
	if (results == NULL)
		return FALSE;

	/* apply each update */
	for (guint i = 0; i < results->len; i++) {
		FwupdResult *res = g_ptr_array_index (results, i);
		FwupdDevice *dev = fwupd_result_get_device (res);
		FwupdRelease *rel = fwupd_result_get_release (res);

		/* check not already done */
		if (fwupd_result_get_update_state (res) != FWUPD_UPDATE_STATE_PENDING)
			continue;

		/* tell the user what's going to happen */
		vercmp = as_utils_vercmp (fwupd_device_get_version (dev),
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
		if (!fwupd_client_install (priv->client,
					   fwupd_device_get_id (dev),
					   fwupd_release_get_filename (rel),
					   priv->flags,
					   NULL,
					   error))
			return FALSE;
		cnt++;
	}

	/* nothing to do */
	if (cnt == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOTHING_TO_DO,
				     "No updates prepared");
		return FALSE;
	}

	/* reboot */
	fu_util_offline_update_reboot ();

	g_print ("%s\n", _("Done!"));
	return TRUE;
}

static gboolean
fu_util_clear_results (FuUtilPrivate *priv, gchar **values, GError **error)
{
	if (g_strv_length (values) != 1) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments: expected 'id'");
		return FALSE;
	}
	return fwupd_client_clear_results (priv->client, values[0], NULL, error);
}

static gboolean
fu_util_verify_update_all (FuUtilPrivate *priv, GError **error)
{
	g_autoptr(GPtrArray) devs = NULL;

	/* get devices from daemon */
	devs = fwupd_client_get_devices_simple (priv->client, NULL, error);
	if (devs == NULL)
		return FALSE;

	/* get results */
	for (guint i = 0; i < devs->len; i++) {
		g_autoptr(GError) error_local = NULL;
		FwupdDevice *dev = g_ptr_array_index (devs, i);
		if (!fwupd_client_verify_update (priv->client,
						 fwupd_device_get_id (dev),
						 NULL,
						 &error_local)) {
			g_print ("%s\tFAILED: %s\n",
				 fwupd_device_get_guid_default (dev),
				 error_local->message);
			continue;
		}
		g_print ("%s\t%s\n",
			 fwupd_device_get_guid_default (dev),
			 _("OK"));
	}
	return TRUE;
}

static gboolean
fu_util_verify_update (FuUtilPrivate *priv, gchar **values, GError **error)
{
	if (g_strv_length (values) == 0)
		return fu_util_verify_update_all (priv, error);
	if (g_strv_length (values) != 1) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments: expected 'id'");
		return FALSE;
	}
	return fwupd_client_verify_update (priv->client, values[0], NULL, error);
}

static gboolean
fu_util_file_exists_with_checksum (const gchar *fn,
				   const gchar *checksum_expected,
				   GChecksumType checksum_type)
{
	gsize len = 0;
	g_autofree gchar *checksum_actual = NULL;
	g_autofree gchar *data = NULL;

	if (!g_file_get_contents (fn, &data, &len, NULL))
		return FALSE;
	checksum_actual = g_compute_checksum_for_data (checksum_type,
						       (guchar *) data, len);
	return g_strcmp0 (checksum_expected, checksum_actual) == 0;
}

static void
fu_util_download_chunk_cb (SoupMessage *msg, SoupBuffer *chunk, gpointer user_data)
{
	guint percentage;
	goffset header_size;
	goffset body_length;

	/* if it's returning "Found" or an error, ignore the percentage */
	if (msg->status_code != SOUP_STATUS_OK) {
		g_debug ("ignoring status code %u (%s)",
			 msg->status_code, msg->reason_phrase);
		return;
	}

	/* get data */
	body_length = msg->response_body->length;
	header_size = soup_message_headers_get_content_length (msg->response_headers);

	/* size is not known */
	if (header_size < body_length)
		return;

	/* calulate percentage */
	percentage = (guint) ((100 * body_length) / header_size);
	g_debug ("progress: %u%%", percentage);
	fu_util_display_percentage (FWUPD_STATUS_DOWNLOADING, percentage);
}

static gboolean
fu_util_download_file (FuUtilPrivate *priv,
		       SoupURI *uri,
		       const gchar *fn,
		       const gchar *checksum_expected,
		       GError **error)
{
	GChecksumType checksum_type;
	const gchar *http_proxy;
	guint status_code;
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *checksum_actual = NULL;
	g_autofree gchar *user_agent = NULL;
	g_autofree gchar *uri_str = NULL;
	g_autoptr(SoupMessage) msg = NULL;
	g_autoptr(SoupSession) session = NULL;

	/* check if the file already exists with the right checksum */
	checksum_type = fwupd_checksum_guess_kind (checksum_expected);
	if (fu_util_file_exists_with_checksum (fn, checksum_expected, checksum_type)) {
		g_debug ("skpping download as file already exists");
		return TRUE;
	}

	/* create the soup session */
	user_agent = g_strdup_printf ("%s/%s", PACKAGE_NAME, PACKAGE_VERSION);
	session = soup_session_new_with_options (SOUP_SESSION_USER_AGENT, user_agent,
						 SOUP_SESSION_TIMEOUT, 60,
						 NULL);
	if (session == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "%s: failed to setup networking");
		return FALSE;
	}

	/* set the proxy */
	http_proxy = g_getenv ("http_proxy");
	if (http_proxy != NULL) {
		g_autoptr(SoupURI) proxy_uri = soup_uri_new (http_proxy);
		if (proxy_uri == NULL) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "invalid proxy URI: %s", http_proxy);
			return FALSE;
		}
		g_object_set (session, SOUP_SESSION_PROXY_URI, proxy_uri, NULL);
	}

	/* this disables the double-compression of the firmware.xml.gz file */
	soup_session_remove_feature_by_type (session, SOUP_TYPE_CONTENT_DECODER);

	/* download data */
	uri_str = soup_uri_to_string (uri, FALSE);
	g_debug ("downloading %s to %s", uri_str, fn);
	msg = soup_message_new_from_uri (SOUP_METHOD_GET, uri);
	if (msg == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Failed to parse URI %s", uri_str);
		return FALSE;
	}
	if (g_str_has_suffix (uri_str, ".asc")) {
		/* TRANSLATORS: downloading new signing file */
		g_print ("%s %s\n", _("Fetching signature"), uri_str);
	} else if (g_str_has_suffix (uri_str, ".gz")) {
		/* TRANSLATORS: downloading new metadata file */
		g_print ("%s %s\n", _("Fetching metadata"), uri_str);
	} else if (g_str_has_suffix (uri_str, ".cab")) {
		/* TRANSLATORS: downloading new firmware file */
		g_print ("%s %s\n", _("Fetching firmware"), uri_str);
	} else {
		/* TRANSLATORS: downloading unknown file */
		g_print ("%s %s\n", _("Fetching file"), uri_str);
	}
	g_signal_connect (msg, "got-chunk",
			  G_CALLBACK (fu_util_download_chunk_cb), priv);
	status_code = soup_session_send_message (session, msg);
	g_print ("\n");
	if (status_code != SOUP_STATUS_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Failed to download %s: %s",
			     uri_str, soup_status_get_phrase (status_code));
		return FALSE;
	}

	/* verify checksum */
	if (checksum_expected != NULL) {
		checksum_actual = g_compute_checksum_for_data (checksum_type,
							       (guchar *) msg->response_body->data,
							       (gsize) msg->response_body->length);
		if (g_strcmp0 (checksum_expected, checksum_actual) != 0) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "Checksum invalid, expected %s got %s",
				     checksum_expected, checksum_actual);
			return FALSE;
		}
	}

	/* save file */
	if (!g_file_set_contents (fn,
				  msg->response_body->data,
				  msg->response_body->length,
				  &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "Failed to save file: %s",
			     error_local->message);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_util_mkdir_with_parents (const gchar *path, GError **error)
{
	g_autoptr(GFile) file = g_file_new_for_path (path);
	if (g_file_query_exists (file, NULL))
		return TRUE;
	return g_file_make_directory_with_parents (file, NULL, error);
}

static gboolean
fu_util_download_metadata_for_remote (FuUtilPrivate *priv,
				      FwupdRemote *remote,
				      GError **error)
{
	g_autofree gchar *cache_dir = NULL;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *filename_asc = NULL;

	/* ensure cache directory exists */
	cache_dir = g_build_filename (g_get_user_cache_dir (), "fwupdmgr", NULL);
	if (!fu_util_mkdir_with_parents (cache_dir, error))
		return FALSE;

	/* download the metadata */
	filename = g_build_filename (cache_dir, fwupd_remote_get_filename (remote), NULL);
	if (!fu_util_download_file (priv, fwupd_remote_get_uri (remote),
				    filename, NULL, error))
		return FALSE;

	/* download the signature */
	filename_asc = g_build_filename (cache_dir, fwupd_remote_get_filename_asc (remote), NULL);
	if (!fu_util_download_file (priv, fwupd_remote_get_uri_asc (remote),
				    filename_asc, NULL, error))
		return FALSE;

	/* send all this to fwupd */
	return fwupd_client_update_metadata_with_id (priv->client,
						     fwupd_remote_get_id (remote),
						     filename,
						     filename_asc,
						     NULL, error);
}

static gboolean
fu_util_download_metadata (FuUtilPrivate *priv, GError **error)
{
	g_autoptr(GPtrArray) remotes = NULL;
	remotes = fwupd_client_get_remotes (priv->client, NULL, error);
	if (remotes == NULL)
		return FALSE;
	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote = g_ptr_array_index (remotes, i);
		if (!fwupd_remote_get_enabled (remote))
			continue;
		if (fwupd_remote_get_kind (remote) != FWUPD_REMOTE_KIND_HTTP)
			continue;
		if (!fu_util_download_metadata_for_remote (priv, remote, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_util_refresh (FuUtilPrivate *priv, gchar **values, GError **error)
{
	if (g_strv_length (values) == 0)
		return fu_util_download_metadata (priv, error);
	if (g_strv_length (values) != 3) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments: expected 'filename.xml' 'filename.xml.asc' 'remote-id'");
		return FALSE;
	}

	/* open file */
	return fwupd_client_update_metadata_with_id (priv->client,
						     values[2],
						     values[0],
						     values[1],
						     NULL,
						     error);
}

static gboolean
fu_util_get_results (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autofree gchar *tmp = NULL;
	g_autoptr(FwupdResult) res = NULL;

	if (g_strv_length (values) != 1) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments: expected 'DeviceID'");
		return FALSE;
	}
	res = fwupd_client_get_results (priv->client, values[0], NULL, error);
	if (res == NULL)
		return FALSE;
	tmp = fwupd_result_to_string (res);
	g_print ("%s", tmp);
	return TRUE;
}

static gboolean
fu_util_get_releases (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FwupdDevice) dev = NULL;
	g_autoptr(GPtrArray) rels = NULL;

	/* get device to use */
	if (g_strv_length (values) == 1) {
		dev = fwupd_client_get_device_by_id (priv->client, values[0],
						     NULL, error);
		if (dev == NULL)
			return FALSE;
	} else {
		dev = fu_util_prompt_for_device (priv, error);
		if (dev == NULL)
			return FALSE;
	}

	/* get the releases for this device */
	rels = fwupd_client_get_releases (priv->client, fwupd_device_get_id (dev), NULL, error);
	if (rels == NULL)
		return FALSE;
	g_print ("%s:\n", fwupd_device_get_name (dev));
	for (guint i = 0; i < rels->len; i++) {
		FwupdRelease *rel = g_ptr_array_index (rels, i);
		GPtrArray *checksums;
		const gchar *tmp;

		/* TRANSLATORS: section header for release version number */
		fu_util_print_data (_("Version"), fwupd_release_get_version (rel));

		/* TRANSLATORS: section header for firmware URI */
		fu_util_print_data (_("URI"), fwupd_release_get_uri (rel));
		tmp = fwupd_release_get_description (rel);
		if (tmp != NULL) {
			g_autofree gchar *desc = NULL;
			desc = as_markup_convert_simple (tmp, NULL);
			/* TRANSLATORS: section header for firmware description */
			fu_util_print_data (_("Description"), desc);
		}
		checksums = fwupd_release_get_checksums (rel);
		for (guint j = 0; j < checksums->len; j++) {
			const gchar *checksum = g_ptr_array_index (checksums, j);
			g_autofree gchar *checksum_display = NULL;
			checksum_display = fwupd_checksum_format_for_display (checksum);
			/* TRANSLATORS: section header for firmware checksum */
			fu_util_print_data (_("Checksum"), checksum_display);
		}

		/* new line between all but last entries */
		if (i != rels->len - 1)
			g_print ("\n");
	}
	return TRUE;
}

static FwupdRelease *
fu_util_prompt_for_release (FuUtilPrivate *priv, GPtrArray *rels, GError **error)
{
	FwupdRelease *rel;
	guint idx;

	/* nothing */
	if (rels->len == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOTHING_TO_DO,
				     "No supported releases");
		return NULL;
	}

	/* exactly one */
	if (rels->len == 1) {
		rel = g_ptr_array_index (rels, 0);
		return g_object_ref (rel);
	}

	/* TRANSLATORS: get interactive prompt */
	g_print ("%s\n", _("Choose a release:"));
	for (guint i = 0; i < rels->len; i++) {
		rel = g_ptr_array_index (rels, i);
		g_print ("%u.\t%s (%s)\n",
			 i + 1,
			 fwupd_release_get_version (rel),
			 fwupd_release_get_description (rel));
	}
	idx = fu_util_prompt_for_number (rels->len);
	rel = g_ptr_array_index (rels, idx - 1);
	return g_object_ref (rel);
}

static gboolean
fu_util_verify_all (FuUtilPrivate *priv, GError **error)
{
	g_autoptr(GPtrArray) devs = NULL;

	/* get devices from daemon */
	devs = fwupd_client_get_devices_simple (priv->client, NULL, error);
	if (devs == NULL)
		return FALSE;

	/* get results */
	for (guint i = 0; i < devs->len; i++) {
		g_autoptr(GError) error_local = NULL;
		FwupdDevice *dev = g_ptr_array_index (devs, i);
		if (!fwupd_client_verify (priv->client,
					  fwupd_device_get_id (dev),
					  NULL,
					  &error_local)) {
			g_print ("%s\tFAILED: %s\n",
				 fwupd_device_get_guid_default (dev),
				 error_local->message);
			continue;
		}
		g_print ("%s\t%s\n",
			 fwupd_device_get_guid_default (dev),
			 _("OK"));
	}
	return TRUE;
}

static gboolean
fu_util_verify (FuUtilPrivate *priv, gchar **values, GError **error)
{
	if (g_strv_length (values) == 0)
		return fu_util_verify_all (priv, error);
	if (g_strv_length (values) != 1) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments: expected 'id'");
		return FALSE;
	}
	return fwupd_client_verify (priv->client, values[0], NULL, error);
}

static gboolean
fu_util_unlock (FuUtilPrivate *priv, gchar **values, GError **error)
{
	if (g_strv_length (values) != 1) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments: expected 'id'");
		return FALSE;
	}
	return fwupd_client_unlock (priv->client, values[0], NULL, error);
}

static gboolean
fu_util_get_updates (FuUtilPrivate *priv, gchar **values, GError **error)
{
	GPtrArray *results = NULL;
	GPtrArray *guids;
	const gchar *tmp;

	/* print any updates */
	results = fwupd_client_get_updates (priv->client, NULL, error);
	if (results == NULL)
		return FALSE;
	for (guint i = 0; i < results->len; i++) {
		FwupdResult *res = g_ptr_array_index (results, i);
		FwupdDevice *dev = fwupd_result_get_device (res);
		FwupdRelease *rel = fwupd_result_get_release (res);
		GPtrArray *checksums;

		/* TRANSLATORS: first replacement is device name */
		g_print (_("%s has firmware updates:"), fwupd_device_get_name (dev));
		g_print ("\n");

		/* TRANSLATORS: Appstream ID for the hardware type */
		fu_util_print_data (_("ID"), fwupd_release_get_appstream_id (rel));

		/* TRANSLATORS: a GUID for the hardware */
		guids = fwupd_device_get_guids (dev);
		for (guint j = 0; j < guids->len; j++) {
			tmp = g_ptr_array_index (guids, j);
			fu_util_print_data (_("GUID"), tmp);
		}

		/* TRANSLATORS: section header for firmware version */
		fu_util_print_data (_("Update Version"),
				    fwupd_release_get_version (rel));

		/* TRANSLATORS: section header for remote ID, e.g. lvfs-testing */
		fu_util_print_data (_("Update Remote ID"),
				    fwupd_release_get_remote_id (rel));

		checksums = fwupd_release_get_checksums (rel);
		for (guint j = 0; j < checksums->len; j++) {
			const gchar *checksum = g_ptr_array_index (checksums, j);
			g_autofree gchar *checksum_display = NULL;
			checksum_display = fwupd_checksum_format_for_display (checksum);
			/* TRANSLATORS: section header for firmware checksum */
			fu_util_print_data (_("Update Checksum"), checksum_display);
		}

		/* TRANSLATORS: section header for firmware remote http:// */
		fu_util_print_data (_("Update Location"), fwupd_release_get_uri (rel));

		/* convert XML -> text */
		tmp = fwupd_release_get_description (rel);
		if (tmp != NULL) {
			g_autofree gchar *md = NULL;
			md = as_markup_convert (tmp,
						AS_MARKUP_CONVERT_FORMAT_SIMPLE,
						NULL);
			if (md != NULL) {
				/* TRANSLATORS: section header for long firmware desc */
				fu_util_print_data (_("Update Description"), md);
			}
		}
	}

	return TRUE;
}

static gboolean
fu_util_get_remotes (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GPtrArray) remotes = NULL;

	/* print any updates */
	remotes = fwupd_client_get_remotes (priv->client, NULL, error);
	if (remotes == NULL)
		return FALSE;
	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote = g_ptr_array_index (remotes, i);
		FwupdRemoteKind kind = fwupd_remote_get_kind (remote);
		SoupURI *uri;
		const gchar *tmp;
		gint priority;
		gdouble age;

		/* TRANSLATORS: remote identifier, e.g. lvfs-testing */
		fu_util_print_data (_("Remote ID"),
				    fwupd_remote_get_id (remote));

		/* TRANSLATORS: remote type, e.g. remote or local */
		fu_util_print_data (_("Type"),
				    fwupd_remote_kind_to_string (kind));

		/* TRANSLATORS: if the remote is enabled */
		fu_util_print_data (_("Enabled"),
				    fwupd_remote_get_enabled (remote) ? "True" : "False");

		/* optional parameters */
		age = fwupd_remote_get_age (remote);
		if (kind == FWUPD_REMOTE_KIND_HTTP &&
		    age > 0 && age != G_MAXUINT64) {
			const gchar *unit = "s";
			g_autofree gchar *age_str = NULL;
			if (age > 60) {
				age /= 60.f;
				unit = "m";
			}
			if (age > 60) {
				age /= 60.f;
				unit = "h";
			}
			if (age > 24) {
				age /= 24.f;
				unit = "d";
			}
			if (age > 7) {
				age /= 7.f;
				unit = "w";
			}
			age_str = g_strdup_printf ("%.2f%s", age, unit);
			/* TRANSLATORS: the age of the metadata */
			fu_util_print_data (_("Age"), age_str);
		}
		priority = fwupd_remote_get_priority (remote);
		if (priority != 0) {
			g_autofree gchar *priority_str = NULL;
			priority_str = g_strdup_printf ("%i", priority);
			/* TRANSLATORS: the numeric priority */
			fu_util_print_data (_("Priority"), priority_str);
		}
		tmp = fwupd_remote_get_username (remote);
		if (tmp != NULL) {
			/* TRANSLATORS: remote filename base */
			fu_util_print_data (_("Username"), tmp);
		}
		tmp = fwupd_remote_get_password (remote);
		if (tmp != NULL) {
			/* TRANSLATORS: remote filename base */
			fu_util_print_data (_("Password"), tmp);
		}
		tmp = fwupd_remote_get_filename (remote);
		if (tmp != NULL) {
			/* TRANSLATORS: remote filename base */
			fu_util_print_data (_("Filename"), tmp);
		}
		tmp = fwupd_remote_get_filename_asc (remote);
		if (tmp != NULL) {
			/* TRANSLATORS: remote filename base */
			fu_util_print_data (_("Filename Signature"), tmp);
		}
		tmp = fwupd_remote_get_filename_cache (remote);
		if (tmp != NULL) {
			/* TRANSLATORS: locatation of the local file */
			fu_util_print_data (_("Location"), tmp);
		}
		uri = fwupd_remote_get_uri (remote);
		if (uri != NULL) {
			g_autofree gchar *uri_str = soup_uri_to_string (uri, FALSE);
			/* TRANSLATORS: remote URI */
			fu_util_print_data (_("URL"), uri_str);
		}
		uri = fwupd_remote_get_uri_asc (remote);
		if (uri != NULL) {
			g_autofree gchar *uri_str = soup_uri_to_string (uri, FALSE);
			/* TRANSLATORS: remote URI */
			fu_util_print_data (_("URI Signature"), uri_str);
		}

		/* newline */
		if (i != remotes->len - 1)
			g_print ("\n");
	}

	return TRUE;
}

static void
fu_util_cancelled_cb (GCancellable *cancellable, gpointer user_data)
{
	FuUtilPrivate *priv = (FuUtilPrivate *) user_data;
	/* TRANSLATORS: this is when a device ctrl+c's a watch */
	g_print ("%s\n", _("Cancelled"));
	g_main_loop_quit (priv->loop);
}

static void
fu_util_device_added_cb (FwupdClient *client,
			 FwupdResult *device,
			 gpointer user_data)
{
	g_autofree gchar *tmp = fwupd_result_to_string (device);
	/* TRANSLATORS: this is when a device is hotplugged */
	g_print ("%s\n%s", _("Device added:"), tmp);
}

static void
fu_util_device_removed_cb (FwupdClient *client,
			   FwupdResult *device,
			   gpointer user_data)
{
	g_autofree gchar *tmp = fwupd_result_to_string (device);
	/* TRANSLATORS: this is when a device is hotplugged */
	g_print ("%s\n%s", _("Device removed:"), tmp);
}

static void
fu_util_device_changed_cb (FwupdClient *client,
			   FwupdResult *device,
			   gpointer user_data)
{
	g_autofree gchar *tmp = fwupd_result_to_string (device);
	/* TRANSLATORS: this is when a device has been updated */
	g_print ("%s\n%s", _("Device changed:"), tmp);
}

static void
fu_util_changed_cb (FwupdClient *client, gpointer user_data)
{
	/* TRANSLATORS: this is when the daemon state changes */
	g_print ("%s\n", _("Changed"));
}

static gboolean
fu_util_monitor (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FwupdClient) client = NULL;

	/* get all the DFU devices */
	client = fwupd_client_new ();
	if (!fwupd_client_connect (client, priv->cancellable, error))
		return FALSE;

	/* watch for any hotplugged device */
	g_signal_connect (client, "changed",
			  G_CALLBACK (fu_util_changed_cb), priv);
	g_signal_connect (client, "device-added",
			  G_CALLBACK (fu_util_device_added_cb), priv);
	g_signal_connect (client, "device-removed",
			  G_CALLBACK (fu_util_device_removed_cb), priv);
	g_signal_connect (client, "device-changed",
			  G_CALLBACK (fu_util_device_changed_cb), priv);
	g_signal_connect (priv->cancellable, "cancelled",
			  G_CALLBACK (fu_util_cancelled_cb), priv);
	g_main_loop_run (priv->loop);
	return TRUE;
}

static gboolean
fu_util_update_device_with_release (FuUtilPrivate *priv,
				    FwupdDevice *dev,
				    FwupdRelease *rel,
				    GError **error)
{
	GPtrArray *checksums;
	const gchar *remote_id;
	const gchar *uri_tmp;
	g_autofree gchar *basename = NULL;
	g_autofree gchar *cache_dir = NULL;
	g_autofree gchar *fn = NULL;
	g_autoptr(SoupURI) uri = NULL;

	/* work out what remote-specific URI fields this should use */
	uri_tmp = fwupd_release_get_uri (rel);
	remote_id = fwupd_release_get_remote_id (rel);
	if (remote_id != NULL) {
		g_autoptr(FwupdRemote) remote = NULL;
		remote = fwupd_client_get_remote_by_id (priv->client,
							remote_id,
							NULL,
							error);
		if (remote == NULL)
			return FALSE;

		/* local remotes have the firmware already */
		if (fwupd_remote_get_kind (remote) == FWUPD_REMOTE_KIND_LOCAL) {
			const gchar *fn_cache = fwupd_remote_get_filename_cache (remote);
			g_autofree gchar *path = g_path_get_dirname (fn_cache);
			fn = g_build_filename (path, uri_tmp, NULL);
			return fu_util_install_with_fallback (priv,
							      fwupd_device_get_id (dev),
							      fn, error);
		}

		uri = fwupd_remote_build_uri (remote, uri_tmp, error);
		if (uri == NULL)
			return FALSE;
	} else {
		uri = soup_uri_new (uri_tmp);
	}

	/* ensure cache directory exists */
	cache_dir = g_build_filename (g_get_user_cache_dir (), "fwupdmgr", NULL);
	if (!fu_util_mkdir_with_parents (cache_dir, error))
		return FALSE;

	/* download file */
	g_print ("Downloading %s for %s...\n",
		 fwupd_release_get_version (rel),
		 fwupd_device_get_name (dev));
	basename = g_path_get_basename (uri_tmp);
	fn = g_build_filename (cache_dir, basename, NULL);
	checksums = fwupd_release_get_checksums (rel);
	if (!fu_util_download_file (priv, uri, fn,
				    fwupd_checksum_get_best (checksums),
				    error))
		return FALSE;
	g_print ("Updating %s on %s...\n",
		 fwupd_release_get_version (rel),
		 fwupd_device_get_name (dev));
	return fu_util_install_with_fallback (priv, fwupd_device_get_id (dev), fn, error);
}

static gboolean
fu_util_update (FuUtilPrivate *priv, gchar **values, GError **error)
{
	GPtrArray *results = NULL;

	/* apply any updates */
	results = fwupd_client_get_updates (priv->client, NULL, error);
	if (results == NULL)
		return FALSE;
	for (guint i = 0; i < results->len; i++) {
		FwupdResult *res = g_ptr_array_index (results, i);
		FwupdDevice *dev = fwupd_result_get_device (res);
		FwupdRelease *rel = fwupd_result_get_release (res);
		GPtrArray *checksums = fwupd_release_get_checksums (rel);
		if (checksums->len == 0)
			continue;
		if (fwupd_release_get_uri (rel) == NULL)
			continue;
		if (!fu_util_update_device_with_release (priv, dev, rel, error))
			return FALSE;
	}

	return TRUE;
}

static gboolean
fu_util_downgrade (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FwupdDevice) dev = NULL;
	g_autoptr(FwupdRelease) rel = NULL;
	g_autoptr(GPtrArray) rels = NULL;
	g_autoptr(GPtrArray) rels_filtered = NULL;

	/* get device to use */
	if (g_strv_length (values) == 1) {
		dev = fwupd_client_get_device_by_id (priv->client, values[1],
						     NULL, error);
		if (dev == NULL)
			return FALSE;
	} else {
		dev = fu_util_prompt_for_device (priv, error);
		if (dev == NULL)
			return FALSE;
	}

	/* get the releases for this device and filter for validity */
	rels = fwupd_client_get_releases (priv->client,
					  fwupd_device_get_id (dev),
					  NULL, error);
	if (rels == NULL)
		return FALSE;
	rels_filtered = g_ptr_array_new ();
	for (guint i = 0; i < rels->len; i++) {
		FwupdRelease *rel_tmp = g_ptr_array_index (rels, i);

		/* only include older firmware */
		if (as_utils_vercmp (fwupd_release_get_version (rel_tmp),
				     fwupd_device_get_version (dev)) >= 0) {
			g_debug ("ignoring %s as older than %s",
				 fwupd_release_get_version (rel_tmp),
				 fwupd_device_get_version (dev));
			continue;
		}

		/* don't show releases we are not allowed to dowgrade to */
		if (fwupd_device_get_version_lowest (dev) != NULL) {
			if (as_utils_vercmp (fwupd_release_get_version (rel_tmp),
					     fwupd_device_get_version_lowest (dev)) <= 0) {
				g_debug ("ignoring %s as older than lowest %s",
					 fwupd_release_get_version (rel_tmp),
					 fwupd_device_get_version_lowest (dev));
				continue;
			}
		}

		/* don't show releases without URIs */
		if (fwupd_release_get_uri (rel_tmp) == NULL) {
			g_debug ("ignoring %s as no URI",
				 fwupd_release_get_version (rel_tmp));
			continue;
		}

		g_ptr_array_add (rels_filtered, rel_tmp);
	}

	/* get the chosen release */
	rel = fu_util_prompt_for_release (priv, rels_filtered, error);
	if (rel == NULL)
		return FALSE;
	priv->flags |= FWUPD_INSTALL_FLAG_ALLOW_OLDER;
	return fu_util_update_device_with_release (priv, dev, rel, error);
}

static gboolean
fu_util_hwids (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FuHwids) hwids = fu_hwids_new ();
	const gchar *hwid_keys[] = {
		FU_HWIDS_KEY_BIOS_VENDOR,
		FU_HWIDS_KEY_BIOS_VERSION,
		FU_HWIDS_KEY_BIOS_MAJOR_RELEASE,
		FU_HWIDS_KEY_BIOS_MINOR_RELEASE,
		FU_HWIDS_KEY_MANUFACTURER,
		FU_HWIDS_KEY_FAMILY,
		FU_HWIDS_KEY_PRODUCT_NAME,
		FU_HWIDS_KEY_PRODUCT_SKU,
		FU_HWIDS_KEY_ENCLOSURE_KIND,
		FU_HWIDS_KEY_BASEBOARD_MANUFACTURER,
		FU_HWIDS_KEY_BASEBOARD_PRODUCT,
		NULL };

	/* read files in /sys */
	if (!fu_hwids_setup (hwids, g_getenv ("SYSFSPATH"), error))
		return FALSE;

	/* show debug output */
	g_print ("Computer Information\n");
	g_print ("--------------------\n");
	for (guint i = 0; hwid_keys[i] != NULL; i++) {
		const gchar *tmp = fu_hwids_get_value (hwids, hwid_keys[i]);
		if (tmp == NULL)
			continue;
		g_print ("%s: %s\n", hwid_keys[i], tmp);
	}

	/* show GUIDs */
	g_print ("\nHardware IDs\n");
	g_print ("------------\n");
	for (guint i = 0; i < 15; i++) {
		const gchar *keys = NULL;
		g_autofree gchar *guid = NULL;
		g_autofree gchar *key = NULL;
		g_autofree gchar *keys_str = NULL;
		g_auto(GStrv) keysv = NULL;
		g_autoptr(GError) error_local = NULL;

		/* get the GUID */
		key = g_strdup_printf ("HardwareID-%u", i);
		keys = fu_hwids_get_replace_keys (hwids, key);
		guid = fu_hwids_get_guid (hwids, key, &error_local);
		if (guid == NULL) {
			g_print ("%s\n", error_local->message);
			continue;
		}

		/* show what makes up the GUID */
		keysv = g_strsplit (keys, "&", -1);
		keys_str = g_strjoinv (" + ", keysv);
		g_print ("{%s}   <- %s\n", guid, keys_str);
	}

	return TRUE;
}

static void
fu_util_ignore_cb (const gchar *log_domain, GLogLevelFlags log_level,
		   const gchar *message, gpointer user_data)
{
}

static gboolean
fu_util_sigint_cb (gpointer user_data)
{
	FuUtilPrivate *priv = (FuUtilPrivate *) user_data;
	g_debug ("Handling SIGINT");
	g_cancellable_cancel (priv->cancellable);
	return FALSE;
}

static void
fu_util_private_free (FuUtilPrivate *priv)
{
	if (priv->cmd_array != NULL)
		g_ptr_array_unref (priv->cmd_array);
	if (priv->client != NULL)
		g_object_unref (priv->client);
	g_main_loop_unref (priv->loop);
	g_object_unref (priv->cancellable);
	g_option_context_free (priv->context);
	g_free (priv);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuUtilPrivate, fu_util_private_free)

int
main (int argc, char *argv[])
{
	gboolean force = FALSE;
	gboolean allow_older = FALSE;
	gboolean allow_reinstall = FALSE;
	gboolean offline = FALSE;
	gboolean ret;
	gboolean verbose = FALSE;
	gboolean version = FALSE;
	g_autoptr(FuUtilPrivate) priv = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *cmd_descriptions = NULL;
	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			/* TRANSLATORS: command line option */
			_("Show extra debugging information"), NULL },
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &version,
			/* TRANSLATORS: command line option */
			_("Show client and daemon versions"), NULL },
		{ "offline", '\0', 0, G_OPTION_ARG_NONE, &offline,
			/* TRANSLATORS: command line option */
			_("Schedule installation for next reboot when possible"), NULL },
		{ "allow-reinstall", '\0', 0, G_OPTION_ARG_NONE, &allow_reinstall,
			/* TRANSLATORS: command line option */
			_("Allow re-installing existing firmware versions"), NULL },
		{ "allow-older", '\0', 0, G_OPTION_ARG_NONE, &allow_older,
			/* TRANSLATORS: command line option */
			_("Allow downgrading firmware versions"), NULL },
		{ "force", '\0', 0, G_OPTION_ARG_NONE, &force,
			/* TRANSLATORS: command line option */
			_("Override plugin warning"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* ensure D-Bus errors are registered */
	fwupd_error_quark ();

	/* create helper object */
	priv = g_new0 (FuUtilPrivate, 1);
	priv->loop = g_main_loop_new (NULL, FALSE);

	/* add commands */
	priv->cmd_array = g_ptr_array_new_with_free_func ((GDestroyNotify) fu_util_item_free);
	fu_util_add (priv->cmd_array,
		     "get-devices",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Get all devices that support firmware updates"),
		     fu_util_get_devices);
#if AS_CHECK_VERSION(0,6,13)
	fu_util_add (priv->cmd_array,
		     "hwids",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Return all the hardware IDs for the machine"),
		     fu_util_hwids);
#endif
	fu_util_add (priv->cmd_array,
		     "install-prepared",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Install prepared updates now"),
		     fu_util_install_prepared);
	fu_util_add (priv->cmd_array,
		     "install",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Install a firmware file on this hardware"),
		     fu_util_install);
	fu_util_add (priv->cmd_array,
		     "get-details",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Gets details about a firmware file"),
		     fu_util_get_details);
	fu_util_add (priv->cmd_array,
		     "get-updates",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Gets the list of updates for connected hardware"),
		     fu_util_get_updates);
	fu_util_add (priv->cmd_array,
		     "update",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Updates all firmware to latest versions available"),
		     fu_util_update);
	fu_util_add (priv->cmd_array,
		     "verify",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Gets the cryptographic hash of the dumped firmware"),
		     fu_util_verify);
	fu_util_add (priv->cmd_array,
		     "unlock",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Unlocks the device for firmware access"),
		     fu_util_unlock);
	fu_util_add (priv->cmd_array,
		     "clear-results",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Clears the results from the last update"),
		     fu_util_clear_results);
	fu_util_add (priv->cmd_array,
		     "get-results",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Gets the results from the last update"),
		     fu_util_get_results);
	fu_util_add (priv->cmd_array,
		     "get-releases",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Gets the releases for a device"),
		     fu_util_get_releases);
	fu_util_add (priv->cmd_array,
		     "get-remotes",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Gets the configured remotes"),
		     fu_util_get_remotes);
	fu_util_add (priv->cmd_array,
		     "downgrade",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Downgrades the firmware on a device"),
		     fu_util_downgrade);
	fu_util_add (priv->cmd_array,
		     "refresh",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Refresh metadata from remote server"),
		     fu_util_refresh);
	fu_util_add (priv->cmd_array,
		     "verify-update",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Update the stored metadata with current ROM contents"),
		     fu_util_verify_update);
	fu_util_add (priv->cmd_array,
		     "monitor",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Monitor the daemon for events"),
		     fu_util_monitor);

	/* do stuff on ctrl+c */
	priv->cancellable = g_cancellable_new ();
	g_unix_signal_add_full (G_PRIORITY_DEFAULT,
				SIGINT, fu_util_sigint_cb,
				priv, NULL);

	/* sort by command name */
	g_ptr_array_sort (priv->cmd_array,
			  (GCompareFunc) fu_sort_command_name_cb);

	/* get a list of the commands */
	priv->context = g_option_context_new (NULL);
	cmd_descriptions = fu_util_get_descriptions (priv->cmd_array);
	g_option_context_set_summary (priv->context, cmd_descriptions);

	/* TRANSLATORS: program name */
	g_set_application_name (_("Firmware Utility"));
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

	/* set flags */
	if (offline)
		priv->flags |= FWUPD_INSTALL_FLAG_OFFLINE;
	if (allow_reinstall)
		priv->flags |= FWUPD_INSTALL_FLAG_ALLOW_REINSTALL;
	if (allow_older)
		priv->flags |= FWUPD_INSTALL_FLAG_ALLOW_OLDER;
	if (force)
		priv->flags |= FWUPD_INSTALL_FLAG_FORCE;

	/* connect to the daemon */
	priv->client = fwupd_client_new ();
	g_signal_connect (priv->client, "notify::percentage",
			  G_CALLBACK (fu_util_client_notify_cb), priv);
	g_signal_connect (priv->client, "notify::status",
			  G_CALLBACK (fu_util_client_notify_cb), priv);

	/* just show versions and exit */
	if (version) {
		g_print ("client version:\t%i.%i.%i\n",
			 FWUPD_MAJOR_VERSION,
			 FWUPD_MINOR_VERSION,
			 FWUPD_MICRO_VERSION);
		if (!fwupd_client_connect (priv->client, priv->cancellable, &error)) {
			g_printerr ("Failed to connect to daemon: %s\n",
				    error->message);
			return EXIT_FAILURE;
		}
		g_print ("daemon version:\t%s\n",
			 fwupd_client_get_daemon_version (priv->client));
#ifdef FWUPD_GIT_DESCRIBE
		g_print ("checkout info:\t%s\n", FWUPD_GIT_DESCRIBE);
#endif
		g_print ("compile-time dependency versions\n");
		g_print ("\tappstream-glib:\t%d.%d.%d\n",
			AS_MAJOR_VERSION,
			AS_MINOR_VERSION,
			AS_MICRO_VERSION);
		g_print ("\tgusb:\t%d.%d.%d\n",
			G_USB_MAJOR_VERSION,
			G_USB_MINOR_VERSION,
			G_USB_MICRO_VERSION);
		return EXIT_SUCCESS;
	}

	/* run the specified command */
	ret = fu_util_run (priv, argv[1], (gchar**) &argv[2], &error);
	if (!ret) {
		if (g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_INVALID_ARGS)) {
			g_autofree gchar *tmp = NULL;
			tmp = g_option_context_get_help (priv->context, TRUE, NULL);
			g_print ("%s\n\n%s", error->message, tmp);
			return EXIT_FAILURE;
		}
		if (g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO)) {
			g_print ("%s\n", error->message);
			return EXIT_NOTHING_TO_DO;
		}
		g_print ("%s\n", error->message);
		return EXIT_FAILURE;
	}

	/* success */
	return EXIT_SUCCESS;
}
