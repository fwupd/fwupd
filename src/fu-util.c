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

#include <fwupd.h>
#include <appstream-glib.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <glib/gi18n.h>
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

typedef struct {
	GMainLoop		*loop;
	GOptionContext		*context;
	GPtrArray		*cmd_array;
	GVariant		*val;		/* for async */
	GDBusMessage		*message;	/* for async */
	GError			*error;		/* for async */
	FuProviderFlags		 flags;
	GDBusConnection		*conn;
	GDBusProxy		*proxy;
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
			     FWUPD_ERROR_INTERNAL,
			     /* TRANSLATORS: error message */
			     _("Command not found"));
	return FALSE;
}

/**
 * fu_util_status_changed_cb:
 **/
static void
fu_util_status_changed_cb (GDBusProxy *proxy, GVariant *changed_properties,
			   GStrv invalidated_properties, gpointer user_data)
{
	g_autoptr(GVariant) val = NULL;

	/* print to the console */
	val = g_dbus_proxy_get_cached_property (proxy, "Status");
	if (val == NULL)
		return;
	switch (g_variant_get_uint32 (val)) {
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
 * fu_util_get_devices_cb:
 **/
static void
fu_util_get_devices_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	FuUtilPrivate *priv = (FuUtilPrivate *) user_data;
	priv->val = g_dbus_proxy_call_finish (G_DBUS_PROXY (source),
					      res, &priv->error);
	g_main_loop_quit (priv->loop);
}

/**
 * fu_util_get_devices_internal:
 **/
static GPtrArray *
fu_util_get_devices_internal (FuUtilPrivate *priv, GError **error)
{
	GVariantIter *iter_device;
	GPtrArray *devices = NULL;
	FuDevice *dev;
	gchar *id;
	g_autoptr(GVariantIter) iter = NULL;

	g_dbus_proxy_call (priv->proxy,
			   "GetDevices",
			   NULL,
			   G_DBUS_CALL_FLAGS_NONE,
			   -1,
			   NULL,
			   fu_util_get_devices_cb, priv);
	g_main_loop_run (priv->loop);
	if (priv->val == NULL) {
		g_propagate_error (error, priv->error);
		return NULL;
	}

	/* parse */
	g_variant_get (priv->val, "(a{sa{sv}})", &iter);
	devices = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	while (g_variant_iter_next (iter, "{&sa{sv}}", &id, &iter_device)) {
		dev = fu_device_new ();
		fu_device_set_id (dev, id);
		fu_device_set_metadata_from_iter (dev, iter_device);
		g_ptr_array_add (devices, dev);
		g_variant_iter_free (iter_device);
	}
	return devices;
}

/**
 * fu_util_get_updates_internal:
 **/
static GPtrArray *
fu_util_get_updates_internal (FuUtilPrivate *priv, GError **error)
{
	GVariantIter *iter_device;
	GPtrArray *devices = NULL;
	FuDevice *dev;
	gchar *id;
	g_autoptr(GVariantIter) iter = NULL;

	g_dbus_proxy_call (priv->proxy,
			   "GetUpdates",
			   NULL,
			   G_DBUS_CALL_FLAGS_NONE,
			   -1,
			   NULL,
			   fu_util_get_devices_cb, priv);
	g_main_loop_run (priv->loop);
	if (priv->val == NULL) {
		g_dbus_error_strip_remote_error (priv->error);
		g_propagate_error (error, priv->error);
		return NULL;
	}

	/* parse */
	g_variant_get (priv->val, "(a{sa{sv}})", &iter);
	devices = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	while (g_variant_iter_next (iter, "{&sa{sv}}", &id, &iter_device)) {
		dev = fu_device_new ();
		fu_device_set_id (dev, id);
		fu_device_set_metadata_from_iter (dev, iter_device);
		g_ptr_array_add (devices, dev);
		g_variant_iter_free (iter_device);
	}
	return devices;
}

/**
 * pad_print:
 **/
static void
pad_print (const gchar *key, const gchar *value)
{
	guint k;
	g_print ("  %s:", key);
	for (k = strlen (key); k < 15; k++)
		g_print (" ");
	g_print (" %s\n", value);
}

/**
 * fu_util_get_devices:
 **/
static gboolean
fu_util_get_devices (FuUtilPrivate *priv, gchar **values, GError **error)
{
	FuDevice *dev;
	g_autoptr(GPtrArray) devices = NULL;
	guint i;
	guint j;
	guint f;
	guint64 flags;
	const gchar *value;
	const gchar *keys[] = {
		FU_DEVICE_KEY_DISPLAY_NAME,
		FU_DEVICE_KEY_PROVIDER,
		FU_DEVICE_KEY_CREATED,
		FU_DEVICE_KEY_MODIFIED,
		FU_DEVICE_KEY_APPSTREAM_ID,
		FU_DEVICE_KEY_GUID,
		FU_DEVICE_KEY_VERSION,
		FU_DEVICE_KEY_URL_HOMEPAGE,
		FU_DEVICE_KEY_NAME,
		FU_DEVICE_KEY_SUMMARY,
		FU_DEVICE_KEY_DESCRIPTION,
		FU_DEVICE_KEY_LICENSE,
		FU_DEVICE_KEY_FLAGS,
		FU_DEVICE_KEY_TRUSTED,
		FU_DEVICE_KEY_SIZE,
		FU_DEVICE_KEY_FIRMWARE_HASH,
		NULL };
	const gchar *flags_str[] = {
		"Internal",
		"AllowOnline",
		"AllowOffline",
		"RequireAc",
		"Locked",
		NULL };

	/* get devices from daemon */
	devices = fu_util_get_devices_internal (priv, error);
	if (devices == NULL)
		return FALSE;

	/* print */
	if (devices->len == 0) {
		/* TRANSLATORS: nothing attached that can be upgraded */
		g_print ("%s\n", _("No hardware detected with firmware update capability"));
		return TRUE;
	}

	for (i = 0; i < devices->len; i++) {
		dev = g_ptr_array_index (devices, i);
		g_print ("Device: %s\n", fu_device_get_id (dev));
		for (j = 0; keys[j] != NULL; j++) {
			if (g_strcmp0 (keys[j], FU_DEVICE_KEY_FLAGS) == 0) {
				flags = fu_device_get_flags (dev);
				for (f = 0; flags_str[f] != NULL; f++) {
					pad_print (flags_str[f],
						   flags & (1 << f) ? "True" : "False");
				}
				continue;
			}
			if (g_strcmp0 (keys[j], FU_DEVICE_KEY_CREATED) == 0) {
				g_autoptr(GDateTime) date = NULL;
				g_autofree gchar *date_str = NULL;
				date = g_date_time_new_from_unix_utc (fu_device_get_created (dev));
				date_str = g_date_time_format (date, "%F");
				pad_print (keys[j], date_str);
			}
			if (g_strcmp0 (keys[j], FU_DEVICE_KEY_MODIFIED) == 0) {
				g_autoptr(GDateTime) date = NULL;
				g_autofree gchar *date_str = NULL;
				if (fu_device_get_modified (dev) > 0) {
					date = g_date_time_new_from_unix_utc (fu_device_get_modified (dev));
					date_str = g_date_time_format (date, "%F");
					pad_print (keys[j], date_str);
				}
			}
			value = fu_device_get_metadata (dev, keys[j]);
			if (value != NULL)
				pad_print (keys[j], value);
		}
	}

	return TRUE;
}

/**
 * fu_util_update_cb:
 **/
static void
fu_util_update_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	FuUtilPrivate *priv = (FuUtilPrivate *) user_data;
	GDBusConnection *con = G_DBUS_CONNECTION (source_object);
	priv->message = g_dbus_connection_send_message_with_reply_finish (con, res,
									  &priv->error);
	g_main_loop_quit (priv->loop);
}

/**
 * fu_util_install_internal:
 **/
static gboolean
fu_util_install_internal (FuUtilPrivate *priv, const gchar *id,
			  const gchar *filename, GError **error)
{
	GVariant *body;
	GVariantBuilder builder;
	gint retval;
	gint fd;
	g_autoptr(GDBusMessage) request = NULL;
	g_autoptr(GUnixFDList) fd_list = NULL;

	/* set options */
	g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
	g_variant_builder_add (&builder, "{sv}",
			       "reason", g_variant_new_string ("user-action"));
	g_variant_builder_add (&builder, "{sv}",
			       "filename", g_variant_new_string (filename));
	if (priv->flags & FU_PROVIDER_UPDATE_FLAG_OFFLINE) {
		g_variant_builder_add (&builder, "{sv}",
				       "offline", g_variant_new_boolean (TRUE));
	}
	if (priv->flags & FU_PROVIDER_UPDATE_FLAG_ALLOW_OLDER) {
		g_variant_builder_add (&builder, "{sv}",
				       "allow-older", g_variant_new_boolean (TRUE));
	}
	if (priv->flags & FU_PROVIDER_UPDATE_FLAG_ALLOW_REINSTALL) {
		g_variant_builder_add (&builder, "{sv}",
				       "allow-reinstall", g_variant_new_boolean (TRUE));
	}

	/* open file */
	fd = open (filename, O_RDONLY);
	if (fd < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "failed to open %s",
			     filename);
		return FALSE;
	}

	/* set out of band file descriptor */
	fd_list = g_unix_fd_list_new ();
	retval = g_unix_fd_list_append (fd_list, fd, NULL);
	g_assert (retval != -1);
	request = g_dbus_message_new_method_call (FWUPD_DBUS_SERVICE,
						  FWUPD_DBUS_PATH,
						  FWUPD_DBUS_INTERFACE,
						  "Install");
	g_dbus_message_set_unix_fd_list (request, fd_list);

	/* g_unix_fd_list_append did a dup() already */
	close (fd);

	/* send message */
	body = g_variant_new ("(sha{sv})", id, fd > -1 ? 0 : -1, &builder);
	g_dbus_message_set_body (request, body);
	g_dbus_connection_send_message_with_reply (priv->conn,
						   request,
						   G_DBUS_SEND_MESSAGE_FLAGS_NONE,
						   -1,
						   NULL,
						   NULL,
						   fu_util_update_cb,
						   priv);
	g_main_loop_run (priv->loop);
	if (priv->message == NULL) {
		g_dbus_error_strip_remote_error (priv->error);
		g_propagate_error (error, priv->error);
		return FALSE;
	}
	if (g_dbus_message_to_gerror (priv->message, error)) {
		g_dbus_error_strip_remote_error (*error);
		return FALSE;
	}
	/* TRANSLATORS: update completed, no errors */
	g_print ("%s\n", _("Done!"));
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
	if (fu_util_install_internal (priv, id, filename, &error_local))
		return TRUE;

	/* some other failure */
	if ((priv->flags & FU_PROVIDER_UPDATE_FLAG_OFFLINE) > 0 ||
	    !g_error_matches (error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_propagate_error (error, error_local);
		error_local = NULL;
		return FALSE;
	}

	/* TRANSLATOR: the provider only supports offline */
	g_print ("%s...\n", _("Retrying as an offline update"));
	priv->flags |= FU_PROVIDER_UPDATE_FLAG_OFFLINE;
	return fu_util_install_internal (priv, id, filename, error);
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
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments: expected 'filename' [id]");
		return FALSE;
	}

	/* install with flags chosen by the user then falling back to offline */
	return fu_util_install_with_fallback (priv, id, values[0], error);
}

/**
 * fu_util_print_metadata:
 **/
static void
fu_util_print_metadata (GVariant *val)
{
	GVariant *variant;
	const gchar *key;
	const gchar *type;
	guint i;
	g_autoptr(GVariantIter) iter = NULL;

	g_variant_get (val, "(a{sv})", &iter);
	while (g_variant_iter_next (iter, "{&sv}", &key, &variant)) {
		g_print ("%s", key);
		for (i = strlen (key); i < 15; i++)
			g_print (" ");
		type = g_variant_get_type_string (variant);
		if (g_strcmp0 (type, "s") == 0) {
			g_print ("%s\n", g_variant_get_string (variant, NULL));
		} else if (g_strcmp0 (type, "b") == 0) {
			g_print ("%s\n", g_variant_get_boolean (variant) ? "True" : "False");
		} else if (g_strcmp0 (type, "t") == 0) {
			g_print ("%" G_GUINT64_FORMAT "\n", g_variant_get_uint64 (variant));
		} else {
			g_print ("???? [%s]\n", type);
		}
		g_variant_unref (variant);
	}
}

/**
 * fu_util_get_details:
 **/
static gboolean
fu_util_get_details (FuUtilPrivate *priv, gchar **values, GError **error)
{
	GVariant *body;
	GVariant *val;
	gint fd;
	gint retval;
	g_autoptr(GDBusMessage) message = NULL;
	g_autoptr(GDBusMessage) request = NULL;
	g_autoptr(GUnixFDList) fd_list = NULL;

	/* check args */
	if (g_strv_length (values) != 1) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments: expected 'filename'");
		return FALSE;
	}

	/* open file */
	fd = open (values[0], O_RDONLY);
	if (fd < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "failed to open %s",
			     values[0]);
		return FALSE;
	}

	/* set out of band file descriptor */
	fd_list = g_unix_fd_list_new ();
	retval = g_unix_fd_list_append (fd_list, fd, NULL);
	g_assert (retval != -1);
	request = g_dbus_message_new_method_call (FWUPD_DBUS_SERVICE,
						  FWUPD_DBUS_PATH,
						  FWUPD_DBUS_INTERFACE,
						  "GetDetails");
	g_dbus_message_set_unix_fd_list (request, fd_list);

	/* g_unix_fd_list_append did a dup() already */
	close (fd);

	/* send message */
	body = g_variant_new ("(h)", fd > -1 ? 0 : -1);
	g_dbus_message_set_body (request, body);
	message = g_dbus_connection_send_message_with_reply_sync (priv->conn,
								  request,
								  G_DBUS_SEND_MESSAGE_FLAGS_NONE,
								  -1,
								  NULL,
								  NULL,
								  error);
	if (message == NULL) {
		g_dbus_error_strip_remote_error (*error);
		return FALSE;
	}
	if (g_dbus_message_to_gerror (message, error)) {
		g_dbus_error_strip_remote_error (*error);
		return FALSE;
	}

	/* print results */
	val = g_dbus_message_get_body (message);
	fu_util_print_metadata (val);
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
	const gchar *tmp;
	g_autofree gchar *link = NULL;
	g_autoptr(GPtrArray) devices = NULL;
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
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments: none expected");
		return FALSE;
	}

	/* ensure root user */
	if (getuid () != 0 || geteuid () != 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "This function can only be used as root");
		return FALSE;
	}

	/* get prepared updates */
	pending = fu_pending_new ();
	devices = fu_pending_get_devices (pending, error);
	if (devices == NULL)
		return FALSE;

	/* apply each update */
	for (i = 0; i < devices->len; i++) {
		FuDevice *device;
		device = g_ptr_array_index (devices, i);

		/* check not already done */
		tmp = fu_device_get_metadata (device, FU_DEVICE_KEY_PENDING_STATE);
		if (g_strcmp0 (tmp, "scheduled") != 0)
			continue;

		/* tell the user what's going to happen */
		vercmp = as_utils_vercmp (fu_device_get_metadata (device, FU_DEVICE_KEY_VERSION),
					  fu_device_get_metadata (device, FU_DEVICE_KEY_UPDATE_VERSION));
		if (vercmp == 0) {
			/* TRANSLATORS: the first replacement is a display name
			 * e.g. "ColorHugALS" and the second is a version number
			 * e.g. "1.2.3" */
			g_print (_("Reinstalling %s with %s... "),
				 fu_device_get_display_name (device),
				 fu_device_get_metadata (device, FU_DEVICE_KEY_UPDATE_VERSION));
		} else if (vercmp > 0) {
			/* TRANSLATORS: the first replacement is a display name
			 * e.g. "ColorHugALS" and the second and third are
			 * version numbers e.g. "1.2.3" */
			g_print (_("Downgrading %s from %s to %s... "),
				 fu_device_get_display_name (device),
				 fu_device_get_metadata (device, FU_DEVICE_KEY_VERSION),
				 fu_device_get_metadata (device, FU_DEVICE_KEY_UPDATE_VERSION));
		} else if (vercmp < 0) {
			/* TRANSLATORS: the first replacement is a display name
			 * e.g. "ColorHugALS" and the second and third are
			 * version numbers e.g. "1.2.3" */
			g_print (_("Updating %s from %s to %s... "),
				 fu_device_get_display_name (device),
				 fu_device_get_metadata (device, FU_DEVICE_KEY_VERSION),
				 fu_device_get_metadata (device, FU_DEVICE_KEY_UPDATE_VERSION));
		}
		if (!fu_util_install_internal (priv,
					       fu_device_get_id (device),
					       fu_device_get_metadata (device, FU_DEVICE_KEY_FILENAME_CAB),
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
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments: expected 'id'");
		return FALSE;
	}

	/* clear results, and wait for reply */
	g_dbus_proxy_call (priv->proxy,
			   "ClearResults",
			   g_variant_new ("(s)", values[0]),
			   G_DBUS_CALL_FLAGS_NONE,
			   -1,
			   NULL,
			   fu_util_get_devices_cb, priv);
	g_main_loop_run (priv->loop);
	if (priv->val == NULL) {
		g_dbus_error_strip_remote_error (priv->error);
		g_propagate_error (error, priv->error);
		return FALSE;
	}
	return TRUE;
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
				     FWUPD_ERROR_INTERNAL,
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
		as_app_set_id_kind (app, AS_ID_KIND_FIRMWARE);
		as_app_set_source_kind (app, AS_APP_SOURCE_KIND_INF);
		rel = as_release_new ();
		as_release_set_version (rel, fu_rom_get_version (rom));
		csum = as_checksum_new ();
		as_checksum_set_kind (csum, G_CHECKSUM_SHA1);
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
 * fu_util_refresh_internal:
 **/
static gboolean
fu_util_refresh_internal (FuUtilPrivate *priv,
			  const gchar *data_fn,
			  const gchar *sig_fn,
			  GError **error)
{
	GVariant *body;
	gint fd;
	gint fd_sig;
	g_autoptr(GDBusMessage) request = NULL;
	g_autoptr(GUnixFDList) fd_list = NULL;

	/* open file */
	fd = open (data_fn, O_RDONLY);
	if (fd < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "failed to open %s",
			     data_fn);
		return FALSE;
	}
	fd_sig = open (sig_fn, O_RDONLY);
	if (fd_sig < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "failed to open %s",
			     sig_fn);
		return FALSE;
	}

	/* set out of band file descriptor */
	fd_list = g_unix_fd_list_new ();
	g_unix_fd_list_append (fd_list, fd, NULL);
	g_unix_fd_list_append (fd_list, fd_sig, NULL);
	request = g_dbus_message_new_method_call (FWUPD_DBUS_SERVICE,
						  FWUPD_DBUS_PATH,
						  FWUPD_DBUS_INTERFACE,
						  "UpdateMetadata");
	g_dbus_message_set_unix_fd_list (request, fd_list);

	/* g_unix_fd_list_append did a dup() already */
	close (fd);
	close (fd_sig);

	/* send message */
	body = g_variant_new ("(hh)", fd, fd_sig);
	g_dbus_message_set_body (request, body);
	g_dbus_connection_send_message_with_reply (priv->conn,
						   request,
						   G_DBUS_SEND_MESSAGE_FLAGS_NONE,
						   -1,
						   NULL,
						   NULL,
						   fu_util_update_cb,
						   priv);
	g_main_loop_run (priv->loop);
	if (priv->message == NULL) {
		g_dbus_error_strip_remote_error (priv->error);
		g_propagate_error (error, priv->error);
		return FALSE;
	}
	if (g_dbus_message_to_gerror (priv->message, error)) {
		g_dbus_error_strip_remote_error (*error);
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_util_download_file:
 **/
static gboolean
fu_util_download_file (FuUtilPrivate *priv,
		       const gchar *uri,
		       const gchar *fn,
		       const gchar *checksum_expected,
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
		checksum_actual = g_compute_checksum_for_data (G_CHECKSUM_SHA1,
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
 * fu_util_download_metadata:
 **/
static gboolean
fu_util_download_metadata (FuUtilPrivate *priv, GError **error)
{
	g_autofree gchar *config_fn = NULL;
	g_autofree gchar *data_uri = NULL;
	g_autofree gchar *sig_fn = NULL;
	g_autofree gchar *sig_uri = NULL;
	g_autoptr(GKeyFile) config = NULL;
	const gchar *data_fn = "/tmp/firmware.xml.gz";

	/* read config file */
	config = g_key_file_new ();
	config_fn = g_build_filename (SYSCONFDIR, "fwupd.conf", NULL);
	if (!g_key_file_load_from_file (config, config_fn, G_KEY_FILE_NONE, error)) {
		g_prefix_error (error, "Failed to load %s: ", config_fn);
		return FALSE;
	}

	/* download the signature */
	data_uri = g_key_file_get_string (config, "fwupd", "DownloadURI", error);
	if (data_uri == NULL)
		return FALSE;
	sig_uri = g_strdup_printf ("%s.asc", data_uri);
	sig_fn = g_strdup_printf ("%s.asc", data_fn);
	if (!fu_util_download_file (priv, sig_uri, sig_fn, NULL, error))
		return FALSE;

	/* download the payload */
	if (!fu_util_download_file (priv, data_uri, data_fn, NULL, error))
		return FALSE;

	/* send all this to fwupd */
	return fu_util_refresh_internal (priv, data_fn, sig_fn, error);
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
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments: expected 'filename.xml' 'filename.xml.asc'");
		return FALSE;
	}

	/* open file */
	return fu_util_refresh_internal (priv, values[0], values[1], error);
}

/**
 * fu_util_get_results:
 **/
static gboolean
fu_util_get_results (FuUtilPrivate *priv, gchar **values, GError **error)
{
	if (g_strv_length (values) != 1) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments: expected 'id'");
		return FALSE;
	}

	/* clear results, and wait for reply */
	g_dbus_proxy_call (priv->proxy,
			   "GetResults",
			   g_variant_new ("(s)", values[0]),
			   G_DBUS_CALL_FLAGS_NONE,
			   -1,
			   NULL,
			   fu_util_get_devices_cb, priv);
	g_main_loop_run (priv->loop);
	if (priv->val == NULL) {
		g_dbus_error_strip_remote_error (priv->error);
		g_propagate_error (error, priv->error);
		return FALSE;
	}
	fu_util_print_metadata (priv->val);
	return TRUE;
}


/**
 * fu_util_verify_internal:
 **/
static gboolean
fu_util_verify_internal (FuUtilPrivate *priv, const gchar *id, GError **error)
{
	g_dbus_proxy_call (priv->proxy,
			   "Verify",
			   g_variant_new ("(s)", id),
			   G_DBUS_CALL_FLAGS_NONE,
			   -1,
			   NULL,
			   fu_util_get_devices_cb, priv);
	g_main_loop_run (priv->loop);
	if (priv->val == NULL) {
		g_dbus_error_strip_remote_error (priv->error);
		g_propagate_error (error, priv->error);
		priv->error = NULL;
		return FALSE;
	}
	return TRUE;

}

/**
 * fu_util_verify_all:
 **/
static gboolean
fu_util_verify_all (FuUtilPrivate *priv, GError **error)
{
	FuDevice *dev;
	guint i;
	g_autoptr(GPtrArray) devices = NULL;

	/* get devices from daemon */
	devices = fu_util_get_devices_internal (priv, error);
	if (devices == NULL)
		return FALSE;

	/* get results */
	for (i = 0; i < devices->len; i++) {
		g_autoptr(GError) error_local = NULL;
		dev = g_ptr_array_index (devices, i);
		if (!fu_util_verify_internal (priv, fu_device_get_id (dev), &error_local)) {
			g_print ("%s\tFAILED: %s\n",
				 fu_device_get_guid (dev),
				 error_local->message);
			continue;
		}
		g_print ("%s\t%s\n",
			 fu_device_get_guid (dev),
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
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments: expected 'id'");
		return FALSE;
	}
	return fu_util_verify_internal (priv, values[0], error);
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
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments: expected 'id'");
		return FALSE;
	}
	g_dbus_proxy_call (priv->proxy,
			   "Unlock",
			   g_variant_new ("(s)", values[0]),
			   G_DBUS_CALL_FLAGS_NONE,
			   -1,
			   NULL,
			   fu_util_get_devices_cb, priv);
	g_main_loop_run (priv->loop);
	if (priv->val == NULL) {
		g_dbus_error_strip_remote_error (priv->error);
		g_propagate_error (error, priv->error);
		priv->error = NULL;
		return FALSE;
	}
	return TRUE;
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
	title_len = strlen (title);
	lines = g_strsplit (msg, "\n", -1);
	for (j = 0; lines[j] != NULL; j++) {
		for (i = title_len; i < 20; i++)
			g_print (" ");
		g_print ("%s\n", lines[j]);
		title_len = 0;
	}
}

/**
 * fu_util_get_updates:
 **/
static gboolean
fu_util_get_updates (FuUtilPrivate *priv, gchar **values, GError **error)
{
	FuDevice *dev;
	GPtrArray *devices = NULL;
	const gchar *tmp;
	guint i;

	/* print any updates */
	devices = fu_util_get_updates_internal (priv, error);
	if (devices == NULL)
		return FALSE;
	for (i = 0; i < devices->len; i++) {
		dev = g_ptr_array_index (devices, i);

		/* TRANSLATORS: first replacement is device name */
		g_print (_("%s has firmware updates:"), fu_device_get_display_name (dev));
		g_print ("\n");

		/* TRANSLATORS: Appstream ID for the hardware type */
		fu_util_print_data (_("ID"),
				    fu_device_get_metadata (dev, FU_DEVICE_KEY_APPSTREAM_ID));

		/* TRANSLATORS: a GUID for the hardware */
		fu_util_print_data (_("GUID"),
				    fu_device_get_metadata (dev, FU_DEVICE_KEY_GUID));

		/* TRANSLATORS: section header for firmware version */
		fu_util_print_data (_("Version"),
				    fu_device_get_metadata (dev, FU_DEVICE_KEY_UPDATE_VERSION));

		/* TRANSLATORS: section header for firmware SHA1 */
		fu_util_print_data (_("Checksum"),
				    fu_device_get_metadata (dev, FU_DEVICE_KEY_UPDATE_HASH));

		/* TRANSLATORS: section header for firmware remote http:// */
		fu_util_print_data (_("Location"),
				    fu_device_get_metadata (dev, FU_DEVICE_KEY_UPDATE_URI));

		/* convert XML -> text */
		tmp = fu_device_get_metadata (dev, FU_DEVICE_KEY_UPDATE_DESCRIPTION);
		if (tmp != NULL) {
			g_autofree gchar *md = NULL;
			md = as_markup_convert (tmp,
						AS_MARKUP_CONVERT_FORMAT_SIMPLE,
						NULL);
			if (md != NULL) {
				/* TRANSLATORS: section header for long firmware desc */
				fu_util_print_data (_("Description"), md);
			}
		}
	}

	return TRUE;
}

/**
 * fu_util_update:
 **/
static gboolean
fu_util_update (FuUtilPrivate *priv, gchar **values, GError **error)
{
	FuDevice *dev;
	GPtrArray *devices = NULL;
	guint i;

	/* apply any updates */
	devices = fu_util_get_updates_internal (priv, error);
	if (devices == NULL)
		return FALSE;
	for (i = 0; i < devices->len; i++) {
		const gchar *checksum;
		const gchar *uri;
		g_autofree gchar *basename = NULL;
		g_autofree gchar *fn = NULL;

		dev = g_ptr_array_index (devices, i);

		/* download file */
		checksum = fu_device_get_metadata (dev, FU_DEVICE_KEY_UPDATE_HASH);
		if (checksum == NULL)
			continue;
		uri = fu_device_get_metadata (dev, FU_DEVICE_KEY_UPDATE_URI);
		if (uri == NULL)
			continue;
		g_print ("Downloading %s for %s...\n",
			 fu_device_get_metadata (dev, FU_DEVICE_KEY_UPDATE_VERSION),
			 fu_device_get_display_name (dev));
		basename = g_path_get_basename (uri);
		fn = g_build_filename (g_get_tmp_dir (), basename, NULL);
		if (!fu_util_download_file (priv, uri, fn, checksum, error))
			return FALSE;
		g_print ("Updating %s on %s...\n",
			 fu_device_get_metadata (dev, FU_DEVICE_KEY_UPDATE_VERSION),
			 fu_device_get_display_name (dev));
		if (!fu_util_install_with_fallback (priv, fu_device_get_id (dev), fn, error))
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
 * main:
 **/
int
main (int argc, char *argv[])
{
	FuUtilPrivate *priv;
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
		priv->flags |= FU_PROVIDER_UPDATE_FLAG_OFFLINE;
	if (allow_reinstall)
		priv->flags |= FU_PROVIDER_UPDATE_FLAG_ALLOW_REINSTALL;
	if (allow_older)
		priv->flags |= FU_PROVIDER_UPDATE_FLAG_ALLOW_OLDER;

	/* connect to the daemon */
	priv->conn = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
	if (priv->conn == NULL) {
		/* TRANSLATORS: the user is in a bad place */
		g_print ("%s: %s\n", _("Failed to connect to D-Bus"),
			 error->message);
		goto out;
	}
	priv->proxy = g_dbus_proxy_new_sync (priv->conn,
					     G_DBUS_PROXY_FLAGS_NONE,
					     NULL,
					     FWUPD_DBUS_SERVICE,
					     FWUPD_DBUS_PATH,
					     FWUPD_DBUS_INTERFACE,
					     NULL,
					     &error);
	if (priv->proxy == NULL) {
		/* TRANSLATORS: we can't connect to the daemon */
		g_print ("%s: %s\n", _("Failed to connect to fwupd"),
			 error->message);
		goto out;
	}
	g_signal_connect (priv->proxy, "g-properties-changed",
			  G_CALLBACK (fu_util_status_changed_cb), priv);

	/* run the specified command */
	ret = fu_util_run (priv, argv[1], (gchar**) &argv[2], &error);
	if (!ret) {
		if (g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL)) {
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
		if (priv->val != NULL)
			g_variant_unref (priv->val);
		if (priv->message != NULL)
			g_object_unref (priv->message);
		if (priv->conn != NULL)
			g_object_unref (priv->conn);
		if (priv->proxy != NULL)
			g_object_unref (priv->proxy);
		g_main_loop_unref (priv->loop);
		g_option_context_free (priv->context);
		g_free (priv);
	}
	return retval;
}

