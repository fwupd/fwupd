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
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>

#include "fu-cleanup.h"
#include "fu-guid.h"
#include "fu-pending.h"
#include "fu-provider.h"
#include "fu-rom.h"

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
	_cleanup_strv_free_ gchar **names = NULL;

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
	_cleanup_string_free_ GString *string = NULL;

	/* find command */
	for (i = 0; i < priv->cmd_array->len; i++) {
		item = g_ptr_array_index (priv->cmd_array, i);
		if (g_strcmp0 (item->name, command) == 0)
			return item->callback (priv, values, error);
	}

	/* not found */
	string = g_string_new ("");
	g_string_append_printf (string, "%s\n",
				/* TRANSLATORS: error message */
				_("Command not found, valid commands are:"));
	for (i = 0; i < priv->cmd_array->len; i++) {
		item = g_ptr_array_index (priv->cmd_array, i);
		g_string_append_printf (string, " * %s %s\n",
					item->name,
					item->arguments ? item->arguments : "");
	}
	g_set_error_literal (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, string->str);
	return FALSE;
}

/**
 * fu_util_status_changed_cb:
 **/
static void
fu_util_status_changed_cb (GDBusProxy *proxy, GVariant *changed_properties,
			   GStrv invalidated_properties, gpointer user_data)
{
	_cleanup_variant_unref_ GVariant *val = NULL;

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
	_cleanup_variant_iter_free_ GVariantIter *iter = NULL;

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
 * fu_util_get_devices:
 **/
static gboolean
fu_util_get_devices (FuUtilPrivate *priv, gchar **values, GError **error)
{
	FuDevice *dev;
	_cleanup_ptrarray_unref_ GPtrArray *devices = NULL;
	guint i;
	guint j;
	guint k;
	guint f;
	guint64 flags;
	const gchar *value;
	const gchar *keys[] = {
		FU_DEVICE_KEY_DISPLAY_NAME,
		FU_DEVICE_KEY_PROVIDER,
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
					g_print ("  %s:", flags_str[f]);
					for (k = strlen (flags_str[f]); k < 15; k++)
						g_print (" ");
					g_print (" %s\n", flags & (1 << f) ? "True" : "False");
				}
				continue;
			}
			value = fu_device_get_metadata (dev, keys[j]);
			if (value != NULL) {
				g_print ("  %s:", keys[j]);
				for (k = strlen (keys[j]); k < 15; k++)
					g_print (" ");
				g_print (" %s\n", value);
			}
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
 * fu_util_update:
 **/
static gboolean
fu_util_update (FuUtilPrivate *priv, const gchar *id, const gchar *filename,
		FuProviderFlags flags, GError **error)
{
	GVariant *body;
	GVariantBuilder builder;
	gint retval;
	gint fd;
	_cleanup_object_unref_ GDBusMessage *request = NULL;
	_cleanup_object_unref_ GUnixFDList *fd_list = NULL;

	/* set options */
	g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
	g_variant_builder_add (&builder, "{sv}",
			       "reason", g_variant_new_string ("user-action"));
	g_variant_builder_add (&builder, "{sv}",
			       "filename", g_variant_new_string (filename));
	if (flags & FU_PROVIDER_UPDATE_FLAG_OFFLINE) {
		g_variant_builder_add (&builder, "{sv}",
				       "offline", g_variant_new_boolean (TRUE));
	}
	if (flags & FU_PROVIDER_UPDATE_FLAG_ALLOW_OLDER) {
		g_variant_builder_add (&builder, "{sv}",
				       "allow-older", g_variant_new_boolean (TRUE));
	}
	if (flags & FU_PROVIDER_UPDATE_FLAG_ALLOW_REINSTALL) {
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
						  "Update");
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
 * fu_util_update_online:
 **/
static gboolean
fu_util_update_online (FuUtilPrivate *priv, gchar **values, GError **error)
{
	if (g_strv_length (values) != 2) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments: expected 'id' 'filename'");
		return FALSE;
	}
	return fu_util_update (priv, values[0], values[1],
			       priv->flags, error);
}

/**
 * fu_util_install:
 **/
static gboolean
fu_util_install (FuUtilPrivate *priv, gchar **values, GError **error)
{
	if (g_strv_length (values) != 1) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments: expected 'filename'");
		return FALSE;
	}
	return fu_util_update (priv, FWUPD_DEVICE_ID_ANY,
			       values[0], priv->flags, error);
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
	_cleanup_variant_iter_free_ GVariantIter *iter = NULL;

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
	_cleanup_object_unref_ GDBusMessage *message = NULL;
	_cleanup_object_unref_ GDBusMessage *request = NULL;
	_cleanup_object_unref_ GUnixFDList *fd_list = NULL;

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
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_object_unref_ GDBusConnection *connection = NULL;
	_cleanup_variant_unref_ GVariant *val = NULL;

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
 * fu_util_update_prepared:
 **/
static gboolean
fu_util_update_prepared (FuUtilPrivate *priv, gchar **values, GError **error)
{
	gint vercmp;
	guint cnt = 0;
	guint i;
	const gchar *tmp;
	_cleanup_ptrarray_unref_ GPtrArray *devices = NULL;
	_cleanup_object_unref_ FuPending *pending = NULL;

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
		vercmp = as_utils_vercmp (fu_device_get_metadata (device, FU_DEVICE_KEY_VERSION_OLD),
					  fu_device_get_metadata (device, FU_DEVICE_KEY_VERSION_NEW));
		if (vercmp == 0) {
			/* TRANSLATORS: the first replacement is a display name
			 * e.g. "ColorHugALS" and the second is a version number
			 * e.g. "1.2.3" */
			g_print (_("Reinstalling %s with %s... "),
				 fu_device_get_display_name (device),
				 fu_device_get_metadata (device, FU_DEVICE_KEY_VERSION_NEW));
		} else if (vercmp > 0) {
			/* TRANSLATORS: the first replacement is a display name
			 * e.g. "ColorHugALS" and the second and third are
			 * version numbers e.g. "1.2.3" */
			g_print (_("Downgrading %s from %s to %s... "),
				 fu_device_get_display_name (device),
				 fu_device_get_metadata (device, FU_DEVICE_KEY_VERSION_OLD),
				 fu_device_get_metadata (device, FU_DEVICE_KEY_VERSION_NEW));
		} else if (vercmp < 0) {
			/* TRANSLATORS: the first replacement is a display name
			 * e.g. "ColorHugALS" and the second and third are
			 * version numbers e.g. "1.2.3" */
			g_print (_("Updating %s from %s to %s... "),
				 fu_device_get_display_name (device),
				 fu_device_get_metadata (device, FU_DEVICE_KEY_VERSION_OLD),
				 fu_device_get_metadata (device, FU_DEVICE_KEY_VERSION_NEW));
		}
		if (!fu_util_update (priv,
				     fu_device_get_id (device),
				     fu_device_get_metadata (device, FU_DEVICE_KEY_FILENAME_CAB),
				     priv->flags, error))
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
 * fu_util_update_offline:
 **/
static gboolean
fu_util_update_offline (FuUtilPrivate *priv, gchar **values, GError **error)
{
	if (g_strv_length (values) != 2) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments: expected 'id' 'filename'");
		return FALSE;
	}
	return fu_util_update (priv, values[0], values[1],
			       priv->flags | FU_PROVIDER_UPDATE_FLAG_OFFLINE,
			       error);
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
	_cleanup_object_unref_ GFile *xml_file = NULL;

	if (g_strv_length (values) == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments: expected 'filename.rom'");
		return FALSE;
	}
	for (i = 0; values[i] != NULL; i++) {
		_cleanup_free_ gchar *guid = NULL;
		_cleanup_free_ gchar *id = NULL;
		_cleanup_object_unref_ FuRom *rom = NULL;
		_cleanup_object_unref_ GFile *file = NULL;
		_cleanup_error_free_ GError *error_local = NULL;

		file = g_file_new_for_path (values[i]);
		rom = fu_rom_new ();
		g_print ("%s:\n", values[i]);
		if (!fu_rom_load_file (rom, file, NULL, &error_local)) {
			g_print ("%s\n", error_local->message);
			continue;
		}
		if (!fu_rom_generate_checksum (rom, NULL, &error_local)) {
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
 * fu_util_verify_update:
 **/
static gboolean
fu_util_verify_update (FuUtilPrivate *priv, gchar **values, GError **error)
{
	guint i;
	_cleanup_object_unref_ AsStore *store = NULL;
	_cleanup_object_unref_ GFile *xml_file = NULL;

	if (g_strv_length (values) < 2) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments: expected 'filename.xml' 'filename.rom'");
		return FALSE;
	}
	store = as_store_new ();

	/* open existing file */
	xml_file = g_file_new_for_path (values[0]);
	if (g_file_query_exists (xml_file, NULL)) {
		if (!as_store_from_file (store, xml_file, NULL, NULL, error))
			return FALSE;
	}

	/* add new values */
	as_store_set_api_version (store, 0.9);
	for (i = 1; values[i] != NULL; i++) {
		_cleanup_free_ gchar *guid = NULL;
		_cleanup_free_ gchar *id = NULL;
		_cleanup_object_unref_ AsApp *app = NULL;
		_cleanup_object_unref_ AsRelease *rel = NULL;
		_cleanup_object_unref_ FuRom *rom = NULL;
		_cleanup_object_unref_ GFile *file = NULL;
		_cleanup_error_free_ GError *error_local = NULL;

		file = g_file_new_for_path (values[i]);
		rom = fu_rom_new ();
		g_print ("Processing %s...\n", values[i]);
		if (!fu_rom_load_file (rom, file, NULL, &error_local)) {
			g_print ("%s\n", error_local->message);
			continue;
		}
		if (!fu_rom_generate_checksum (rom, NULL, error))
			return FALSE;

		/* add app to store */
		app = as_app_new ();
		id = g_strdup_printf ("0x%04x:0x%04x",
				      fu_rom_get_vendor (rom),
				      fu_rom_get_model (rom));
		guid = fu_guid_generate_from_string (id);
		as_app_set_id (app, guid, -1);
		as_app_set_id_kind (app, AS_ID_KIND_FIRMWARE);
		as_app_set_source_kind (app, AS_APP_SOURCE_KIND_INF);
		rel = as_release_new ();
		as_release_set_version (rel, fu_rom_get_version (rom), -1);
		as_release_set_checksum (rel, G_CHECKSUM_SHA1,
					 fu_rom_get_checksum (rom), -1);
		as_app_add_release (app, rel);
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
 * fu_util_update_metadata:
 **/
static gboolean
fu_util_update_metadata (FuUtilPrivate *priv, gchar **values, GError **error)
{
	GVariant *body;
	gint fd;
	gint fd_sig;
	_cleanup_object_unref_ GDBusMessage *request = NULL;
	_cleanup_object_unref_ GUnixFDList *fd_list = NULL;

	if (g_strv_length (values) != 2) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments: expected 'filename.xml' 'filename.xml.asc'");
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
	fd_sig = open (values[1], O_RDONLY);
	if (fd_sig < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "failed to open %s",
			     values[1]);
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
	AsApp *app;
	FuDevice *dev;
	const gchar *tmp;
	guint i;
	_cleanup_object_unref_ AsStore *store = NULL;
	_cleanup_ptrarray_unref_ GPtrArray *devices = NULL;
	_cleanup_ptrarray_unref_ GPtrArray *devices_tmp = NULL;

	/* get devices from daemon */
	devices_tmp = fu_util_get_devices_internal (priv, error);
	if (devices_tmp == NULL)
		return FALSE;

	/* get results */
	for (i = 0; i < devices_tmp->len; i++) {
		_cleanup_error_free_ GError *error_local = NULL;
		dev = g_ptr_array_index (devices_tmp, i);
		if (!fu_util_verify_internal (priv, fu_device_get_id (dev), &error_local)) {
			g_print ("Failed to verify %s: %s\n",
				 fu_device_get_id (dev),
				 error_local->message);
		}
	}

	/* only load firmware from the system */
	store = as_store_new ();
	as_store_add_filter (store, AS_ID_KIND_FIRMWARE);
	if (!as_store_load (store, AS_STORE_LOAD_FLAG_APP_INFO_SYSTEM, NULL, error))
		return FALSE;

	/* print */
	devices = fu_util_get_devices_internal (priv, error);
	if (devices == NULL)
		return FALSE;
	for (i = 0; i < devices->len; i++) {
		const gchar *hash = NULL;
		const gchar *ver = NULL;
		_cleanup_free_ gchar *status = NULL;

		dev = g_ptr_array_index (devices, i);
		hash = fu_device_get_metadata (dev, FU_DEVICE_KEY_FIRMWARE_HASH);
		if (hash == NULL)
			continue;
		app = as_store_get_app_by_id (store, fu_device_get_guid (dev));
		if (app == NULL) {
			status = g_strdup ("No metadata");
		} else {
			AsRelease *rel;
			ver = fu_device_get_metadata (dev, FU_DEVICE_KEY_VERSION);
			rel = as_app_get_release (app, ver);
			if (rel == NULL) {
				status = g_strdup_printf ("No version %s", ver);
			} else {
				tmp = as_release_get_checksum (rel, G_CHECKSUM_SHA1);
				if (g_strcmp0 (tmp, hash) != 0) {
					status = g_strdup_printf ("Failed: for v%s expected %s", ver, tmp);
				} else {
					status = g_strdup ("OK");
				}
			}
		}
		g_print ("%s\t%s\t%s\n", fu_device_get_guid (dev), hash, status);
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
 * fu_util_print_data:
 **/
static void
fu_util_print_data (const gchar *title, const gchar *msg)
{
	guint i;
	guint j;
	guint title_len;
	_cleanup_strv_free_ gchar **lines = NULL;

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
 * fu_util_get_updates_app:
 **/
static AsRelease *
fu_util_get_updates_app (FuUtilPrivate *priv, FuDevice *dev, AsApp *app, GError **error)
{
	AsRelease *rel;
	AsRelease *rel_newest = NULL;
	GPtrArray *releases;
	const gchar *display_name;
	const gchar *tmp;
	const gchar *version;
	guint i;

	/* find any newer versions */
	display_name = fu_device_get_display_name (dev);
	version = fu_device_get_metadata (dev, FU_DEVICE_KEY_VERSION);
	if (version == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "Device %s has no Version",
			     fu_device_get_id (dev));
		return NULL;
	}
	releases = as_app_get_releases (app);
	for (i = 0; i < releases->len; i++) {

		/* check if actually newer */
		rel = g_ptr_array_index (releases, i);
		if ((priv->flags & FU_PROVIDER_UPDATE_FLAG_ALLOW_REINSTALL) == 0 &&
		    (priv->flags & FU_PROVIDER_UPDATE_FLAG_ALLOW_OLDER) == 0 &&
		    as_utils_vercmp (as_release_get_version (rel), version) <= 0)
			continue;

		/* this is the first and newest */
		if (rel_newest == NULL) {
			/* TRANSLATORS: first replacement is device name */
			g_print (_("%s has firmware updates:"), display_name);
			g_print ("\n");
			rel_newest = rel;
		}

		/* TRANSLATORS: section header for firmware version */
		fu_util_print_data (_("Version"), as_release_get_version (rel));

		/* TRANSLATORS: section header for firmware SHA1 */
		fu_util_print_data (_("Checksum"), as_release_get_checksum (rel, G_CHECKSUM_SHA1));

		/* TRANSLATORS: section header for firmware remote http:// */
		fu_util_print_data (_("Location"), as_release_get_location_default (rel));

		/* description is optional */
		tmp = as_release_get_description (rel, NULL);
		if (tmp != NULL) {
			_cleanup_free_ gchar *md = NULL;
			md = as_markup_convert (tmp, -1,
						AS_MARKUP_CONVERT_FORMAT_SIMPLE,
						NULL);
			if (md != NULL) {
				/* TRANSLATORS: section header for long firmware desc */
				fu_util_print_data (_("Description"), md);
			}
		}
	}

	/* nothing */
	if (rel_newest == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOTHING_TO_DO,
			     "Device %s has no firmware updates",
			     display_name);
	}

	return rel_newest;
}

/**
 * fu_util_get_updates:
 **/
static gboolean
fu_util_get_updates (FuUtilPrivate *priv, gchar **values, GError **error)
{
	AsApp *app;
	FuDevice *dev;
	guint i;
	_cleanup_object_unref_ AsStore *store = NULL;
	_cleanup_ptrarray_unref_ GPtrArray *devices = NULL;

	/* only load firmware from the system */
	store = as_store_new ();
	as_store_add_filter (store, AS_ID_KIND_FIRMWARE);
	if (!as_store_load (store, AS_STORE_LOAD_FLAG_APP_INFO_SYSTEM, NULL, error))
		return FALSE;

	/* get devices from daemon */
	devices = fu_util_get_devices_internal (priv, error);
	if (devices == NULL)
		return FALSE;

	/* find any GUIDs in the AppStream metadata */
	for (i = 0; i < devices->len; i++) {
		_cleanup_error_free_ GError *error_local = NULL;
		dev = g_ptr_array_index (devices, i);

		/* match the GUID in the XML */
		app = as_store_get_app_by_id (store, fu_device_get_guid (dev));
		if (app == NULL)
			continue;

		/* we found a device match, does it need updating */
		if (fu_util_get_updates_app (priv, dev, app, &error_local) == NULL) {
			if (g_error_matches (error_local,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOTHING_TO_DO)) {
				g_print ("%s\n", error_local->message);
				continue;
			}
			g_propagate_error (error, error_local);
			error_local = NULL;
			return FALSE;
		}
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
	gboolean ret;
	gboolean force = FALSE;
	gboolean verbose = FALSE;
	guint retval = 1;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_free_ gchar *cmd_descriptions = NULL;
	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			/* TRANSLATORS: command line option */
			_("Show extra debugging information"), NULL },
		{ "force", 'f', 0, G_OPTION_ARG_NONE, &force,
			/* TRANSLATORS: command line option */
			_("Force the installation of firmware"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

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
		     "update-offline",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Install the update the next time the computer is rebooted"),
		     fu_util_update_offline);
	fu_util_add (priv->cmd_array,
		     "update-online",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Install the update now"),
		     fu_util_update_online);
	fu_util_add (priv->cmd_array,
		     "update-prepared",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Install prepared updates now"),
		     fu_util_update_prepared);
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
		     "verify",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Gets the cryptographic hash of the dumped firmware"),
		     fu_util_verify);
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
		     "update-metadata",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Updates metadata"),
		     fu_util_update_metadata);
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
	g_set_application_name (_("Firmware Update"));
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

	/* we're feeling naughty */
	if (force) {
		priv->flags = FU_PROVIDER_UPDATE_FLAG_ALLOW_REINSTALL |
			      FU_PROVIDER_UPDATE_FLAG_ALLOW_OLDER;
	}

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
			_cleanup_free_ gchar *tmp = NULL;
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

