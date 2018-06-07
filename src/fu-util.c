/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
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
#include <json-glib/json-glib.h>
#include <locale.h>
#include <stdlib.h>
#include <libsoup/soup.h>
#include <unistd.h>

#include "fu-hwids.h"
#include "fu-history.h"
#include "fu-plugin-private.h"
#include "fu-progressbar.h"
#include "fu-util-common.h"
#include "fwupd-common-private.h"

/* this is only valid in this file */
#define FWUPD_ERROR_INVALID_ARGS	(FWUPD_ERROR_LAST+1)

/* custom return code */
#define EXIT_NOTHING_TO_DO		2

typedef struct {
	GCancellable		*cancellable;
	GMainLoop		*loop;
	GOptionContext		*context;
	GPtrArray		*cmd_array;
	SoupSession		*soup_session;
	FwupdInstallFlags	 flags;
	FwupdClient		*client;
	FuProgressbar		*progressbar;
	gboolean		 no_metadata_check;
	gboolean		 no_reboot_check;
	gboolean		 no_unreported_check;
	gboolean		 assume_yes;
	gboolean		 show_all_devices;
} FuUtilPrivate;

typedef gboolean (*FuUtilPrivateCb)	(FuUtilPrivate	*util,
					 gchar		**values,
					 GError		**error);

static gboolean	fu_util_report_history (FuUtilPrivate *priv, gchar **values, GError **error);

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

static void
fu_util_client_notify_cb (GObject *object,
			  GParamSpec *pspec,
			  FuUtilPrivate *priv)
{
	fu_progressbar_update (priv->progressbar,
			       fwupd_client_get_status (priv->client),
			       fwupd_client_get_percentage (priv->client));
}

static FwupdDevice *
fu_util_prompt_for_device (FuUtilPrivate *priv, GError **error)
{
	FwupdDevice *dev;
	guint idx;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) devices_filtered = NULL;

	/* get devices from daemon */
	devices = fwupd_client_get_devices (priv->client, NULL, error);
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
	/* TRANSLATORS: this is to abort the interactive prompt */
	g_print ("0.\t%s\n", _("Cancel"));
	for (guint i = 0; i < devices_filtered->len; i++) {
		dev = g_ptr_array_index (devices_filtered, i);
		g_print ("%u.\t%s (%s)\n",
			 i + 1,
			 fwupd_device_get_id (dev),
			 fwupd_device_get_name (dev));
	}
	idx = fu_util_prompt_for_number (devices_filtered->len);
	if (idx == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOTHING_TO_DO,
				     "Request canceled");
		return NULL;
	}
	dev = g_ptr_array_index (devices_filtered, idx - 1);
	return g_object_ref (dev);
}

static gboolean
fu_util_setup_networking (FuUtilPrivate *priv, GError **error)
{
	const gchar *http_proxy;
	g_autofree gchar *user_agent = NULL;

	/* already done */
	if (priv->soup_session != NULL)
		return TRUE;

	/* create the soup session */
	user_agent = fwupd_build_user_agent (PACKAGE_NAME, PACKAGE_VERSION);
	priv->soup_session = soup_session_new_with_options (SOUP_SESSION_USER_AGENT, user_agent,
							    SOUP_SESSION_TIMEOUT, 60,
							    NULL);
	if (priv->soup_session == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "failed to setup networking");
		return FALSE;
	}

	/* set the proxy */
	http_proxy = g_getenv ("https_proxy");
	if (http_proxy == NULL)
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
		g_object_set (priv->soup_session, SOUP_SESSION_PROXY_URI, proxy_uri, NULL);
	}

	/* this disables the double-compression of the firmware.xml.gz file */
	soup_session_remove_feature_by_type (priv->soup_session, SOUP_TYPE_CONTENT_DECODER);
	return TRUE;
}

static gboolean
fu_util_perhaps_show_unreported (FuUtilPrivate *priv, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) devices_failed = g_ptr_array_new ();
	g_autoptr(GPtrArray) devices_success = g_ptr_array_new ();

	/* we don't want to ask anything */
	if (priv->no_unreported_check) {
		g_debug ("skipping unreported check");
		return TRUE;
	}

	/* get all devices from the history database */
	devices = fwupd_client_get_history (priv->client, NULL, &error_local);
	if (devices == NULL) {
		if (g_error_matches (error_local, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO))
			return TRUE;
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}

	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index (devices, i);
		if (fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_REPORTED))
			continue;
		if (!fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_SUPPORTED))
			continue;
		switch (fwupd_device_get_update_state (dev)) {
		case FWUPD_UPDATE_STATE_FAILED:
			g_ptr_array_add (devices_failed, dev);
			break;
		case FWUPD_UPDATE_STATE_SUCCESS:
			g_ptr_array_add (devices_success, dev);
			break;
		default:
			break;
		}
	}

	/* nothing to do */
	if (devices_failed->len == 0 && devices_success->len == 0) {
		g_debug ("no unreported devices");
		return TRUE;
	}

	/* show the success and failures */
	if (!priv->assume_yes) {

		/* delimit */
		g_print ("________________________________________________\n");

		/* failures */
		if (devices_failed->len > 0) {
			/* TRANSLATORS: a list of failed updates */
			g_print ("\n%s\n\n", _("Devices that were not updated correctly:"));
			for (guint i = 0; i < devices_failed->len; i++) {
				FwupdDevice *dev = g_ptr_array_index (devices_failed, i);
				FwupdRelease *rel = fwupd_device_get_release_default (dev);
				g_print (" • %s (%s → %s)\n",
					 fwupd_device_get_name (dev),
					 fwupd_device_get_version (dev),
					 fwupd_release_get_version (rel));
			}
		}

		/* success */
		if (devices_success->len > 0) {
			/* TRANSLATORS: a list of successful updates */
			g_print ("\n%s\n\n", _("Devices that have been updated successfully:"));
			for (guint i = 0; i < devices_success->len; i++) {
				FwupdDevice *dev = g_ptr_array_index (devices_success, i);
				FwupdRelease *rel = fwupd_device_get_release_default (dev);
				g_print (" • %s (%s → %s)\n",
					 fwupd_device_get_name (dev),
					 fwupd_device_get_version (dev),
					 fwupd_release_get_version (rel));
			}
		}

		/* ask for permission */
		g_print ("\n%s\n%s (%s) [Y|n]: ",
			 /* TRANSLATORS: explain why we want to upload */
			 _("Uploading firmware reports helps hardware vendors"
			   " to quickly identify failing and successful updates"
			   " on real devices."),
			 /* TRANSLATORS: ask the user to upload */
			 _("Upload report now?"),
			 /* TRANSLATORS: metadata is downloaded from the Internet */
			 _("Requires internet connection"));
		if (!fu_util_prompt_for_boolean (TRUE))
			return TRUE;
	}

	/* success */
	return fu_util_report_history (priv, NULL, error);
}

static gboolean
fu_util_modify_remote_warning (FuUtilPrivate *priv, FwupdRemote *remote, GError **error)
{
	const gchar *warning_markup = NULL;
	g_autofree gchar *warning_plain = NULL;

	/* get formatted text */
	warning_markup = fwupd_remote_get_agreement (remote);
	if (warning_markup == NULL)
		return TRUE;
	warning_plain = as_markup_convert_simple (warning_markup, error);
	if (warning_plain == NULL)
		return FALSE;

	/* show and ask user to confirm */
	g_print ("%s", warning_plain);
	if (!priv->assume_yes) {
		/* ask for permission */
		g_print ("\n%s [Y|n]: ",
			 /* TRANSLATORS: should the remote still be enabled */
			 _("Agree and enable the remote?"));
		if (!fu_util_prompt_for_boolean (TRUE)) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOTHING_TO_DO,
					     "Declined agreement");
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_util_modify_remote (FuUtilPrivate *priv,
		       const gchar *remote_id,
		       const gchar *key,
		       const gchar *value,
		       GError **error)
{
	g_autoptr(FwupdRemote) remote = NULL;

	/* ensure the remote exists */
	remote = fwupd_client_get_remote_by_id (priv->client, remote_id, NULL, error);
	if (remote == NULL)
		return FALSE;

	/* show some kind of warning when enabling download-type remotes */
	if (g_strcmp0 (key, "Enabled") == 0 && g_strcmp0 (value, "true") == 0) {
		if (!fu_util_modify_remote_warning (priv, remote, error))
			return FALSE;
	}
	return fwupd_client_modify_remote (priv->client,
					   remote_id, key, value,
					   NULL, error);
}

static void
fu_util_build_device_tree (FuUtilPrivate *priv, GNode *root, GPtrArray *devs, FwupdDevice *dev)
{
	for (guint i = 0; i < devs->len; i++) {
		FwupdDevice *dev_tmp = g_ptr_array_index (devs, i);
		if (!(fwupd_device_has_flag (dev_tmp, FWUPD_DEVICE_FLAG_UPDATABLE) || priv->show_all_devices))
			continue;
		if (fwupd_device_get_parent (dev_tmp) == dev) {
			GNode *child = g_node_append_data (root, dev_tmp);
			fu_util_build_device_tree (priv, child, devs, dev_tmp);
		}
	}
}

static gboolean
fu_util_get_topology (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GNode) root = g_node_new (NULL);
	g_autoptr(GPtrArray) devs = NULL;

	/* get results from daemon */
	devs = fwupd_client_get_devices (priv->client, NULL, error);
	if (devs == NULL)
		return FALSE;

	/* print */
	if (devs->len == 0) {
		/* TRANSLATORS: nothing attached that can be upgraded */
		g_print ("%s\n", _("No hardware detected with firmware update capability"));
		return TRUE;
	}
	fu_util_build_device_tree (priv, root, devs, NULL);
	g_node_traverse (root, G_PRE_ORDER, G_TRAVERSE_ALL, -1,
			 fu_util_print_device_tree, priv);

	return TRUE;
}

static gboolean
fu_util_get_devices (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GPtrArray) devs = NULL;

	/* get results from daemon */
	devs = fwupd_client_get_devices (priv->client, NULL, error);
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
		if (!(fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE) || priv->show_all_devices))
			continue;
		tmp = fwupd_device_to_string (dev);
		g_print ("%s\n", tmp);
	}

	/* nag? */
	if (!fu_util_perhaps_show_unreported (priv, error))
		return FALSE;

	return TRUE;
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
				     "Invalid arguments");
		return FALSE;
	}

	/* install with flags chosen by the user */
	return fwupd_client_install (priv->client, id, values[0], priv->flags, NULL, error);
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
				     "Invalid arguments");
		return FALSE;
	}
	array = fwupd_client_get_details (priv->client, values[0], NULL, error);
	if (array == NULL)
		return FALSE;
	for (guint i = 0; i < array->len; i++) {
		FwupdDevice *dev = g_ptr_array_index (array, i);
		g_autofree gchar *tmp = NULL;
		tmp = fwupd_device_to_string (dev);
		g_print ("%s", tmp);
	}
	return TRUE;
}

static gboolean
fu_util_update_reboot (GError **error)
{
	g_autoptr(GDBusConnection) connection = NULL;
	g_autoptr(GVariant) val = NULL;

	connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, error);
	if (connection == NULL)
		return FALSE;

#ifdef HAVE_SYSTEMD
	/* reboot using logind */
	val = g_dbus_connection_call_sync (connection,
					   "org.freedesktop.login1",
					   "/org/freedesktop/login1",
					   "org.freedesktop.login1.Manager",
					   "Reboot",
					   g_variant_new ("(b)", TRUE),
					   NULL,
					   G_DBUS_CALL_FLAGS_NONE,
					   -1,
					   NULL,
					   error);
#elif defined(HAVE_CONSOLEKIT)
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
					   error);
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_ARGS,
			     "No supported backend compiled in to perform the operation.");
#endif
	return val != NULL;
}

static gboolean
fu_util_install_prepared (FuUtilPrivate *priv, gchar **values, GError **error)
{
	gint vercmp;
	guint cnt = 0;
	g_autofree gchar *link = NULL;
	g_autoptr(GPtrArray) results = NULL;
	g_autoptr(FuHistory) history = NULL;

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
	history = fu_history_new ();
	results = fu_history_get_devices (history, error);
	if (results == NULL)
		return FALSE;

	/* apply each update */
	for (guint i = 0; i < results->len; i++) {
		FwupdDevice *dev = g_ptr_array_index (results, i);
		FwupdRelease *rel = fwupd_device_get_release_default (dev);

		/* check not already done */
		if (fwupd_device_get_update_state (dev) != FWUPD_UPDATE_STATE_PENDING)
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
	if (!fu_util_update_reboot (error))
		return FALSE;

	g_print ("%s\n", _("Done!"));
	return TRUE;
}

static gboolean
fu_util_clear_history (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FuHistory) history = fu_history_new ();
	return fu_history_remove_all (history, error);
}

static gboolean
fu_util_report_history_for_uri (FuUtilPrivate *priv,
				const gchar *report_uri,
				GPtrArray *devices,
				GError **error)
{
	JsonNode *json_root;
	JsonObject *json_object;
	const gchar *server_msg = NULL;
	guint status_code;
	g_autofree gchar *data = NULL;
	g_autoptr(JsonParser) json_parser = NULL;
	g_autoptr(SoupMessage) msg = NULL;

	/* convert to JSON */
	data = fwupd_build_history_report_json (devices, error);
	if (data == NULL)
		return FALSE;

	/* ask for permission */
	if (!priv->assume_yes) {
		fu_util_print_data (_("Target"), report_uri);
		fu_util_print_data (_("Payload"), data);
		g_print ("%s [Y|n]: ", _("Proceed with upload?"));
		if (!fu_util_prompt_for_boolean (TRUE)) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_PERMISSION_DENIED,
					     "User declined action");
			return FALSE;
		}
	}

	/* POST request */
	msg = soup_message_new (SOUP_METHOD_POST, report_uri);
	soup_message_set_request (msg, "application/json; charset=utf-8",
				  SOUP_MEMORY_COPY, data, strlen (data));
	status_code = soup_session_send_message (priv->soup_session, msg);
	g_debug ("server returned: %s", msg->response_body->data);

	/* server returned nothing, and probably exploded in a ball of flames */
	if (msg->response_body->length == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Failed to upload to %s: %s",
			     report_uri, soup_status_get_phrase (status_code));
		return FALSE;
	}

	/* parse JSON reply */
	json_parser = json_parser_new ();
	if (!json_parser_load_from_data (json_parser,
					 msg->response_body->data,
					 msg->response_body->length,
					 error)) {
		g_autofree gchar *str = g_strndup (msg->response_body->data,
						   msg->response_body->length);
		g_prefix_error (error, "Failed to parse JSON response from '%s': ", str);
		return FALSE;
	}
	json_root = json_parser_get_root (json_parser);
	if (json_root == NULL) {
		g_autofree gchar *str = g_strndup (msg->response_body->data,
						   msg->response_body->length);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_PERMISSION_DENIED,
			     "JSON response was malformed: '%s'", str);
		return FALSE;
	}
	json_object = json_node_get_object (json_root);
	if (json_object == NULL) {
		g_autofree gchar *str = g_strndup (msg->response_body->data,
						   msg->response_body->length);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_PERMISSION_DENIED,
			     "JSON response object was malformed: '%s'", str);
		return FALSE;
	}

	/* get any optional server message */
	if (json_object_has_member (json_object, "msg"))
		server_msg = json_object_get_string_member (json_object, "msg");

	/* server reported failed */
	if (!json_object_get_boolean_member (json_object, "success")) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_PERMISSION_DENIED,
			     "Server rejected report: %s",
			     server_msg != NULL ? server_msg : "unspecified");
		return FALSE;
	}

	/* server wanted us to see the message */
	if (server_msg != NULL) {
		if (g_strstr_len (server_msg, -1, "known issue") != NULL &&
		    json_object_has_member (json_object, "uri")) {
			g_print ("%s %s\n",
				 /* TRANSLATORS: the server sent the user a small message */
				 _("Update failure is a known issue, visit this URL for more information:"),
				 json_object_get_string_member (json_object, "uri"));
		} else {
			/* TRANSLATORS: the server sent the user a small message */
			g_print ("%s %s\n", _("Upload message:"), server_msg);
		}
	}

	/* fall back to HTTP status codes in case the server is offline */
	if (!SOUP_STATUS_IS_SUCCESSFUL (status_code)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Failed to upload to %s: %s",
			     report_uri, soup_status_get_phrase (status_code));
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_util_report_history (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GHashTable) remote_id_uri_map = NULL;
	g_autoptr(GHashTable) report_map = NULL;
	g_autoptr(GList) uris = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) remotes = NULL;

	/* set up networking */
	if (!fu_util_setup_networking (priv, error))
		return FALSE;

	/* create a map of RemoteID to RemoteURI */
	remotes = fwupd_client_get_remotes (priv->client, NULL, error);
	if (remotes == NULL)
		return FALSE;
	remote_id_uri_map = g_hash_table_new (g_str_hash, g_str_equal);
	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote = g_ptr_array_index (remotes, i);
		if (fwupd_remote_get_id (remote) == NULL)
			continue;
		if (fwupd_remote_get_report_uri (remote) == NULL)
			continue;
		g_debug ("adding %s for %s",
			 fwupd_remote_get_report_uri (remote),
			 fwupd_remote_get_id (remote));
		g_hash_table_insert (remote_id_uri_map,
				     (gpointer) fwupd_remote_get_id (remote),
				     (gpointer) fwupd_remote_get_report_uri (remote));
	}

	/* get all devices from the history database, then filter them,
	 * adding to a hash map of report-ids */
	devices = fwupd_client_get_history (priv->client, NULL, error);
	if (devices == NULL)
		return FALSE;
	report_map = g_hash_table_new_full (g_str_hash, g_str_equal,
					    g_free, (GDestroyNotify) g_ptr_array_unref);
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index (devices, i);
		FwupdRelease *rel = fwupd_device_get_release_default (dev);
		const gchar *remote_id;
		const gchar *remote_uri;
		GPtrArray *devices_tmp;

		/* filter, if not forcing */
		if ((priv->flags & FWUPD_INSTALL_FLAG_FORCE) == 0) {
			if (fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_REPORTED))
				continue;
			if (!fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_SUPPORTED))
				continue;
		}

		/* only send success and failure */
		if (fwupd_device_get_update_state (dev) != FWUPD_UPDATE_STATE_FAILED &&
		    fwupd_device_get_update_state (dev) != FWUPD_UPDATE_STATE_SUCCESS) {
			g_debug ("ignoring %s with UpdateState %s",
				 fwupd_device_get_id (dev),
				 fwupd_update_state_to_string (fwupd_device_get_update_state (dev)));
			continue;
		}

		/* find the RemoteURI to use for the device */
		remote_id = fwupd_release_get_remote_id (rel);
		if (remote_id == NULL) {
			g_debug ("%s has no RemoteID", fwupd_device_get_id (dev));
			continue;
		}
		remote_uri = g_hash_table_lookup (remote_id_uri_map, remote_id);
		if (remote_uri == NULL) {
			g_debug ("%s has no RemoteURI", remote_id);
			continue;
		}

		/* add this to the hash map */
		devices_tmp = g_hash_table_lookup (report_map, remote_uri);
		if (devices_tmp == NULL) {
			devices_tmp = g_ptr_array_new ();
			g_hash_table_insert (report_map, g_strdup (remote_uri), devices_tmp);
		}
		g_debug ("using %s for %s", remote_uri, fwupd_device_get_id (dev));
		g_ptr_array_add (devices_tmp, dev);
	}

	/* nothing to report */
	if (g_hash_table_size (report_map) == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "No reports require uploading");
		return FALSE;
	}

	/* process each uri */
	uris = g_hash_table_get_keys (report_map);
	for (GList *l = uris; l != NULL; l = l->next) {
		const gchar *uri = l->data;
		GPtrArray *devices_tmp = g_hash_table_lookup (report_map, uri);
		if (!fu_util_report_history_for_uri (priv, uri, devices_tmp, error))
			return FALSE;
	}

	/* mark each device as reported */
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index (devices, i);
		if (fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_REPORTED))
			continue;
		if (!fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_SUPPORTED))
			continue;
		g_debug ("setting flag on %s", fwupd_device_get_id (dev));
		if (!fwupd_client_modify_device (priv->client,
						 fwupd_device_get_id (dev),
						 "Flags", "reported",
						 NULL, error))
			return FALSE;
	}

	return TRUE;
}

static gboolean
fu_util_get_history (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;

	/* get all devices from the history database */
	devices = fwupd_client_get_history (priv->client, NULL, error);
	if (devices == NULL)
		return FALSE;

	/* show each device */
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index (devices, i);
		g_autofree gchar *str = fwupd_device_to_string (dev);
		g_print ("%s\n", str);
	}

	return TRUE;
}

static FwupdDevice*
fu_util_get_device_or_prompt (FuUtilPrivate *priv, gchar **values, GError **error)
{
	FwupdDevice *dev = NULL;

	/* get device to use */
	if (g_strv_length (values) >= 1) {
		g_autoptr(GError) error_local = NULL;
		if (g_strv_length (values) > 1) {
			for (guint i = 1; i < g_strv_length (values); i++)
				g_debug ("Ignoring extra input %s", values[i]);
		}
		dev = fwupd_client_get_device_by_id (priv->client, values[0],
						     NULL, &error_local);
		if (dev != NULL)
			return dev;
		g_print ("%s\n",  error_local->message);
	}
	return fu_util_prompt_for_device (priv, error);
}

static gboolean
fu_util_clear_results (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FwupdDevice) dev = NULL;

	dev = fu_util_get_device_or_prompt (priv, values, error);
	if (dev == NULL)
		return FALSE;

	return fwupd_client_clear_results (priv->client, fwupd_device_get_id (dev), NULL, error);
}

static gboolean
fu_util_clear_offline (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FuHistory) history = fu_history_new ();
	return fu_history_remove_all_with_state (history, FWUPD_UPDATE_STATE_PENDING, error);
}

static gboolean
fu_util_verify_update_all (FuUtilPrivate *priv, GError **error)
{
	g_autoptr(GPtrArray) devs = NULL;

	/* get devices from daemon */
	devs = fwupd_client_get_devices (priv->client, NULL, error);
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
	g_autoptr(FwupdDevice) dev = NULL;

	if (g_strv_length (values) == 0)
		return fu_util_verify_update_all (priv, error);

	dev = fu_util_get_device_or_prompt (priv, values, error);
	if (dev == NULL)
		return FALSE;

	return fwupd_client_verify_update (priv->client, fwupd_device_get_id (dev), NULL, error);
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
	FuUtilPrivate *priv = (FuUtilPrivate *) user_data;

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
	fu_progressbar_update (priv->progressbar, FWUPD_STATUS_DOWNLOADING, percentage);
}

static gboolean
fu_util_download_file (FuUtilPrivate *priv,
		       SoupURI *uri,
		       const gchar *fn,
		       const gchar *checksum_expected,
		       GError **error)
{
	GChecksumType checksum_type;
	guint status_code;
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *checksum_actual = NULL;
	g_autofree gchar *uri_str = NULL;
	g_autoptr(SoupMessage) msg = NULL;

	/* check if the file already exists with the right checksum */
	checksum_type = fwupd_checksum_guess_kind (checksum_expected);
	if (fu_util_file_exists_with_checksum (fn, checksum_expected, checksum_type)) {
		g_debug ("skpping download as file already exists");
		return TRUE;
	}

	/* set up networking */
	if (!fu_util_setup_networking (priv, error))
		return FALSE;

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
	if (g_str_has_suffix (uri_str, ".asc") ||
	    g_str_has_suffix (uri_str, ".p7b") ||
	    g_str_has_suffix (uri_str, ".p7c")) {
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
	status_code = soup_session_send_message (priv->soup_session, msg);
	g_print ("\n");
	if (status_code == 429) {
		g_autofree gchar *str = g_strndup (msg->response_body->data,
						   msg->response_body->length);
		if (g_strcmp0 (str, "Too Many Requests") == 0) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     /* TRANSLATORS: the server is rate-limiting downloads */
				     "%s", _("Failed to download due to server limit"));
			return FALSE;
		}
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Failed to download due to server limit: %s", str);
		return FALSE;
	}
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
fu_util_download_metadata_for_remote (FuUtilPrivate *priv,
				      FwupdRemote *remote,
				      GError **error)
{
	g_autofree gchar *basename_asc = NULL;
	g_autofree gchar *basename_id_asc = NULL;
	g_autofree gchar *basename_id = NULL;
	g_autofree gchar *basename = NULL;
	g_autofree gchar *cache_dir = NULL;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *filename_asc = NULL;
	g_autoptr(SoupURI) uri = NULL;
	g_autoptr(SoupURI) uri_sig = NULL;

	/* generate some plausible local filenames */
	basename = g_path_get_basename (fwupd_remote_get_filename_cache (remote));
	basename_id = g_strdup_printf ("%s-%s", fwupd_remote_get_id (remote), basename);

	/* download the metadata */
	cache_dir = g_build_filename (g_get_user_cache_dir (), "fwupdmgr", NULL);
	filename = g_build_filename (cache_dir, basename_id, NULL);
	if (!fu_common_mkdir_parent (filename, error))
		return FALSE;
	uri = soup_uri_new (fwupd_remote_get_metadata_uri (remote));
	if (!fu_util_download_file (priv, uri, filename, NULL, error))
		return FALSE;

	/* download the signature */
	basename_asc = g_path_get_basename (fwupd_remote_get_filename_cache_sig (remote));
	basename_id_asc = g_strdup_printf ("%s-%s", fwupd_remote_get_id (remote), basename_asc);
	filename_asc = g_build_filename (cache_dir, basename_id_asc, NULL);
	uri_sig = soup_uri_new (fwupd_remote_get_metadata_uri_sig (remote));
	if (!fu_util_download_file (priv, uri_sig, filename_asc, NULL, error))
		return FALSE;

	/* send all this to fwupd */
	return fwupd_client_update_metadata (priv->client,
					     fwupd_remote_get_id (remote),
					     filename,
					     filename_asc,
					     NULL, error);
}

static gboolean
fu_util_download_metadata_enable_lvfs (FuUtilPrivate *priv, GError **error)
{
	g_autoptr(FwupdRemote) remote = NULL;

	/* is the LVFS available but disabled? */
	remote = fwupd_client_get_remote_by_id (priv->client, "lvfs", NULL, error);
	if (remote == NULL)
		return TRUE;
	g_print ("%s\n%s\n%s [Y|n]: ",
		/* TRANSLATORS: explain why no metadata available */
		_("No remotes are currently enabled so no metadata is available."),
		/* TRANSLATORS: explain why no metadata available */
		_("Metadata can be obtained from the Linux Vendor Firmware Service."),
		/* TRANSLATORS: Turn on the remote */
		_("Enable this remote?"));
	if (!fu_util_prompt_for_boolean (TRUE))
		return TRUE;
	if (!fu_util_modify_remote (priv, "lvfs", "Enabled", "true", error))
		return FALSE;

	/* refresh the newly-enabled remote */
	return fu_util_download_metadata_for_remote (priv, remote, error);
}

static gboolean
fu_util_download_metadata (FuUtilPrivate *priv, GError **error)
{
	gboolean download_remote_enabled = FALSE;
	g_autoptr(GPtrArray) remotes = NULL;

	remotes = fwupd_client_get_remotes (priv->client, NULL, error);
	if (remotes == NULL)
		return FALSE;
	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote = g_ptr_array_index (remotes, i);
		if (!fwupd_remote_get_enabled (remote))
			continue;
		if (fwupd_remote_get_kind (remote) != FWUPD_REMOTE_KIND_DOWNLOAD)
			continue;
		download_remote_enabled = TRUE;
		if (!fu_util_download_metadata_for_remote (priv, remote, error))
			return FALSE;
	}

	/* no web remote is declared; try to enable LVFS */
	if (!download_remote_enabled) {
		if (!fu_util_download_metadata_enable_lvfs (priv, error))
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
				     "Invalid arguments");
		return FALSE;
	}

	/* open file */
	return fwupd_client_update_metadata (priv->client,
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
	g_autoptr(FwupdDevice) dev = NULL;
	g_autoptr(FwupdDevice) rel = NULL;

	dev = fu_util_get_device_or_prompt (priv, values, error);
	if (dev == NULL)
		return FALSE;

	rel = fwupd_client_get_results (priv->client, fwupd_device_get_id (dev), NULL, error);
	if (rel == NULL)
		return FALSE;
	tmp = fwupd_device_to_string (rel);
	g_print ("%s", tmp);
	return TRUE;
}

static gboolean
fu_util_get_releases (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FwupdDevice) dev = NULL;
	g_autoptr(GPtrArray) rels = NULL;

	dev = fu_util_get_device_or_prompt (priv, values, error);
	if (dev == NULL)
		return FALSE;

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

		/* TRANSLATORS: section header for the release name */
		fu_util_print_data (_("Name"), fwupd_release_get_name (rel));

		/* TRANSLATORS: section header for the release one line summary */
		fu_util_print_data (_("Summary"), fwupd_release_get_summary (rel));

		/* TRANSLATORS: section header for the remote the file is coming from */
		fu_util_print_data (_("Remote"), fwupd_release_get_remote_id (rel));

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
	devs = fwupd_client_get_devices (priv->client, NULL, error);
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
	g_autoptr(FwupdDevice) dev = NULL;

	if (g_strv_length (values) == 0)
		return fu_util_verify_all (priv, error);

	dev = fu_util_get_device_or_prompt (priv, values, error);
	if (dev == NULL)
		return FALSE;

	return fwupd_client_verify (priv->client, fwupd_device_get_id (dev), NULL, error);
}

static gboolean
fu_util_unlock (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FwupdDevice) dev = NULL;

	dev = fu_util_get_device_or_prompt (priv, values, error);
	if (dev == NULL)
		return FALSE;

	return fwupd_client_unlock (priv->client, fwupd_device_get_id (dev), NULL, error);
}

static gboolean
fu_util_perhaps_refresh_remotes (FuUtilPrivate *priv, GError **error)
{
	g_autoptr(GPtrArray) remotes = NULL;
	guint64 age_oldest = 0;
	const guint64 age_limit_days = 30;

	/* we don't want to ask anything */
	if (priv->no_metadata_check) {
		g_debug ("skipping metadata check");
		return TRUE;
	}

	/* get the age of the oldest enabled remotes */
	remotes = fwupd_client_get_remotes (priv->client, NULL, error);
	if (remotes == NULL)
		return FALSE;
	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote = g_ptr_array_index (remotes, i);
		if (!fwupd_remote_get_enabled (remote))
			continue;
		if (fwupd_remote_get_kind (remote) != FWUPD_REMOTE_KIND_DOWNLOAD)
			continue;
		if (fwupd_remote_get_age (remote) > age_oldest)
			age_oldest = fwupd_remote_get_age (remote);
	}

	/* metadata is new enough */
	if (age_oldest < 60 * 60 * 24 * age_limit_days)
		return TRUE;

	/* ask for permission */
	if (!priv->assume_yes) {
		/* TRANSLATORS: the metadata is very out of date; %u is a number > 1 */
		g_print (ngettext("Firmware metadata has not been updated for %u"
				  " day and may not be up to date.",
				  "Firmware metadata has not been updated for %u"
				  " days and may not be up to date.",
				  (gint) age_limit_days), (guint) age_limit_days);
		g_print ("\n\n");
		g_print ("%s (%s) [y|N]: ",
			 /* TRANSLATORS: ask the user if we can update the metadata */
			 _("Update now?"),
			 /* TRANSLATORS: metadata is downloaded from the Internet */
			 _("Requires internet connection"));
		if (!fu_util_prompt_for_boolean (FALSE))
			return TRUE;
	}

	/* downloads new metadata */
	return fu_util_download_metadata (priv, error);
}

static gboolean
fu_util_get_updates (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;

	/* are the remotes very old */
	if (!fu_util_perhaps_refresh_remotes (priv, error))
		return FALSE;

	/* get devices from daemon */
	devices = fwupd_client_get_devices (priv->client, NULL, error);
	if (devices == NULL)
		return FALSE;
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index (devices, i);
		GPtrArray *guids;
		const gchar *tmp;
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
			g_printerr ("%s\n", error_local->message);
			continue;
		}

		/* TRANSLATORS: first replacement is device name */
		g_print (_("%s has firmware updates:"), fwupd_device_get_name (dev));
		g_print ("\n");

		/* TRANSLATORS: a GUID for the hardware */
		guids = fwupd_device_get_guids (dev);
		for (guint j = 0; j < guids->len; j++) {
			tmp = g_ptr_array_index (guids, j);
			fu_util_print_data (_("GUID"), tmp);
		}

		/* print all releases */
		for (guint j = 0; j < rels->len; j++) {
			FwupdRelease *rel = g_ptr_array_index (rels, j);
			GPtrArray *checksums;

			/* TRANSLATORS: Appstream ID for the hardware type */
			fu_util_print_data (_("ID"), fwupd_release_get_appstream_id (rel));

			/* TRANSLATORS: section header for firmware version */
			fu_util_print_data (_("Update Version"),
					    fwupd_release_get_version (rel));

			/* TRANSLATORS: section header for the release name */
			fu_util_print_data (_("Update Name"), fwupd_release_get_name (rel));

			/* TRANSLATORS: section header for the release one line summary */
			fu_util_print_data (_("Update Summary"), fwupd_release_get_summary (rel));

			/* TRANSLATORS: section header for remote ID, e.g. lvfs-testing */
			fu_util_print_data (_("Update Remote ID"),
					    fwupd_release_get_remote_id (rel));

			checksums = fwupd_release_get_checksums (rel);
			for (guint k = 0; k < checksums->len; k++) {
				const gchar *checksum = g_ptr_array_index (checksums, k);
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
	}

	/* nag? */
	if (!fu_util_perhaps_show_unreported (priv, error))
		return FALSE;

	/* success */
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
		FwupdKeyringKind keyring_kind = fwupd_remote_get_keyring_kind (remote);
		const gchar *tmp;
		gint priority;
		gdouble age;

		/* TRANSLATORS: remote identifier, e.g. lvfs-testing */
		fu_util_print_data (_("Remote ID"),
				    fwupd_remote_get_id (remote));

		/* TRANSLATORS: remote title, e.g. "Linux Vendor Firmware Service" */
		fu_util_print_data (_("Title"),
				    fwupd_remote_get_title (remote));

		/* TRANSLATORS: remote type, e.g. remote or local */
		fu_util_print_data (_("Type"),
				    fwupd_remote_kind_to_string (kind));

		/* TRANSLATORS: keyring type, e.g. GPG or PKCS7 */
		if (keyring_kind != FWUPD_KEYRING_KIND_UNKNOWN) {
			fu_util_print_data (_("Keyring"),
					    fwupd_keyring_kind_to_string (keyring_kind));
		}

		/* TRANSLATORS: if the remote is enabled */
		fu_util_print_data (_("Enabled"),
				    fwupd_remote_get_enabled (remote) ? "true" : "false");

		/* TRANSLATORS: remote checksum */
		fu_util_print_data (_("Checksum"),
				    fwupd_remote_get_checksum (remote));

		/* optional parameters */
		age = fwupd_remote_get_age (remote);
		if (kind == FWUPD_REMOTE_KIND_DOWNLOAD &&
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
		tmp = fwupd_remote_get_filename_cache (remote);
		if (tmp != NULL) {
			/* TRANSLATORS: filename of the local file */
			fu_util_print_data (_("Filename"), tmp);
		}
		tmp = fwupd_remote_get_filename_cache_sig (remote);
		if (tmp != NULL) {
			/* TRANSLATORS: filename of the local file */
			fu_util_print_data (_("Filename Signature"), tmp);
		}
		tmp = fwupd_remote_get_metadata_uri (remote);
		if (tmp != NULL) {
			/* TRANSLATORS: remote URI */
			fu_util_print_data (_("Metadata URI"), tmp);
		}
		tmp = fwupd_remote_get_metadata_uri_sig (remote);
		if (tmp != NULL) {
			/* TRANSLATORS: remote URI */
			fu_util_print_data (_("Metadata URI Signature"), tmp);
		}
		tmp = fwupd_remote_get_firmware_base_uri (remote);
		if (tmp != NULL) {
			/* TRANSLATORS: remote URI */
			fu_util_print_data (_("Firmware Base URI"), tmp);
		}
		tmp = fwupd_remote_get_report_uri (remote);
		if (tmp != NULL) {
			/* TRANSLATORS: URI to send success/failure reports */
			fu_util_print_data (_("Report URI"), tmp);
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
			 FwupdDevice *device,
			 gpointer user_data)
{
	g_autofree gchar *tmp = fwupd_device_to_string (device);
	/* TRANSLATORS: this is when a device is hotplugged */
	g_print ("%s\n%s", _("Device added:"), tmp);
}

static void
fu_util_device_removed_cb (FwupdClient *client,
			   FwupdDevice *device,
			   gpointer user_data)
{
	g_autofree gchar *tmp = fwupd_device_to_string (device);
	/* TRANSLATORS: this is when a device is hotplugged */
	g_print ("%s\n%s", _("Device removed:"), tmp);
}

static void
fu_util_device_changed_cb (FwupdClient *client,
			   FwupdDevice *device,
			   gpointer user_data)
{
	g_autofree gchar *tmp = fwupd_device_to_string (device);
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
fu_util_firmware_builder (FuUtilPrivate *priv, gchar **values, GError **error)
{
	const gchar *script_fn = "startup.sh";
	const gchar *output_fn = "firmware.bin";
	g_autoptr(GBytes) archive_blob = NULL;
	g_autoptr(GBytes) firmware_blob = NULL;
	if (g_strv_length (values) < 2) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments");
		return FALSE;
	}
	archive_blob = fu_common_get_contents_bytes (values[0], error);
	if (archive_blob == NULL)
		return FALSE;
	if (g_strv_length (values) > 2)
		script_fn = values[2];
	if (g_strv_length (values) > 3)
		output_fn = values[3];
	firmware_blob = fu_common_firmware_builder (archive_blob, script_fn, output_fn, error);
	if (firmware_blob == NULL)
		return FALSE;
	return fu_common_set_contents_bytes (values[1], firmware_blob, error);
}

static gboolean
fu_util_monitor (FuUtilPrivate *priv, gchar **values, GError **error)
{
	/* get all the devices */
	if (!fwupd_client_connect (priv->client, priv->cancellable, error))
		return FALSE;

	/* watch for any hotplugged device */
	g_signal_connect (priv->client, "changed",
			  G_CALLBACK (fu_util_changed_cb), priv);
	g_signal_connect (priv->client, "device-added",
			  G_CALLBACK (fu_util_device_added_cb), priv);
	g_signal_connect (priv->client, "device-removed",
			  G_CALLBACK (fu_util_device_removed_cb), priv);
	g_signal_connect (priv->client, "device-changed",
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
	g_autofree gchar *fn = NULL;
	g_autofree gchar *uri_str = NULL;
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

			/* install with flags chosen by the user */
			fn = g_build_filename (path, uri_tmp, NULL);
			return fwupd_client_install (priv->client,
						     fwupd_device_get_id (dev),
						     fn, priv->flags, NULL, error);
		}

		uri_str = fwupd_remote_build_firmware_uri (remote, uri_tmp, error);
		if (uri_str == NULL)
			return FALSE;
	} else {
		uri_str = g_strdup (uri_tmp);
	}

	/* download file */
	g_print ("Downloading %s for %s...\n",
		 fwupd_release_get_version (rel),
		 fwupd_device_get_name (dev));
	basename = g_path_get_basename (uri_str);
	fn = g_build_filename (g_get_user_cache_dir (), "fwupdmgr", basename, NULL);
	if (!fu_common_mkdir_parent (fn, error))
		return FALSE;
	checksums = fwupd_release_get_checksums (rel);
	uri = soup_uri_new (uri_str);
	if (!fu_util_download_file (priv, uri, fn,
				    fwupd_checksum_get_best (checksums),
				    error))
		return FALSE;
	g_print ("Updating %s on %s...\n",
		 fwupd_release_get_version (rel),
		 fwupd_device_get_name (dev));
	return fwupd_client_install (priv->client,
				     fwupd_device_get_id (dev), fn,
				     priv->flags, NULL, error);
}

static gboolean
fu_util_update (FuUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean requires_reboot = FALSE;
	g_autoptr(GPtrArray) devices = NULL;

	/* get devices from daemon */
	devices = fwupd_client_get_devices (priv->client, NULL, error);
	if (devices == NULL)
		return FALSE;
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index (devices, i);
		FwupdRelease *rel;
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
			g_printerr ("%s\n", error_local->message);
			continue;
		}
		rel = g_ptr_array_index (rels, 0);
		if (!fu_util_update_device_with_release (priv, dev, rel, error))
			return FALSE;
		if (fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_NEEDS_REBOOT))
			requires_reboot = TRUE;
	}

	/* we don't want to ask anything */
	if (priv->no_reboot_check) {
		g_debug ("skipping reboot check");
		return TRUE;
	}

	/* at least one of the updates needed a reboot */
	if (requires_reboot) {
		g_print ("\n%s %s [Y|n]: ",
			 /* TRANSLATORS: explain why we want to upload */
			 _("An update requires a reboot to complete."),
			 /* TRANSLATORS: reboot to apply the update */
			 _("Restart now?"));
		if (!fu_util_prompt_for_boolean (TRUE))
			return TRUE;
		return fu_util_update_reboot (error);
	}
	return TRUE;
}

static gboolean
fu_util_remote_modify (FuUtilPrivate *priv, gchar **values, GError **error)
{
	if (g_strv_length (values) < 3) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments");
		return FALSE;
	}
	return fu_util_modify_remote (priv, values[0], values[1], values[2], error);
}

static gboolean
fu_util_remote_enable (FuUtilPrivate *priv, gchar **values, GError **error)
{
	if (g_strv_length (values) != 1) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments");
		return FALSE;
	}
	return fu_util_modify_remote (priv, values[0], "Enabled", "true", error);
}

static gboolean
fu_util_remote_disable (FuUtilPrivate *priv, gchar **values, GError **error)
{
	if (g_strv_length (values) != 1) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments");
		return FALSE;
	}
	return fu_util_modify_remote (priv, values[0], "Enabled", "false", error);
}

static gboolean
fu_util_downgrade (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FwupdDevice) dev = NULL;
	g_autoptr(FwupdRelease) rel = NULL;
	g_autoptr(GPtrArray) rels = NULL;

	dev = fu_util_get_device_or_prompt (priv, values, error);
	if (dev == NULL)
		return FALSE;

	/* get the releases for this device and filter for validity */
	rels = fwupd_client_get_downgrades (priv->client,
					    fwupd_device_get_id (dev),
					    NULL, error);
	if (rels == NULL)
		return FALSE;

	/* get the chosen release */
	rel = fu_util_prompt_for_release (priv, rels, error);
	if (rel == NULL)
		return FALSE;
	priv->flags |= FWUPD_INSTALL_FLAG_ALLOW_OLDER;
	return fu_util_update_device_with_release (priv, dev, rel, error);
}

static gboolean
fu_util_hwids (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FuSmbios) smbios = fu_smbios_new ();
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

	/* read DMI data */
	if (!fu_smbios_setup (smbios, error))
		return FALSE;
	if (!fu_hwids_setup (hwids, smbios, error))
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
	if (priv->soup_session != NULL)
		g_object_unref (priv->soup_session);
	g_main_loop_unref (priv->loop);
	g_object_unref (priv->cancellable);
	g_object_unref (priv->progressbar);
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
	gboolean force = FALSE;
	gboolean allow_older = FALSE;
	gboolean allow_reinstall = FALSE;
	gboolean no_history = FALSE;
	gboolean offline = FALSE;
	gboolean ret;
	gboolean verbose = FALSE;
	gboolean version = FALSE;
	g_autoptr(FuUtilPrivate) priv = g_new0 (FuUtilPrivate, 1);
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
		{ "assume-yes", 'y', 0, G_OPTION_ARG_NONE, &priv->assume_yes,
			/* TRANSLATORS: command line option */
			_("Answer yes to all questions"), NULL },
		{ "no-unreported-check", '\0', 0, G_OPTION_ARG_NONE, &priv->no_unreported_check,
			/* TRANSLATORS: command line option */
			_("Do not check for unreported history"), NULL },
		{ "no-metadata-check", '\0', 0, G_OPTION_ARG_NONE, &priv->no_metadata_check,
			/* TRANSLATORS: command line option */
			_("Do not check for old metadata"), NULL },
		{ "no-reboot-check", '\0', 0, G_OPTION_ARG_NONE, &priv->no_reboot_check,
			/* TRANSLATORS: command line option */
			_("Do not check for reboot after update"), NULL },
		{ "no-history", '\0', 0, G_OPTION_ARG_NONE, &no_history,
			/* TRANSLATORS: command line option */
			_("Do not write to the history database"), NULL },
		{ "show-all-devices", '\0', 0, G_OPTION_ARG_NONE, &priv->show_all_devices,
			/* TRANSLATORS: command line option */
			_("Show devices that are not updatable"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* ensure D-Bus errors are registered */
	fwupd_error_quark ();

	/* create helper object */
	priv->loop = g_main_loop_new (NULL, FALSE);
	priv->progressbar = fu_progressbar_new ();

	/* add commands */
	priv->cmd_array = g_ptr_array_new_with_free_func ((GDestroyNotify) fu_util_item_free);
	fu_util_add (priv->cmd_array,
		     "get-devices",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Get all devices that support firmware updates"),
		     fu_util_get_devices);
	fu_util_add (priv->cmd_array,
		     "get-topology",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Get all devices according to the system topology"),
		     fu_util_get_topology);
	fu_util_add (priv->cmd_array,
		     "hwids",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Return all the hardware IDs for the machine"),
		     fu_util_hwids);
	fu_util_add (priv->cmd_array,
		     "install-prepared",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Install prepared updates now"),
		     fu_util_install_prepared);
	fu_util_add (priv->cmd_array,
		     "get-history",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Show history of firmware updates"),
		     fu_util_get_history);
	fu_util_add (priv->cmd_array,
		     "clear-history",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Erase all firmware update history"),
		     fu_util_clear_history);
	fu_util_add (priv->cmd_array,
		     "report-history",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Share firmware history with the developers"),
		     fu_util_report_history);
	fu_util_add (priv->cmd_array,
		     "install",
		     "FILE [ID]",
		     /* TRANSLATORS: command description */
		     _("Install a firmware file on this hardware"),
		     fu_util_install);
	fu_util_add (priv->cmd_array,
		     "get-details",
		     "FILE",
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
		     "[DEVICE_ID]",
		     /* TRANSLATORS: command description */
		     _("Gets the cryptographic hash of the dumped firmware"),
		     fu_util_verify);
	fu_util_add (priv->cmd_array,
		     "unlock",
		     "DEVICE_ID",
		     /* TRANSLATORS: command description */
		     _("Unlocks the device for firmware access"),
		     fu_util_unlock);
	fu_util_add (priv->cmd_array,
		     "clear-results",
		     "DEVICE_ID",
		     /* TRANSLATORS: command description */
		     _("Clears the results from the last update"),
		     fu_util_clear_results);
	fu_util_add (priv->cmd_array,
		     "clear-offline",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Clears any updates scheduled to be updated offline"),
		     fu_util_clear_offline);
	fu_util_add (priv->cmd_array,
		     "get-results",
		     "DEVICE_ID",
		     /* TRANSLATORS: command description */
		     _("Gets the results from the last update"),
		     fu_util_get_results);
	fu_util_add (priv->cmd_array,
		     "get-releases",
		     "[DEVICE_ID]",
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
		     "[DEVICE_ID]",
		     /* TRANSLATORS: command description */
		     _("Downgrades the firmware on a device"),
		     fu_util_downgrade);
	fu_util_add (priv->cmd_array,
		     "refresh",
		     "[FILE FILE_SIG REMOTE_ID]",
		     /* TRANSLATORS: command description */
		     _("Refresh metadata from remote server"),
		     fu_util_refresh);
	fu_util_add (priv->cmd_array,
		     "verify-update",
		     "[DEVICE_ID]",
		     /* TRANSLATORS: command description */
		     _("Update the stored metadata with current ROM contents"),
		     fu_util_verify_update);
	fu_util_add (priv->cmd_array,
		     "monitor",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Monitor the daemon for events"),
		     fu_util_monitor);
	fu_util_add (priv->cmd_array,
		     "build-firmware",
		     "FILE-IN FILE-OUT [SCRIPT] [OUTPUT]",
		     /* TRANSLATORS: command description */
		     _("Build firmware using a sandbox"),
		     fu_util_firmware_builder);
	fu_util_add (priv->cmd_array,
		     "modify-remote",
		     "REMOTE-ID KEY VALUE",
		     /* TRANSLATORS: command description */
		     _("Modifies a given remote"),
		     fu_util_remote_modify);
	fu_util_add (priv->cmd_array,
		     "enable-remote",
		     "REMOTE-ID",
		     /* TRANSLATORS: command description */
		     _("Enables a given remote"),
		     fu_util_remote_enable);
	fu_util_add (priv->cmd_array,
		     "disable-remote",
		     "REMOTE-ID",
		     /* TRANSLATORS: command description */
		     _("Disables a given remote"),
		     fu_util_remote_disable);

	/* do stuff on ctrl+c */
	priv->cancellable = g_cancellable_new ();
	g_unix_signal_add_full (G_PRIORITY_DEFAULT,
				SIGINT, fu_util_sigint_cb,
				priv, NULL);

	/* sort by command name */
	g_ptr_array_sort (priv->cmd_array,
			  (GCompareFunc) fu_sort_command_name_cb);

	/* non-TTY consoles cannot answer questions */
	if (isatty (fileno (stdout)) == 0) {
		priv->no_unreported_check = TRUE;
		priv->no_metadata_check = TRUE;
		priv->no_reboot_check = TRUE;
	}

	/* get a list of the commands */
	priv->context = g_option_context_new (NULL);
	cmd_descriptions = fu_util_get_descriptions (priv->cmd_array);
	g_option_context_set_summary (priv->context, cmd_descriptions);
	g_option_context_set_description (priv->context,
		"This tool allows an administrator to query and control the "
		"fwupd daemon, allowing them to perform actions such as "
		"installing or downgrading firmware.");

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
	if (no_history)
		priv->flags |= FWUPD_INSTALL_FLAG_NO_HISTORY;

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
#ifdef LIBFWUP_LIBRARY_VERSION
		g_print ("\tfwupdate:\t%s\n",
			LIBFWUP_LIBRARY_VERSION);
#endif
#ifdef EFIVAR_LIBRARY_VERSION
		g_print ("\tefivar:\t%s\n",
			EFIVAR_LIBRARY_VERSION);
#endif
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
