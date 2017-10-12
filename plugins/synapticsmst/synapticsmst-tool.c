/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
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
#include "synapticsmst-common.h"
#include "synapticsmst-device.h"

#include <stdlib.h>
#include <stdio.h>
#include <glib/gi18n.h>
#include <glib-unix.h>
#include <libintl.h>
#include <locale.h>

typedef struct {
	GCancellable		*cancellable;
	GPtrArray		*cmd_array;
	gboolean		 force;
	GPtrArray		*device_array;
} SynapticsMSTToolPrivate;

static void
synapticsmst_tool_private_free (SynapticsMSTToolPrivate *priv)
{
	if (priv == NULL)
		return;
	g_object_unref (priv->cancellable);
	g_ptr_array_unref (priv->device_array);
	if (priv->cmd_array != NULL)
		g_ptr_array_unref (priv->cmd_array);
	g_free (priv);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(SynapticsMSTToolPrivate, synapticsmst_tool_private_free)

typedef gboolean (*FuUtilPrivateCb)     (SynapticsMSTToolPrivate *util,
					 gchar			**values,
					 guint8			  device_index,
					 GError			**error);

typedef struct {
	gchar		*name;
	gchar		*arguments;
	gchar		*description;
	FuUtilPrivateCb	 callback;
} FuUtilItem;

static void
synapticsmst_tool_item_free (FuUtilItem *item)
{
	g_free (item->name);
	g_free (item->arguments);
	g_free (item->description);
	g_free (item);
}

static gint
synapticsmst_tool_sort_command_name_cb (FuUtilItem **item1, FuUtilItem **item2)
{
	return g_strcmp0 ((*item1)->name, (*item2)->name);
}

static void
synapticsmst_tool_add (GPtrArray *array,
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
synapticsmst_tool_get_descriptions (GPtrArray *array)
{
	gsize len;
	const gsize max_len = 31;
	GString *str;

	/* print each command */
	str = g_string_new ("");
	for (guint i = 0; i < array->len; i++) {
		FuUtilItem *item = g_ptr_array_index (array, i);
		g_string_append (str, "  ");
		g_string_append (str, item->name);
		len = strlen (item->name) + 2;
		if (item->arguments != NULL) {
			g_string_append (str, " ");
			g_string_append (str, item->arguments);
			len += strlen (item->arguments) + 1;
		}
		if (len < max_len) {
			for (gsize j = len; j < max_len + 1; j++)
				g_string_append_c (str, ' ');
			g_string_append (str, item->description);
			g_string_append_c (str, '\n');
		} else {
			g_string_append_c (str, '\n');
			for (gsize j = 0; j < max_len + 1; j++)
				g_string_append_c (str, ' ');
			g_string_append (str, item->description);
			g_string_append_c (str, '\n');
		}
	}

	/* remove trailing newline */
	if (str->len > 0)
		g_string_set_size (str, str->len - 1);

	return g_string_free (str, FALSE);
}

static gboolean
synapticsmst_tool_scan_aux_nodes (SynapticsMSTToolPrivate *priv, GError **error)
{
	SynapticsMSTDevice *cascade_device = NULL;
	GDir *dir;
	const gchar *aux_node;
	guint8 layer = 0;
	guint16 rad = 0;

	dir = g_dir_open (SYSFS_DRM_DP_AUX, 0, NULL);
	do {
		g_autoptr(GError) error_local = NULL;
		g_autoptr(SynapticsMSTDevice) device = NULL;

		aux_node = g_dir_read_name (dir);
		if (aux_node == NULL)
			break;

		/* can we open the device? */
		device = synapticsmst_device_new (SYNAPTICSMST_DEVICE_KIND_DIRECT, aux_node, 0, 0);
		if (!synapticsmst_device_open (device, &error_local)) {
			if (g_error_matches (error_local,
					     G_IO_ERROR,
					     G_IO_ERROR_PERMISSION_DENIED)) {
				g_set_error (error,
					     error_local->domain,
					     error_local->code,
					     "failed to open aux node: %s",
					     error_local->message);
				return FALSE;
			}
			/* ignore */
			continue;
		}

		/* add device to results */
		g_ptr_array_add (priv->device_array, g_object_ref (device));
	} while (TRUE);

	/* no devices */
	if (priv->device_array->len == 0) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "No Synaptics MST Device Found");
		return FALSE;
	}

	/* add all cascaded devices */
	for (guint8 i = 0; i < priv->device_array->len; i++) {
		SynapticsMSTDevice *device = g_ptr_array_index (priv->device_array, i);
		aux_node = synapticsmst_device_get_aux_node (device);
		if (!synapticsmst_device_open (device, error)) {
			g_prefix_error (error,
					"failed to open aux node %s again",
					aux_node);
			return FALSE;
		}

		for (guint8 j = 0; j < 2; j++) {
			if (!synapticsmst_device_scan_cascade_device (device, error, j))
				return FALSE;
			if (!synapticsmst_device_get_cascade (device))
				continue;
			layer = synapticsmst_device_get_layer (device) + 1;
			rad = synapticsmst_device_get_rad (device) | (j << (2 * (layer - 1)));
			cascade_device = synapticsmst_device_new (SYNAPTICSMST_DEVICE_KIND_REMOTE,
								  aux_node, layer, rad);
			g_ptr_array_add (priv->device_array, cascade_device);
		}
	}

	/* success */
	return TRUE;
}

static gboolean
synapticsmst_tool_enumerate (SynapticsMSTToolPrivate *priv,
			     gchar **values,
			     guint8 device_index,
			     GError **error)
{
	SynapticsMSTDevice *device = NULL;

	/* check avaliable dp aux nodes and add devices */
	if (!synapticsmst_tool_scan_aux_nodes (priv, error))
		return FALSE;

	g_print ("\nMST Devices:\n");
	/* enumerate all devices one by one */
	for (guint8 i = 0; i < priv->device_array->len; i++) {
		const gchar *board_id = NULL;
		device = g_ptr_array_index (priv->device_array, i);
		g_print ("[Device %1d]\n", i+1);
		if (!synapticsmst_device_enumerate_device (device, NULL, NULL,
							   error))
			return FALSE;

		board_id = synapticsmst_device_board_id_to_string (synapticsmst_device_get_board_id (device));
		if (board_id != NULL) {
			g_print ("Device: %s with Synaptics %s\n",
				 board_id,
				 synapticsmst_device_get_chip_id (device));
			g_print ("Connect Type: %s in DP Aux Node %s\n",
				 synapticsmst_device_kind_to_string (synapticsmst_device_get_kind (device)),
				 synapticsmst_device_get_aux_node (device));
			g_print ("Firmware version: %s\n", synapticsmst_device_get_version (device));
		} else {
			g_print ("Unknown Device\n");
		}
		g_print ("\n");
	}
	return TRUE;
}

static gboolean
synapticsmst_tool_flash (SynapticsMSTToolPrivate *priv,
			 gchar **values,
			 guint8 device_index,
			 GError **error)
{
	SynapticsMSTDevice *device = NULL;
	gsize len;
	g_autofree guint8 *data = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GError) error_local = NULL;

	/* incorrect args */
	if (g_strv_length (values) != 1) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "Incorrect arguments, expected FILENAME]");
		return FALSE;
	}

	/* check avaliable dp aux nodes and add devices */
	if (!synapticsmst_tool_scan_aux_nodes (priv, error))
		return FALSE;

	device = g_ptr_array_index (priv->device_array, (device_index - 1));
	if (!synapticsmst_device_enumerate_device (device, NULL, NULL, error))
		return FALSE;

	if (synapticsmst_device_board_id_to_string (synapticsmst_device_get_board_id (device)) == NULL) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "failed to flash firmware: unknown device");
		return FALSE;
	}
	if (!g_file_get_contents (values[0],
				  (gchar **) &data, &len,
				  &error_local)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "Failed to flash firmware: "
			     "can't load file %s: %s",
			     values[0],
			     error_local->message);
		return FALSE;
	}

	fw = g_bytes_new (data, len);
	if (!synapticsmst_device_write_firmware (device, fw, NULL, NULL, error)) {
		g_prefix_error (error, "failed to flash firmware: ");
		return FALSE;
	}
	g_print ("Update Sucessfully. Please reset device to apply new firmware\n");

	return TRUE;
}

static gboolean
synapticsmst_tool_run (SynapticsMSTToolPrivate *priv,
		       const gchar *command,
		       gchar **values,
		       guint8 device_index,
		       GError **error)
{
	/* find command */
	for (guint i = 0; i < priv->cmd_array->len; i++) {
		FuUtilItem *item = g_ptr_array_index (priv->cmd_array, i);
		if (g_strcmp0 (item->name, command) == 0)
			return item->callback (priv, values, device_index, error);
	}

	/* not found */
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     /* TRANSLATORS: error message */
			     _("Command not found"));
	return FALSE;
}

static gboolean
synapticsmst_tool_sigint_cb (gpointer user_data)
{
	SynapticsMSTToolPrivate *priv = (SynapticsMSTToolPrivate *) user_data;
	g_debug ("Handling SIGINT");
	g_cancellable_cancel (priv->cancellable);
	return FALSE;
}

int
main (int argc, char **argv)
{
	gboolean ret;
	gboolean verbose = FALSE;
	guint8 device_index = 0;
	g_autofree gchar *cmd_descriptions = NULL;
	g_autoptr (SynapticsMSTToolPrivate) priv = g_new0 (SynapticsMSTToolPrivate, 1);
	g_autoptr (GError) error = NULL;
	g_autoptr (GOptionContext) context = NULL;
	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			"Print verbose debug statements", NULL },
		{ "force", '\0', 0, G_OPTION_ARG_NONE, &priv->force,
			"Force the action ignoring all warnings", NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* list of devices */
	priv->device_array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

	/* add commands */
	priv->cmd_array = g_ptr_array_new_with_free_func ((GDestroyNotify) synapticsmst_tool_item_free);
	synapticsmst_tool_add (priv->cmd_array,
			       "enumerate",
			       NULL,
			       /* TRANSLATORS: command description */
			       _("Enumerate all Synaptics MST devices"),
			       synapticsmst_tool_enumerate);
	synapticsmst_tool_add (priv->cmd_array,
			       "flash",
			       NULL,
			       /* TRANSLATORS: command description */
			       _("Flash firmware file to MST device"),
			       synapticsmst_tool_flash);

	/* do stuff on ctrl+c */
	priv->cancellable = g_cancellable_new ();
	g_unix_signal_add_full (G_PRIORITY_DEFAULT,
				SIGINT,
				synapticsmst_tool_sigint_cb,
				priv,
				NULL);

	/* sort by command name */
	g_ptr_array_sort (priv->cmd_array,
			  (GCompareFunc) synapticsmst_tool_sort_command_name_cb);

	/* get a list of the commands */
	context = g_option_context_new (NULL);
	cmd_descriptions = synapticsmst_tool_get_descriptions (priv->cmd_array);
	g_option_context_set_summary (context, cmd_descriptions);

	g_set_application_name (_("Synaptics Multistream Transport Utility"));
	g_option_context_add_main_entries (context, options, NULL);
	ret = g_option_context_parse (context, &argc, &argv, &error);
	if (!ret) {
		/* TRANSLATORS: the user didn't read the man page */
		g_print ("%s: %s\n", _("Failed to parse arguments"), error->message);
		return EXIT_FAILURE;
	}

	/* set verbose? */
	if (verbose)
		g_setenv ("G_MESSAGES_DEBUG", "all", FALSE);

	/* run the specified command */
	if (argc == 4)
		device_index = strtol (argv[3], NULL, 10);
	ret = synapticsmst_tool_run (priv, argv[1], (gchar**) &argv[2], device_index, &error);
	if (!ret) {
		g_print ("%s\n", error->message);
		return EXIT_FAILURE;
	}

	/* success/ */
	return EXIT_SUCCESS;
}
