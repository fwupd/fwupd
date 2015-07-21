/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
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

#include <appstream-glib.h>
#include <fwupd.h>
#include <gio/gio.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "fu-cleanup.h"
#include "fu-cab.h"
#include "fu-debug.h"
#include "fu-keyring.h"

typedef struct {
	gchar			*source;
	gchar			*destination;
	gchar			*key_id;
	gboolean		 set_owner;
	FuKeyring		*keyring;
} FuSignPrivate;

/**
 * fu_sign_process_file_cab:
 **/
static gboolean
fu_sign_process_file_cab (FuSignPrivate *priv,
			  const gchar *fn_src,
			  const gchar *fn_dst,
			  GError **error)
{
	gsize fw_len = 0;
	_cleanup_bytes_unref_ GBytes *fw = NULL;
	_cleanup_bytes_unref_ GBytes *sig = NULL;
	_cleanup_free_ gchar *fw_data = NULL;
	_cleanup_free_ gchar *fn_bin_asc = NULL;
	_cleanup_object_unref_ FuCab *cab = NULL;
	_cleanup_object_unref_ GFile *src = NULL;
	_cleanup_object_unref_ GFile *dst = NULL;
	const gchar *fn_bin;

	g_info ("processing %s", fn_src);

	/* open the .cab file and extract firmware */
	cab = fu_cab_new ();
	src = g_file_new_for_path (fn_src);
	if (!fu_cab_load_file (cab, src, NULL, error))
		return FALSE;
	if (!fu_cab_extract (cab, FU_CAB_EXTRACT_FLAG_ALL, error))
		return FALSE;

	/* sign the .bin */
	fn_bin = fu_cab_get_filename_firmware (cab);
	g_info ("signing %s with key %s", fn_bin, priv->key_id);
	if (!g_file_get_contents (fn_bin, &fw_data, &fw_len, error))
		return FALSE;
	fw = g_bytes_new_static (fw_data, fw_len);
	sig = fu_keyring_sign_data (priv->keyring, fw, error);
	if (sig == NULL)
		return FALSE;

	/* write new detached signature */
	fn_bin_asc = g_strdup_printf ("%s.asc", fn_bin);
	g_debug ("writing to %s", fn_bin_asc);
	if (!g_file_set_contents (fn_bin_asc,
				  g_bytes_get_data (sig, NULL),
				  g_bytes_get_size (sig),
				  error))
		return FALSE;

	/* add to file list */
	fu_cab_add_file (cab, fn_bin_asc);

	/* add the detached signature to the .cab file */
	g_debug ("saving %s", fn_dst);
	dst = g_file_new_for_path (fn_dst);
	if (!fu_cab_save_file (cab, dst, NULL, error))
		return FALSE;

	/* delete the working space */
	g_unlink (fn_bin_asc);
	if (!fu_cab_delete_temp_files (cab, error))
		return FALSE;
	return TRUE;
}

/**
 * fu_sign_process_file_xml:
 **/
static gboolean
fu_sign_process_file_xml (FuSignPrivate *priv,
			  const gchar *fn_src,
			  const gchar *fn_dst,
			  GError **error)
{
	gsize xml_len = 0;
	_cleanup_bytes_unref_ GBytes *xml = NULL;
	_cleanup_bytes_unref_ GBytes *sig = NULL;
	_cleanup_free_ gchar *xml_data = NULL;

	/* sign the firmware */
	g_info ("signing %s with key %s", fn_src, priv->key_id);
	if (!g_file_get_contents (fn_src, &xml_data, &xml_len, error))
		return FALSE;
	xml = g_bytes_new_static (xml_data, xml_len);
	sig = fu_keyring_sign_data (priv->keyring, xml, error);
	if (sig == NULL)
		return FALSE;

	/* write new detached signature */
	g_debug ("writing to %s", fn_dst);
	if (!g_file_set_contents (fn_dst,
				  g_bytes_get_data (sig, NULL),
				  g_bytes_get_size (sig),
				  error))
		return FALSE;
	return TRUE;
}

/**
 * fu_sign_process_file:
 **/
static gboolean
fu_sign_process_file (FuSignPrivate *priv,
		      const gchar *fn_src,
		      GError **error)
{
	guint32 uid;
	guint32 gid;
	gboolean ret = FALSE;
	_cleanup_free_ gchar *fn_dst = NULL;
	_cleanup_free_ gchar *basename = NULL;
	_cleanup_object_unref_ GFileInfo *info = NULL;
	_cleanup_object_unref_ GFile *src = NULL;

	/* get the file owner */
	src = g_file_new_for_path (fn_src);
	info = g_file_query_info (src,
				  G_FILE_ATTRIBUTE_UNIX_UID ","
				  G_FILE_ATTRIBUTE_UNIX_GID ","
				  G_FILE_ATTRIBUTE_OWNER_USER ","
				  G_FILE_ATTRIBUTE_OWNER_GROUP,
				  G_FILE_QUERY_INFO_NONE,
				  NULL, error);
	if (info == NULL)
		return FALSE;

	/* process these in different ways */
	if (g_str_has_suffix (fn_src, ".cab")) {
		/* cab file */
		basename = g_path_get_basename (fn_src);
		fn_dst = g_build_filename (priv->destination, basename, NULL);
		ret = fu_sign_process_file_cab (priv, fn_src, fn_dst, error);
	} else if (as_app_guess_source_kind (fn_src) == AS_APP_SOURCE_KIND_APPSTREAM) {
		/* AppStream metadata */
		basename = g_path_get_basename (fn_src);
		fn_dst = g_strdup_printf ("%s/%s.asc", priv->destination, basename);
		ret = fu_sign_process_file_xml (priv, fn_src, fn_dst, error);
	} else {
		/* unknown */
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "unknown file type");
	}
	if (!ret)
		return FALSE;

	/* set the owner:group on the new file */
	if (priv->set_owner) {
		uid = g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_UID);
		gid = g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_GID);
		g_debug ("Attempting to set unix owner to %s:%s",
			 g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_OWNER_USER),
			 g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_OWNER_GROUP));
		chown (fn_dst, uid, gid);
	}

	/* only delete the source file if *everything* worked */
	g_debug ("deleting %s", fn_src);
	g_unlink (fn_src);

	return TRUE;
}

/**
 * fu_sign_coldplug:
 **/
static gboolean
fu_sign_coldplug (FuSignPrivate *priv, GError **error)
{
	_cleanup_dir_close_ GDir *dir = NULL;

	dir = g_dir_open (priv->source, 0, error);
	if (dir == NULL)
		return FALSE;
	while (TRUE) {
		const gchar *fn;
		_cleanup_free_ gchar *fn_src = NULL;
		fn = g_dir_read_name (dir);
		if (fn == NULL)
			break;
		fn_src = g_build_filename (priv->source, fn, NULL);
		if (!fu_sign_process_file (priv, fn_src, error))
			return FALSE;
	}
	return TRUE;
}

/**
 * fu_sign_monitor_changed_cb:
 **/
static void
fu_sign_monitor_changed_cb (GFileMonitor *monitor,
			    GFile *file, GFile *other_file,
			    GFileMonitorEvent event_type,
			    FuSignPrivate *priv)
{
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_free_ gchar *fn_src = NULL;

	/* only new files */
	if (event_type != G_FILE_MONITOR_EVENT_CREATED)
		return;
	fn_src = g_file_get_path (file);
	if (fn_src == NULL)
		return;

	/* process file */
	if (!fu_sign_process_file (priv, fn_src, &error)) {
		g_warning ("failed to process %s: %s", fn_src, error->message);
		return;
	}
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	FuSignPrivate *priv = NULL;
	GOptionContext *context;
	GMainLoop *loop = NULL;
	gboolean ret;
	gboolean one_shot = FALSE;
	guint retval = 1;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_free_ gchar *config_file = NULL;
	_cleanup_free_ gchar *destination = NULL;
	_cleanup_free_ gchar *key_id = NULL;
	_cleanup_free_ gchar *source = NULL;
	_cleanup_keyfile_unref_ GKeyFile *config = NULL;
	const GOptionEntry options[] = {
		{ "one-shot", '\0', 0, G_OPTION_ARG_NONE, &one_shot,
		  /* TRANSLATORS: exit after we've started up, used for user profiling */
		  _("Exit after signing queue"), NULL },
		{ "source", 's', 0, G_OPTION_ARG_FILENAME, &source,
		  /* TRANSLATORS: input location to watch  */
		  _("Source path for files"), NULL },
		{ "destination", 'd', 0, G_OPTION_ARG_FILENAME, &destination,
		  /* TRANSLATORS: output location to put files  */
		  _("Destination path for files"), NULL },
		{ "key-id", 'd', 0, G_OPTION_ARG_STRING, &key_id,
		  /* TRANSLATORS: output location to put files  */
		  _("GPG key used to sign the firmware"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* TRANSLATORS: program name */
	g_set_application_name (_("fwsignd"));
	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_add_group (context, fu_debug_get_option_group ());
	/* TRANSLATORS: program summary */
	g_option_context_set_summary (context, _("Firmware signing server"));
	ret = g_option_context_parse (context, &argc, &argv, &error);
	if (!ret) {
		g_print ("failed to parse command line arguments: %s\n",
			  error->message);
		retval = EXIT_FAILURE;
		goto out;
	}

	/* fall back to keyfile */
	config = g_key_file_new ();
	config_file = g_build_filename (SYSCONFDIR, "fwsignd.conf", NULL);
	g_debug ("Loading fallback values from %s", config_file);
	if (!g_key_file_load_from_file (config, config_file,
					G_KEY_FILE_NONE, &error)) {
		g_print ("failed to load config file %s: %s\n",
			  config_file, error->message);
		retval = EXIT_FAILURE;
		goto out;
	}
	if (source == NULL)
		source = g_key_file_get_string (config, "fwupd", "SourceDirectory", NULL);
	if (destination == NULL)
		destination = g_key_file_get_string (config, "fwupd", "DestinationDirectory", NULL);
	if (key_id == NULL)
		key_id = g_key_file_get_string (config, "fwupd", "KeyID", NULL);
	if (source == NULL || destination == NULL) {
		g_print ("source and destination required\n");
		retval = EXIT_FAILURE;
		goto out;
	}
	priv = g_new0 (FuSignPrivate, 1);
	priv->source = g_strdup (source);
	priv->destination = g_strdup (destination);
	priv->key_id = g_strdup (key_id);
	priv->keyring = fu_keyring_new ();
	priv->set_owner = g_key_file_get_boolean (config, "fwupd", "SetDestinationOwner", NULL);
	if (priv->key_id != NULL &&
	    !fu_keyring_set_signing_key (priv->keyring, priv->key_id, &error)) {
		g_print ("valid GPG key required: %s\n", error->message);
		retval = EXIT_FAILURE;
		goto out;
	}

	/* process */
	g_debug ("clearing queue");
	if (!fu_sign_coldplug (priv, &error)) {
		g_print ("failed to clear queue: %s\n", error->message);
		retval = EXIT_FAILURE;
		goto out;
	}
	if (!one_shot) {
		_cleanup_object_unref_ GFileMonitor *monitor = NULL;
		_cleanup_object_unref_ GFile *src = NULL;
		g_debug ("waiting for files to appear in %s", source);
		src = g_file_new_for_path (source);
		monitor = g_file_monitor (src, G_FILE_MONITOR_NONE, NULL, &error);
		if (monitor == NULL) {
			g_print ("failed to watch %s: %s\n", source, error->message);
			retval = EXIT_FAILURE;
			goto out;
		}
		g_signal_connect (monitor, "changed",
				  G_CALLBACK (fu_sign_monitor_changed_cb), priv);
		loop = g_main_loop_new (NULL, FALSE);
		g_main_loop_run (loop);
	}

	/* success */
	retval = 0;
out:
	if (priv != NULL) {
		g_object_unref (priv->keyring);
		g_free (priv->source);
		g_free (priv->destination);
		g_free (priv->key_id);
		g_free (priv);
	}
	g_option_context_free (context);
	if (loop != NULL)
		g_main_loop_unref (loop);
	return retval;
}

