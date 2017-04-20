/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
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

#include "fu-pending.h"
#include "fu-plugin-private.h"

#ifndef GUdevClient_autoptr
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GUdevClient, g_object_unref)
#endif

/* this is only valid in this file */
#define FWUPD_ERROR_INVALID_ARGS	(FWUPD_ERROR_LAST+1)

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
	default:
		break;
	}

	/* TRANSLATORS: currect daemon status is unknown */
	return _("Unknown");
}

static void
fu_util_display_panel (FuUtilPrivate *priv)
{
	FwupdStatus status;
	const gchar *title;
	const guint progressbar_len = 40;
	const guint title_len = 25;
	guint i;
	guint percentage;
	static guint to_erase = 0;
	g_autoptr(GString) str = g_string_new (NULL);

	/* erase previous line */
	for (i = 0; i < to_erase; i++)
		g_print ("\b");

	/* add status */
	status = fwupd_client_get_status (priv->client);
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
	percentage = fwupd_client_get_percentage (priv->client);
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
	fu_util_display_panel (priv);
}

static gboolean
fu_util_get_devices (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GPtrArray) results = NULL;

	/* get results from daemon */
	results = fwupd_client_get_devices (priv->client, NULL, error);
	if (results == NULL)
		return FALSE;

	/* print */
	if (results->len == 0) {
		/* TRANSLATORS: nothing attached that can be upgraded */
		g_print ("%s\n", _("No hardware detected with firmware update capability"));
		return TRUE;
	}

	for (guint i = 0; i < results->len; i++) {
		g_autofree gchar *tmp = NULL;
		FwupdResult *res = g_ptr_array_index (results, i);
		tmp = fwupd_result_to_string (res);
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

	/* reboot using systemd */
	connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
	if (connection == NULL)
		return;
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

		/* check not already done */
		if (fwupd_result_get_update_state (res) != FWUPD_UPDATE_STATE_PENDING)
			continue;

		/* tell the user what's going to happen */
		vercmp = as_utils_vercmp (fwupd_result_get_device_version (res),
					  fwupd_result_get_update_version (res));
		if (vercmp == 0) {
			/* TRANSLATORS: the first replacement is a display name
			 * e.g. "ColorHugALS" and the second is a version number
			 * e.g. "1.2.3" */
			g_print (_("Reinstalling %s with %s... "),
				 fwupd_result_get_device_name (res),
				 fwupd_result_get_update_version (res));
		} else if (vercmp > 0) {
			/* TRANSLATORS: the first replacement is a display name
			 * e.g. "ColorHugALS" and the second and third are
			 * version numbers e.g. "1.2.3" */
			g_print (_("Downgrading %s from %s to %s... "),
				 fwupd_result_get_device_name (res),
				 fwupd_result_get_device_version (res),
				 fwupd_result_get_update_version (res));
		} else if (vercmp < 0) {
			/* TRANSLATORS: the first replacement is a display name
			 * e.g. "ColorHugALS" and the second and third are
			 * version numbers e.g. "1.2.3" */
			g_print (_("Updating %s from %s to %s... "),
				 fwupd_result_get_device_name (res),
				 fwupd_result_get_device_version (res),
				 fwupd_result_get_update_version (res));
		}
		if (!fwupd_client_install (priv->client,
					   fwupd_result_get_device_id (res),
					   fwupd_result_get_update_filename (res),
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
	g_autoptr(GPtrArray) results = NULL;

	/* get devices from daemon */
	results = fwupd_client_get_devices (priv->client, NULL, error);
	if (results == NULL)
		return FALSE;

	/* get results */
	for (guint i = 0; i < results->len; i++) {
		g_autoptr(GError) error_local = NULL;
		FwupdResult *res = g_ptr_array_index (results, i);
		if (!fwupd_client_verify_update (priv->client,
					  fwupd_result_get_device_id (res),
					  NULL,
					  &error_local)) {
			g_print ("%s\tFAILED: %s\n",
				 fwupd_result_get_guid_default (res),
				 error_local->message);
			continue;
		}
		g_print ("%s\t%s\n",
			 fwupd_result_get_guid_default (res),
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
fu_util_download_file (FuUtilPrivate *priv,
		       const gchar *uri,
		       const gchar *fn,
		       const gchar *checksum_expected,
		       GChecksumType checksum_type,
		       GError **error)
{
	const gchar *http_proxy;
	guint status_code;
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *checksum_actual = NULL;
	g_autofree gchar *user_agent = NULL;
	g_autoptr(SoupMessage) msg = NULL;
	g_autoptr(SoupSession) session = NULL;

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
	g_debug ("downloading %s to %s:", uri, fn);
	msg = soup_message_new (SOUP_METHOD_GET, uri);
	status_code = soup_session_send_message (session, msg);
	if (status_code != SOUP_STATUS_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Failed to download %s: %s",
			     uri, soup_status_get_phrase (status_code));
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
fu_util_download_metadata (FuUtilPrivate *priv, GError **error)
{
	g_autofree gchar *cache_dir = NULL;
	g_autofree gchar *config_fn = NULL;
	g_autofree gchar *data_fn = NULL;
	g_autofree gchar *data_uri = NULL;
	g_autofree gchar *sig_fn = NULL;
	g_autofree gchar *sig_uri = NULL;
	g_autoptr(GKeyFile) config = NULL;

	/* read config file */
	config = g_key_file_new ();
	config_fn = g_build_filename (SYSCONFDIR, "fwupd.conf", NULL);
	if (!g_key_file_load_from_file (config, config_fn, G_KEY_FILE_NONE, error)) {
		g_prefix_error (error, "Failed to load %s: ", config_fn);
		return FALSE;
	}

	/* ensure cache directory exists */
	cache_dir = g_build_filename (g_get_user_cache_dir (), "fwupdmgr", NULL);
	if (!fu_util_mkdir_with_parents (cache_dir, error))
		return FALSE;

	/* download the signature */
	data_uri = g_key_file_get_string (config, "fwupd", "DownloadURI", error);
	if (data_uri == NULL)
		return FALSE;
	sig_uri = g_strdup_printf ("%s.asc", data_uri);
	data_fn = g_build_filename (cache_dir, "firmware.xml.gz", NULL);
	sig_fn = g_strdup_printf ("%s.asc", data_fn);
	if (!fu_util_download_file (priv, sig_uri, sig_fn, NULL, 0, error))
		return FALSE;

	/* download the payload */
	if (!fu_util_download_file (priv, data_uri, data_fn, NULL, 0, error))
		return FALSE;

	/* send all this to fwupd */
	return fwupd_client_update_metadata (priv->client, data_fn, sig_fn, NULL, error);
}

static gboolean
fu_util_refresh (FuUtilPrivate *priv, gchar **values, GError **error)
{
	if (g_strv_length (values) == 0)
		return fu_util_download_metadata (priv, error);
	if (g_strv_length (values) != 2) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments: expected 'filename.xml' 'filename.xml.asc'");
		return FALSE;
	}

	/* open file */
	return fwupd_client_update_metadata (priv->client,
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
fu_util_verify_all (FuUtilPrivate *priv, GError **error)
{
	g_autoptr(GPtrArray) results = NULL;

	/* get devices from daemon */
	results = fwupd_client_get_devices (priv->client, NULL, error);
	if (results == NULL)
		return FALSE;

	/* get results */
	for (guint i = 0; i < results->len; i++) {
		g_autoptr(GError) error_local = NULL;
		FwupdResult *res = g_ptr_array_index (results, i);
		if (!fwupd_client_verify (priv->client,
					  fwupd_result_get_device_id (res),
					  NULL,
					  &error_local)) {
			g_print ("%s\tFAILED: %s\n",
				 fwupd_result_get_guid_default (res),
				 error_local->message);
			continue;
		}
		g_print ("%s\t%s\n",
			 fwupd_result_get_guid_default (res),
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

static const gchar *
_g_checksum_type_to_string (GChecksumType checksum_type)
{
	if (checksum_type == G_CHECKSUM_MD5)
		return "md5";
	if (checksum_type == G_CHECKSUM_SHA1)
		return "sha1";
	if (checksum_type == G_CHECKSUM_SHA256)
		return "sha256";
	if (checksum_type == G_CHECKSUM_SHA512)
		return "sha512";
	return NULL;
}

static gboolean
fu_util_get_updates (FuUtilPrivate *priv, gchar **values, GError **error)
{
	GPtrArray *results = NULL;
	GPtrArray *guids;
	GChecksumType checksum_type;
	const gchar *tmp;

	/* print any updates */
	results = fwupd_client_get_updates (priv->client, NULL, error);
	if (results == NULL)
		return FALSE;
	for (guint i = 0; i < results->len; i++) {
		FwupdResult *res = g_ptr_array_index (results, i);

		/* TRANSLATORS: first replacement is device name */
		g_print (_("%s has firmware updates:"), fwupd_result_get_device_name (res));
		g_print ("\n");

		/* TRANSLATORS: Appstream ID for the hardware type */
		fu_util_print_data (_("ID"), fwupd_result_get_update_id (res));

		/* TRANSLATORS: a GUID for the hardware */
		guids = fwupd_result_get_guids (res);
		for (guint j = 0; j < guids->len; j++) {
			tmp = g_ptr_array_index (guids, j);
			fu_util_print_data (_("GUID"), tmp);
		}

		/* TRANSLATORS: section header for firmware version */
		fu_util_print_data (_("Update Version"),
				    fwupd_result_get_update_version (res));

		/* TRANSLATORS: section header for firmware checksum */
		fu_util_print_data (_("Update Checksum"),
				    fwupd_result_get_update_checksum (res));

		/* TRANSLATORS: section header for firmware checksum type */
		if (fwupd_result_get_update_checksum (res) != NULL) {
			checksum_type = fwupd_result_get_update_checksum_kind (res);
			tmp = _g_checksum_type_to_string (checksum_type);
			fu_util_print_data (_("Update Checksum Type"), tmp);
		}

		/* TRANSLATORS: section header for firmware remote http:// */
		fu_util_print_data (_("Update Location"), fwupd_result_get_update_uri (res));

		/* convert XML -> text */
		tmp = fwupd_result_get_update_description (res);
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
fu_util_update (FuUtilPrivate *priv, gchar **values, GError **error)
{
	GPtrArray *results = NULL;

	/* apply any updates */
	results = fwupd_client_get_updates (priv->client, NULL, error);
	if (results == NULL)
		return FALSE;
	for (guint i = 0; i < results->len; i++) {
		GChecksumType checksum_type;
		const gchar *checksum;
		const gchar *uri;
		g_autofree gchar *basename = NULL;
		g_autofree gchar *fn = NULL;

		FwupdResult *res = g_ptr_array_index (results, i);

		/* download file */
		checksum = fwupd_result_get_update_checksum (res);
		if (checksum == NULL)
			continue;
		uri = fwupd_result_get_update_uri (res);
		if (uri == NULL)
			continue;
		g_print ("Downloading %s for %s...\n",
			 fwupd_result_get_update_version (res),
			 fwupd_result_get_device_name (res));
		basename = g_path_get_basename (uri);
		fn = g_build_filename (g_get_tmp_dir (), basename, NULL);
		checksum_type = fwupd_result_get_update_checksum_kind (res);
		if (!fu_util_download_file (priv, uri, fn, checksum, checksum_type, error))
			return FALSE;
		g_print ("Updating %s on %s...\n",
			 fwupd_result_get_update_version (res),
			 fwupd_result_get_device_name (res));
		if (!fu_util_install_with_fallback (priv, fwupd_result_get_device_id (res), fn, error))
			return FALSE;
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

int
main (int argc, char *argv[])
{
	FuUtilPrivate *priv;
	gboolean force = FALSE;
	gboolean allow_older = FALSE;
	gboolean allow_reinstall = FALSE;
	gboolean offline = FALSE;
	gboolean ret;
	gboolean verbose = FALSE;
	gint rc = 1;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *cmd_descriptions = NULL;
	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			/* TRANSLATORS: command line option */
			_("Show extra debugging information"), NULL },
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
		goto out;
	}

	/* set verbose? */
	if (verbose) {
		g_setenv ("FWUPD_VERBOSE", "1", FALSE);
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

	/* run the specified command */
	ret = fu_util_run (priv, argv[1], (gchar**) &argv[2], &error);
	if (!ret) {
		if (g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_INVALID_ARGS)) {
			g_autofree gchar *tmp = NULL;
			tmp = g_option_context_get_help (priv->context, TRUE, NULL);
			g_print ("%s\n\n%s", error->message, tmp);
		} else {
			g_print ("%s\n", error->message);
		}
		goto out;
	}

	/* success */
	rc = 0;
out:
	if (priv != NULL) {
		if (priv->cmd_array != NULL)
			g_ptr_array_unref (priv->cmd_array);
		if (priv->client != NULL)
			g_object_unref (priv->client);
		g_main_loop_unref (priv->loop);
		g_object_unref (priv->cancellable);
		g_option_context_free (priv->context);
		g_free (priv);
	}
	return rc;
}
