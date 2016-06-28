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
#include "fu-provider.h"
#include "fu-rom.h"

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

/**
 * fu_util_item_free:
 **/
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

/**
 * fu_util_add:
 **/
static void
fu_util_add (GPtrArray *array,
	     const gchar *name,
	     const gchar *arguments,
	     const gchar *description,
	     FuUtilPrivateCb callback)
{
	guint i;
	FuUtilItem *item;
	g_auto(GStrv) names = NULL;

	g_return_if_fail (name != NULL);
	g_return_if_fail (description != NULL);
	g_return_if_fail (callback != NULL);

	/* add each one */
	names = g_strsplit (name, ",", -1);
	for (i = 0; names[i] != NULL; i++) {
		item = g_new0 (FuUtilItem, 1);
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

/**
 * fu_util_get_descriptions:
 **/
static gchar *
fu_util_get_descriptions (GPtrArray *array)
{
	guint i;
	guint j;
	guint len;
	const guint max_len = 35;
	FuUtilItem *item;
	GString *string;

	/* print each command */
	string = g_string_new ("");
	for (i = 0; i < array->len; i++) {
		item = g_ptr_array_index (array, i);
		g_string_append (string, "  ");
		g_string_append (string, item->name);
		len = strlen (item->name) + 2;
		if (item->arguments != NULL) {
			g_string_append (string, " ");
			g_string_append (string, item->arguments);
			len += strlen (item->arguments) + 1;
		}
		if (len < max_len) {
			for (j = len; j < max_len + 1; j++)
				g_string_append_c (string, ' ');
			g_string_append (string, item->description);
			g_string_append_c (string, '\n');
		} else {
			g_string_append_c (string, '\n');
			for (j = 0; j < max_len + 1; j++)
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

/**
 * fu_util_run:
 **/
static gboolean
fu_util_run (FuUtilPrivate *priv, const gchar *command, gchar **values, GError **error)
{
	guint i;
	FuUtilItem *item;

	/* find command */
	for (i = 0; i < priv->cmd_array->len; i++) {
		item = g_ptr_array_index (priv->cmd_array, i);
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

/**
 * fu_util_status_changed_cb:
 **/
static void
fu_util_status_changed_cb (FwupdClient *client,
			   FwupdStatus status,
			   FuUtilPrivate *priv)
{
	switch (status) {
	case FWUPD_STATUS_IDLE:
		/* TRANSLATORS: daemon is inactive */
		g_print (" * %s\n", _("Idle"));
		break;
	case FWUPD_STATUS_DECOMPRESSING:
		/* TRANSLATORS: decompressing the firmware file */
		g_print (" * %s\n", _("Decompressing firmware"));
		break;
	case FWUPD_STATUS_LOADING:
		/* TRANSLATORS: parsing the firmware information */
		g_print (" * %s\n", _("Loading firmware"));
		break;
	case FWUPD_STATUS_DEVICE_RESTART:
		/* TRANSLATORS: restarting the device to pick up new F/W */
		g_print (" * %s\n", _("Restarting device"));
		break;
	case FWUPD_STATUS_DEVICE_WRITE:
		/* TRANSLATORS: writing to the flash chips */
		g_print (" * %s\n", _("Writing firmware to device"));
		break;
	case FWUPD_STATUS_DEVICE_VERIFY:
		/* TRANSLATORS: verifying we wrote the firmware correctly */
		g_print (" * %s\n", _("Verifying firmware from device"));
		break;
	case FWUPD_STATUS_SCHEDULING:
		/* TRANSLATORS: scheduing an update to be done on the next boot */
		g_print (" * %s\n", _("Scheduling upgrade"));
		break;
	default:
		break;
	}
}

/**
 * fu_util_get_devices:
 **/
static gboolean
fu_util_get_devices (FuUtilPrivate *priv, gchar **values, GError **error)
{
	FwupdResult *res;
	guint i;
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

	for (i = 0; i < results->len; i++) {
		g_autofree gchar *tmp = NULL;
		res = g_ptr_array_index (results, i);
		tmp = fwupd_result_to_string (res);
		g_print ("%s\n", tmp);
	}

	return TRUE;
}

/**
 * fu_util_install_with_fallback:
 **/
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

	/* TRANSLATOR: the provider only supports offline */
	g_print ("%s...\n", _("Retrying as an offline update"));
	priv->flags |= FWUPD_INSTALL_FLAG_OFFLINE;
	return fwupd_client_install (priv->client, id, filename, priv->flags,
				     NULL, error);
}

/**
 * fu_util_install:
 **/
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

/**
 * fu_util_get_details:
 **/
static gboolean
fu_util_get_details (FuUtilPrivate *priv, gchar **values, GError **error)
{
	guint i;
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
	for (i = 0; i < array->len; i++) {
		FwupdResult *res = g_ptr_array_index (array, i);
		g_autofree gchar *tmp = NULL;
		tmp = fwupd_result_to_string (res);
		g_print ("%s", tmp);
	}
	return TRUE;
}

/**
 * fu_util_offline_update_reboot:
 **/
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

/**
 * fu_util_install_prepared:
 **/
static gboolean
fu_util_install_prepared (FuUtilPrivate *priv, gchar **values, GError **error)
{
	gint vercmp;
	guint cnt = 0;
	guint i;
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
	for (i = 0; i < results->len; i++) {
		FwupdResult *res;
		res = g_ptr_array_index (results, i);

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

/**
 * fu_util_clear_results:
 **/
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

/**
 * fu_util_dump_rom:
 **/
static gboolean
fu_util_dump_rom (FuUtilPrivate *priv, gchar **values, GError **error)
{
	guint i;

	if (g_strv_length (values) == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments: expected 'filename.rom'");
		return FALSE;
	}
	for (i = 0; values[i] != NULL; i++) {
		g_autoptr(FuRom) rom = NULL;
		g_autoptr(GFile) file = NULL;
		g_autoptr(GError) error_local = NULL;

		file = g_file_new_for_path (values[i]);
		rom = fu_rom_new ();
		g_print ("%s:\n", values[i]);
		if (!fu_rom_load_file (rom, file, FU_ROM_LOAD_FLAG_BLANK_PPID,
				       NULL, &error_local)) {
			g_print ("%s\n", error_local->message);
			continue;
		}
		g_print ("0x%04x:0x%04x -> %s [%s]\n",
			 fu_rom_get_vendor (rom),
			 fu_rom_get_model (rom),
			 fu_rom_get_checksum (rom),
			 fu_rom_get_version (rom));
	}
	return TRUE;
}

/**
 * fu_util_verify_update_internal:
 **/
static gboolean
fu_util_verify_update_internal (FuUtilPrivate *priv,
				const gchar *filename,
				gchar **values,
				GError **error)
{
	guint i;
	g_autoptr(AsStore) store = NULL;
	g_autoptr(GFile) xml_file = NULL;

	store = as_store_new ();

	/* open existing file */
	xml_file = g_file_new_for_path (filename);
	if (g_file_query_exists (xml_file, NULL)) {
		if (!as_store_from_file (store, xml_file, NULL, NULL, error))
			return FALSE;
	}

	/* add new values */
	as_store_set_api_version (store, 0.9);
	for (i = 0; values[i] != NULL; i++) {
		g_autofree gchar *id = NULL;
		g_autoptr(AsApp) app = NULL;
		g_autoptr(AsChecksum) csum = NULL;
		g_autoptr(AsRelease) rel = NULL;
		g_autoptr(AsProvide) prov = NULL;
		g_autoptr(FuRom) rom = NULL;
		g_autoptr(GFile) file = NULL;
		g_autoptr(GError) error_local = NULL;

		file = g_file_new_for_path (values[i]);
		rom = fu_rom_new ();
		g_print ("Processing %s...\n", values[i]);
		if (!fu_rom_load_file (rom, file, FU_ROM_LOAD_FLAG_BLANK_PPID,
				       NULL, &error_local)) {
			g_print ("%s\n", error_local->message);
			continue;
		}

		/* make a plausible ID */
		id = g_strdup_printf ("%s.firmware", fu_rom_get_guid (rom));

		/* add app to store */
		app = as_app_new ();
		as_app_set_id (app, id);
		as_app_set_kind (app, AS_APP_KIND_FIRMWARE);
		as_app_set_source_kind (app, AS_APP_SOURCE_KIND_INF);
		rel = as_release_new ();
		as_release_set_version (rel, fu_rom_get_version (rom));
		csum = as_checksum_new ();
		as_checksum_set_kind (csum, fu_rom_get_checksum_kind (rom));
		as_checksum_set_value (csum, fu_rom_get_checksum (rom));
		as_checksum_set_target (csum, AS_CHECKSUM_TARGET_CONTENT);
		as_release_add_checksum (rel, csum);
		as_app_add_release (app, rel);
		prov = as_provide_new ();
		as_provide_set_kind (prov, AS_PROVIDE_KIND_FIRMWARE_FLASHED);
		as_provide_set_value (prov, fu_rom_get_guid (rom));
		as_app_add_provide (app, prov);
		as_store_add_app (store, app);
	}
	if (!as_store_to_file (store, xml_file,
			       AS_NODE_TO_XML_FLAG_ADD_HEADER |
			       AS_NODE_TO_XML_FLAG_FORMAT_INDENT |
			       AS_NODE_TO_XML_FLAG_FORMAT_MULTILINE,
			       NULL, error))
		return FALSE;
	return TRUE;
}

/**
 * fu_util_verify_update_all:
 **/
static gboolean
fu_util_verify_update_all (FuUtilPrivate *priv, const gchar *fn, GError **error)
{
	GList *devices;
	GList *l;
	GUdevDevice *dev;
	const gchar *devclass[] = { "pci", NULL };
	const gchar *subsystems[] = { NULL };
	guint i;
	g_autoptr(GUdevClient) gudev_client = NULL;
	g_autoptr(GPtrArray) roms = NULL;

	/* get all devices of class */
	gudev_client = g_udev_client_new (subsystems);
	roms = g_ptr_array_new_with_free_func (g_free);
	for (i = 0; devclass[i] != NULL; i++) {
		devices = g_udev_client_query_by_subsystem (gudev_client,
							    devclass[i]);
		for (l = devices; l != NULL; l = l->next) {
			g_autofree gchar *rom_fn = NULL;
			dev = l->data;
			rom_fn = g_build_filename (g_udev_device_get_sysfs_path (dev), "rom", NULL);
			if (!g_file_test (rom_fn, G_FILE_TEST_EXISTS))
				continue;
			g_ptr_array_add (roms, g_strdup (rom_fn));
		}
		g_list_foreach (devices, (GFunc) g_object_unref, NULL);
		g_list_free (devices);
	}

	/* no ROMs to add */
	if (roms->len == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOTHING_TO_DO,
				     "No hardware with ROM");
		return FALSE;
	}
	g_ptr_array_add (roms, NULL);
	return fu_util_verify_update_internal (priv, fn,
					       (gchar **) roms->pdata,
					       error);
}

/**
 * fu_util_verify_update:
 **/
static gboolean
fu_util_verify_update (FuUtilPrivate *priv, gchar **values, GError **error)
{
	const gchar *fn = "/var/cache/app-info/xmls/fwupd-verify.xml";
	if (g_strv_length (values) == 0)
		return fu_util_verify_update_all (priv, fn, error);
	if (g_strv_length (values) == 1)
		return fu_util_verify_update_all (priv, values[0], error);
	return fu_util_verify_update_internal (priv,
					       values[0],
					       &values[1],
					       error);
}

/**
 * fu_util_download_file:
 **/
static gboolean
fu_util_download_file (FuUtilPrivate *priv,
		       const gchar *uri,
		       const gchar *fn,
		       const gchar *checksum_expected,
		       GChecksumType checksum_type,
		       GError **error)
{
	guint status_code;
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *checksum_actual = NULL;
	g_autofree gchar *user_agent = NULL;
	g_autoptr(SoupMessage) msg = NULL;
	g_autoptr(SoupSession) session = NULL;

	user_agent = g_strdup_printf ("%s/%s", PACKAGE_NAME, PACKAGE_VERSION);
	session = soup_session_new_with_options (SOUP_SESSION_USER_AGENT,
						 user_agent,
						 NULL);
	if (session == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "%s: failed to setup networking");
		return FALSE;
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
							       msg->response_body->length);
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

/**
 * fu_util_mkdir_with_parents:
 **/
static gboolean
fu_util_mkdir_with_parents (const gchar *path, GError **error)
{
	g_autoptr(GFile) file = g_file_new_for_path (path);
	if (g_file_query_exists (file, NULL))
		return TRUE;
	return g_file_make_directory_with_parents (file, NULL, error);
}

/**
 * fu_util_download_metadata:
 **/
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

/**
 * fu_util_refresh:
 **/
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

/**
 * fu_util_get_results:
 **/
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

/**
 * fu_util_verify_all:
 **/
static gboolean
fu_util_verify_all (FuUtilPrivate *priv, GError **error)
{
	FwupdResult *res;
	guint i;
	g_autoptr(GPtrArray) results = NULL;

	/* get devices from daemon */
	results = fwupd_client_get_devices (priv->client, NULL, error);
	if (results == NULL)
		return FALSE;

	/* get results */
	for (i = 0; i < results->len; i++) {
		g_autoptr(GError) error_local = NULL;
		res = g_ptr_array_index (results, i);
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

/**
 * fu_util_verify:
 **/
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

/**
 * fu_util_unlock:
 **/
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

/**
 * fu_util_print_data:
 **/
static void
fu_util_print_data (const gchar *title, const gchar *msg)
{
	guint i;
	guint j;
	guint title_len;
	g_auto(GStrv) lines = NULL;

	if (msg == NULL)
		return;
	g_print ("%s:", title);

	/* pad */
	title_len = strlen (title) + 1;
	lines = g_strsplit (msg, "\n", -1);
	for (j = 0; lines[j] != NULL; j++) {
		for (i = title_len; i < 25; i++)
			g_print (" ");
		g_print ("%s\n", lines[j]);
		title_len = 0;
	}
}

/**
 * _g_checksum_type_to_string:
 **/
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

/**
 * fu_util_get_updates:
 **/
static gboolean
fu_util_get_updates (FuUtilPrivate *priv, gchar **values, GError **error)
{
	FwupdResult *res;
	GPtrArray *results = NULL;
	GPtrArray *guids;
	GChecksumType checksum_type;
	const gchar *tmp;
	guint i, j;

	/* print any updates */
	results = fwupd_client_get_updates (priv->client, NULL, error);
	if (results == NULL)
		return FALSE;
	for (i = 0; i < results->len; i++) {
		res = g_ptr_array_index (results, i);

		/* TRANSLATORS: first replacement is device name */
		g_print (_("%s has firmware updates:"), fwupd_result_get_device_name (res));
		g_print ("\n");

		/* TRANSLATORS: Appstream ID for the hardware type */
		fu_util_print_data (_("ID"), fwupd_result_get_update_id (res));

		/* TRANSLATORS: a GUID for the hardware */
		guids = fwupd_result_get_guids (res);
		for (j = 0; j < guids->len; j++) {
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

/**
 * fu_util_cancelled_cb:
 **/
static void
fu_util_cancelled_cb (GCancellable *cancellable, gpointer user_data)
{
	FuUtilPrivate *priv = (FuUtilPrivate *) user_data;
	/* TRANSLATORS: this is when a device ctrl+c's a watch */
	g_print ("%s\n", _("Cancelled"));
	g_main_loop_quit (priv->loop);
}

/**
 * fu_util_device_added_cb:
 **/
static void
fu_util_device_added_cb (FwupdClient *client,
			 FwupdResult *device,
			 gpointer user_data)
{
	g_autofree gchar *tmp = fwupd_result_to_string (device);
	/* TRANSLATORS: this is when a device is hotplugged */
	g_print ("%s\n%s", _("Device added:"), tmp);
}

/**
 * fu_util_device_removed_cb:
 **/
static void
fu_util_device_removed_cb (FwupdClient *client,
			   FwupdResult *device,
			   gpointer user_data)
{
	g_autofree gchar *tmp = fwupd_result_to_string (device);
	/* TRANSLATORS: this is when a device is hotplugged */
	g_print ("%s\n%s", _("Device removed:"), tmp);
}

/**
 * fu_util_device_changed_cb:
 **/
static void
fu_util_device_changed_cb (FwupdClient *client,
			   FwupdResult *device,
			   gpointer user_data)
{
	g_autofree gchar *tmp = fwupd_result_to_string (device);
	/* TRANSLATORS: this is when a device has been updated */
	g_print ("%s\n%s", _("Device changed:"), tmp);
}

/**
 * fu_util_changed_cb:
 **/
static void
fu_util_changed_cb (FwupdClient *client, gpointer user_data)
{
	/* TRANSLATORS: this is when the daemon state changes */
	g_print ("%s\n", _("Changed"));
}

/**
 * fu_util_monitor:
 **/
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

/**
 * fu_util_update:
 **/
static gboolean
fu_util_update (FuUtilPrivate *priv, gchar **values, GError **error)
{
	FwupdResult *res;
	GPtrArray *results = NULL;
	guint i;

	/* apply any updates */
	results = fwupd_client_get_updates (priv->client, NULL, error);
	if (results == NULL)
		return FALSE;
	for (i = 0; i < results->len; i++) {
		GChecksumType checksum_type;
		const gchar *checksum;
		const gchar *uri;
		g_autofree gchar *basename = NULL;
		g_autofree gchar *fn = NULL;

		res = g_ptr_array_index (results, i);

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

/**
 * fu_util_ignore_cb:
 **/
static void
fu_util_ignore_cb (const gchar *log_domain, GLogLevelFlags log_level,
		   const gchar *message, gpointer user_data)
{
}

/**
 * fu_util_sigint_cb:
 **/
static gboolean
fu_util_sigint_cb (gpointer user_data)
{
	FuUtilPrivate *priv = (FuUtilPrivate *) user_data;
	g_debug ("Handling SIGINT");
	g_cancellable_cancel (priv->cancellable);
	return FALSE;
}

/**
 * main:
 **/
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
	guint retval = 1;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *cmd_descriptions = NULL;
	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			/* TRANSLATORS: command line option */
			_("Show extra debugging information"), NULL },
		{ "offline", '\0', 0, G_OPTION_ARG_NONE, &offline,
			/* TRANSLATORS: command line option */
			_("Perform the installation offline where possible"), NULL },
		{ "allow-reinstall", '\0', 0, G_OPTION_ARG_NONE, &allow_reinstall,
			/* TRANSLATORS: command line option */
			_("Allow re-installing existing firmware versions"), NULL },
		{ "allow-older", '\0', 0, G_OPTION_ARG_NONE, &allow_older,
			/* TRANSLATORS: command line option */
			_("Allow downgrading firmware versions"), NULL },
		{ "force", '\0', 0, G_OPTION_ARG_NONE, &force,
			/* TRANSLATORS: command line option */
			_("Override provider warning"), NULL },
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
		     "dump-rom",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Dump the ROM checksum"),
		     fu_util_dump_rom);
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
	g_signal_connect (priv->client, "status-changed",
			  G_CALLBACK (fu_util_status_changed_cb), priv);

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
	retval = 0;
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
	return retval;
}
