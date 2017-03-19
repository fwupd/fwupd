/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
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

#include <string.h>
#include <stdlib.h>

#include "lu-context.h"
#include "lu-device-bootloader.h"

typedef struct {
	GCancellable		*cancellable;
	GPtrArray		*cmd_array;
	LuContext		*ctx;
	LuDeviceKind		 emulation_kind;
} FuLuToolPrivate;

static void
lu_tool_private_free (FuLuToolPrivate *priv)
{
	if (priv == NULL)
		return;
	if (priv->ctx != NULL)
		g_object_unref (priv->ctx);
	g_object_unref (priv->cancellable);
	if (priv->cmd_array != NULL)
		g_ptr_array_unref (priv->cmd_array);
	g_free (priv);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuLuToolPrivate, lu_tool_private_free)

typedef gboolean (*FuLuToolPrivateCb)	(FuLuToolPrivate	*util,
					 gchar			**values,
					 GError			**error);

typedef struct {
	gchar			*name;
	gchar			*arguments;
	gchar			*description;
	FuLuToolPrivateCb	 callback;
} FuLuToolItem;

static void
lu_tool_item_free (FuLuToolItem *item)
{
	g_free (item->name);
	g_free (item->arguments);
	g_free (item->description);
	g_free (item);
}

static gint
lu_tool_sort_command_name_cb (FuLuToolItem **item1, FuLuToolItem **item2)
{
	return g_strcmp0 ((*item1)->name, (*item2)->name);
}

static void
lu_tool_add (GPtrArray *array,
	     const gchar *name,
	     const gchar *arguments,
	     const gchar *description,
	     FuLuToolPrivateCb callback)
{
	guint i;
	FuLuToolItem *item;
	g_auto(GStrv) names = NULL;

	g_return_if_fail (name != NULL);
	g_return_if_fail (description != NULL);
	g_return_if_fail (callback != NULL);

	/* add each one */
	names = g_strsplit (name, ",", -1);
	for (i = 0; names[i] != NULL; i++) {
		item = g_new0 (FuLuToolItem, 1);
		item->name = g_strdup (names[i]);
		if (i == 0) {
			item->description = g_strdup (description);
		} else {
			item->description = g_strdup_printf ("Alias to %s", names[0]);
		}
		item->arguments = g_strdup (arguments);
		item->callback = callback;
		g_ptr_array_add (array, item);
	}
}

static gchar *
lu_tool_get_descriptions (GPtrArray *array)
{
	guint i;
	gsize j;
	gsize len;
	const gsize max_len = 31;
	FuLuToolItem *item;
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

static gboolean
lu_tool_run (FuLuToolPrivate *priv,
	     const gchar *command,
	     gchar **values,
	     GError **error)
{
	guint i;
	FuLuToolItem *item;

	/* find command */
	for (i = 0; i < priv->cmd_array->len; i++) {
		item = g_ptr_array_index (priv->cmd_array, i);
		if (g_strcmp0 (item->name, command) == 0)
			return item->callback (priv, values, error);
	}

	/* not found */
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_FOUND,
			     "Command not found");
	return FALSE;
}

static LuDevice *
lu_get_default_device (FuLuToolPrivate *priv, GError **error)
{
	GPtrArray *devices = NULL;
	LuDevice *device = NULL;

	devices = lu_context_get_devices (priv->ctx);
	for (guint i = 0; i < devices->len; i++) {
		LuDevice *device_tmp = g_ptr_array_index (devices, i);
		g_debug ("got %s", lu_device_kind_to_string (lu_device_get_kind (device_tmp)));
		if (lu_device_get_kind (device_tmp) != LU_DEVICE_KIND_PERIPHERAL) {
			device = g_object_ref (device_tmp);
			break;
		}
	}

	/* nothing supported */
	if (device == NULL) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "No supported device plugged in");
		return NULL;
	}
	return device;
}

static gchar *
lu_device_flags_to_string (LuDeviceFlags flags)
{
	GString *str = g_string_new (NULL);
	if (flags & LU_DEVICE_FLAG_SIGNED_FIRMWARE)
		g_string_append (str, "signed-firmware,");
	if (flags & LU_DEVICE_FLAG_CAN_FLASH)
		g_string_append (str, "can-flash,");
	if (str->len == 0) {
		g_string_append (str, "none");
	} else {
		g_string_truncate (str, str->len - 1);
	}
	return g_string_free (str, FALSE);
}

static gboolean
lu_tool_info_device (FuLuToolPrivate *priv, LuDevice *device, GError **error)
{
	GPtrArray *guids;

	/* open */
	if (!lu_device_open (device, error))
		return FALSE;

	/* show on console */
	g_print ("Type:           %s\n",
		 lu_device_kind_to_string (lu_device_get_kind (device)));
	g_print ("Flags:          %s\n",
		 lu_device_flags_to_string (lu_device_get_flags (device)));
	g_print ("Platform ID:    %s\n",
		 lu_device_get_platform_id (device));
	g_print ("Vendor:         %s\n",
		 lu_device_get_vendor (device));
	g_print ("Product:        %s\n",
		 lu_device_get_product (device));
	if (lu_device_get_version_fw (device) != NULL) {
		g_print ("Firmware Ver:   %s\n",
			 lu_device_get_version_fw (device));
	}
	g_print ("Bootloader Ver: %s\n",
		 lu_device_get_version_bl (device));
	if (LU_IS_DEVICE_BOOTLOADER (device)) {
		g_print ("Flash Addr Hi:  0x%04x\n",
			 lu_device_bootloader_get_addr_hi (device));
		g_print ("Flash Addr Lo:  0x%04x\n",
			 lu_device_bootloader_get_addr_lo (device));
		g_print ("Flash Block Sz: 0x%04x\n",
			 lu_device_bootloader_get_blocksize (device));
	}
	guids = lu_device_get_guids (device);
	for (guint i = 0; i < guids->len; i++) {
		const gchar *guid = g_ptr_array_index (guids, i);
		g_print ("GUID:           %s\n", guid);
	}

	/* close device */
	return lu_device_close (device, error);
}

static gboolean
lu_tool_info (FuLuToolPrivate *priv, gchar **values, GError **error)
{
	GPtrArray *devices = NULL;
	g_autoptr(LuContext) ctx = NULL;

	/* emulated */
	if (priv->emulation_kind != LU_DEVICE_KIND_UNKNOWN) {
		g_autoptr(LuDevice) device = NULL;
		device = lu_device_fake_new (priv->emulation_kind);
		if (!lu_tool_info_device (priv, device, error))
			return FALSE;
	}

	/* get the devices */
	ctx = lu_context_new (error);
	if (ctx == NULL) {
		g_prefix_error (error, "Failed to create context: ");
		return FALSE;
	}
	devices = lu_context_get_devices (ctx);
	for (guint i = 0; i < devices->len; i++) {
		LuDevice *device = g_ptr_array_index (devices, i);
		if (!lu_tool_info_device (priv, device, error))
			return FALSE;
		if (i != devices->len - 1)
			g_print ("\n");
	}
	return TRUE;
}

static void
lu_write_progress_cb (goffset current, goffset total, gpointer user_data)
{
	FuLuToolPrivate *priv = (FuLuToolPrivate *) user_data;
	gdouble percentage = -1.f;
	if (priv->emulation_kind != LU_DEVICE_KIND_UNKNOWN)
		return;
	if (total > 0)
		percentage = (100.f * (gdouble) current) / (gdouble) total;
	g_print ("Written %" G_GOFFSET_FORMAT "/%" G_GOFFSET_FORMAT " bytes [%.1f%%]\n",
		 current, total, percentage);
}

static gboolean
lu_tool_dump (FuLuToolPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GPtrArray) reqs = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autofree gchar *data = NULL;
	gsize len = 0;

	/* check args */
	if (g_strv_length (values) != 1) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "Invalid arguments, expected FILENAME"
				     " -- e.g. `firmware.hex`");
		return FALSE;
	}

	/* load file and display */
	if (!g_file_get_contents (values[0], &data, &len, error))
		return FALSE;
	fw = g_bytes_new_static (data, len);
	reqs = lu_device_bootloader_parse_requests (fw, error);
	if (reqs == NULL)
		return FALSE;
	for (guint i = 0; i < reqs->len; i++) {
		LuDeviceBootloaderRequest *req = g_ptr_array_index (reqs, i);
		g_print ("0x%04x [0x%02x]", req->addr, req->len);
		for (guint j = 0; j < req->len; j++)
			g_print (" %02x", req->data[j]);
		g_print ("\n");
	}
	return TRUE;
}

static gboolean
lu_tool_write (FuLuToolPrivate *priv, gchar **values, GError **error)
{
	gsize len;
	g_autofree guint8 *data = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(LuDevice) device = NULL;

	/* check args */
	if (g_strv_length (values) != 1) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "Invalid arguments, expected FILENAME"
				     " -- e.g. `firmware.hex`");
		return FALSE;
	}

	/* open device */
	if (priv->emulation_kind == LU_DEVICE_KIND_UNKNOWN) {
		device = lu_get_default_device (priv, error);
		if (device == NULL)
			return FALSE;
	} else {
		device = lu_device_fake_new (priv->emulation_kind);
	}
	if (!lu_device_open (device, error))
		return FALSE;

	/* do we need to go into bootloader mode */
	if (lu_device_get_kind (device) == LU_DEVICE_KIND_RUNTIME) {
		if (!lu_device_detach (device, error))
			return FALSE;
		if (!lu_context_wait_for_replug (priv->ctx,
						 device,
						 5000,
						 error))
			return FALSE;
		g_object_unref (device);
		device = lu_get_default_device (priv, error);
		if (device == NULL)
			return FALSE;
		if (!lu_device_open (device, error))
			return FALSE;
	}

	/* load firmware file */
	if (!g_file_get_contents (values[0], (gchar **) &data, &len, error)) {
		g_prefix_error (error, "Failed to load %s: ", values[0]);
		return FALSE;
	}

	/* update with data blob */
	fw = g_bytes_new (data, len);
	if (!lu_device_write_firmware (device, fw,
				       lu_write_progress_cb, priv,
				       error))
		return FALSE;

	/* detach back into runtime */
	if (!lu_device_attach (device, error))
		return FALSE;
	if (!lu_device_close (device, error))
		return FALSE;

	return TRUE;
}

static gboolean
lu_tool_attach (FuLuToolPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(LuDevice) device = NULL;
	device = lu_get_default_device (priv, error);
	if (device == NULL)
		return FALSE;
	if (!lu_device_open (device, error))
		return FALSE;
	if (!lu_device_attach (device, error))
		return FALSE;
	if (!lu_device_close (device, error))
		return FALSE;
	return TRUE;
}

static void
lu_tool_device_added_cb (LuContext* ctx, LuDevice *device, FuLuToolPrivate *priv)
{
	g_print ("ADDED\tLogitech Unifying device %s {%p} [%s]\n",
		 lu_device_kind_to_string (lu_device_get_kind (device)),
		 device, lu_device_get_platform_id (device));
}

static void
lu_tool_device_removed_cb (LuContext* ctx, LuDevice *device, FuLuToolPrivate *priv)
{
	g_print ("REMOVED\tLogitech Unifying device %s {%p} [%s]\n",
		 lu_device_kind_to_string (lu_device_get_kind (device)),
		 device, lu_device_get_platform_id (device));
}

static gboolean
lu_tool_watch (FuLuToolPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GMainLoop) loop = g_main_loop_new (NULL, FALSE);
	g_autoptr(LuContext) ctx = lu_context_new (error);
	if (ctx == NULL)
		return FALSE;
	g_signal_connect (ctx, "added", G_CALLBACK (lu_tool_device_added_cb), priv);
	g_signal_connect (ctx, "removed", G_CALLBACK (lu_tool_device_removed_cb), priv);
	lu_context_coldplug (ctx);
	g_main_loop_run (loop);
	return TRUE;
}

static gboolean
lu_tool_detach (FuLuToolPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(LuDevice) device = NULL;
	device = lu_get_default_device (priv, error);
	if (device == NULL)
		return FALSE;
	if (!lu_device_open (device, error))
		return FALSE;
	if (!lu_device_detach (device, error))
		return FALSE;
	if (!lu_device_close (device, error))
		return FALSE;
	return TRUE;
}

static void
lu_tool_log_handler_cb (const gchar *log_domain,
			      GLogLevelFlags log_level,
			      const gchar *message,
			      gpointer user_data)
{
	g_print ("%s\t%s\n", log_domain, message);
}

int
main (int argc, char **argv)
{
	gboolean verbose = FALSE;
	g_autofree gchar *cmd_descriptions = NULL;
	g_autofree gchar *emulation_kind = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GOptionContext) context = NULL;
	g_autoptr(LuDevice) device = NULL;
	g_autoptr(FuLuToolPrivate) priv = g_new0 (FuLuToolPrivate, 1);
	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			"Print verbose debug statements", NULL },
		{ "emulate", 'e', 0, G_OPTION_ARG_STRING, &emulation_kind,
			"Emulate a device type", NULL },
		{ NULL}
	};

	/* FIXME: do stuff on ctrl+c */
	priv->cancellable = g_cancellable_new ();

	/* add commands */
	priv->cmd_array = g_ptr_array_new_with_free_func ((GDestroyNotify) lu_tool_item_free);
	lu_tool_add (priv->cmd_array,
		     "info", NULL,
		     "Show information about the device",
		     lu_tool_info);
	lu_tool_add (priv->cmd_array,
		     "write", "FILENAME",
		     "Update the firmware",
		     lu_tool_write);
	lu_tool_add (priv->cmd_array,
		     "dump", "FILENAME",
		     "Dump the firmware",
		     lu_tool_dump);
	lu_tool_add (priv->cmd_array,
		     "attach", NULL,
		     "Attach to firmware mode",
		     lu_tool_attach);
	lu_tool_add (priv->cmd_array,
		     "watch", NULL,
		     "Watch for hardare changes",
		     lu_tool_watch);
	lu_tool_add (priv->cmd_array,
		     "detach", NULL,
		     "Detach to bootloader mode",
		     lu_tool_detach);

	/* sort by command name */
	g_ptr_array_sort (priv->cmd_array,
			  (GCompareFunc) lu_tool_sort_command_name_cb);

	/* get a list of the commands */
	context = g_option_context_new (NULL);
	cmd_descriptions = lu_tool_get_descriptions (priv->cmd_array);
	g_option_context_set_summary (context, cmd_descriptions);
	g_set_application_name ("Logitech Lu Debug Tool");
	g_option_context_add_main_entries (context, options, NULL);
	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		g_print ("%s: %s\n", "Failed to parse arguments", error->message);
		return EXIT_FAILURE;
	}

	/* emulate */
	priv->emulation_kind = lu_device_kind_from_string (emulation_kind);
	if (priv->emulation_kind != LU_DEVICE_KIND_UNKNOWN)
		g_log_set_default_handler (lu_tool_log_handler_cb, priv);

	/* get the device */
	priv->ctx = lu_context_new (&error);
	if (priv->ctx == NULL) {
		g_print ("Failed to open USB devices: %s\n", error->message);
		return EXIT_FAILURE;
	}

	/* set verbose? */
	if (verbose)
		g_setenv ("G_MESSAGES_DEBUG", "all", FALSE);

	/* run the specified command */
	if (!lu_tool_run (priv, argv[1], (gchar**) &argv[2], &error)) {
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
			g_autofree gchar *tmp = NULL;
			tmp = g_option_context_get_help (context, TRUE, NULL);
			g_print ("%s\n\n%s", error->message, tmp);
		} else {
			g_print ("%s\n", error->message);
		}
		return EXIT_FAILURE;
	}

	return 0;
}
