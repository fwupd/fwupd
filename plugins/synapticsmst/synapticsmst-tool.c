/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Mario Limonciello <mario_limonciello@dell.com>
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
#include "synapticsmst-device.h"
#include "synapticsmst-common.h"

#include <stdlib.h>
#include <stdio.h>
#include <glib/gi18n.h>
#include <glib-unix.h>
#include <libintl.h>
#include <locale.h>
#include <libfwupd/fwupd-error.h>

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
	if (priv->cmd_array != NULL)
		g_ptr_array_unref (priv->cmd_array);
		if (priv->device_array != NULL)
			g_ptr_array_unref (priv->device_array);
	g_free (priv);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC(SynapticsMSTToolPrivate, synapticsmst_tool_private_free)

typedef gboolean (*FuUtilPrivateCb)	(SynapticsMSTToolPrivate *util,
					 gchar	**values,
					 guint8	  device_index,
					 GError	**error);

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

static gchar *
synapticsmst_tool_get_descriptions (GPtrArray *array)
{
	guint i;
	gsize j;
	gsize len;
	const gsize max_len = 31;
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

static gboolean
synapticsmst_tool_scan_aux_nodes (SynapticsMSTToolPrivate *priv, GError **error)
{
	gboolean ret = FALSE;

	priv->device_array = g_ptr_array_new ();
	for (guint8 i = 0; i < MAX_DP_AUX_NODES; i++) {
		if (synapticsmst_common_open_aux_node(synapticsmst_device_get_aux_node (i))) {
			g_autoptr(SynapticsMSTDevice) device = NULL;
			device = synapticsmst_device_new (SYNAPTICSMST_DEVICE_KIND_DIRECT,
							  synapticsmst_device_get_aux_node(i));
			g_ptr_array_add (priv->device_array, g_object_ref(device));
			synapticsmst_common_close_aux_node ();
			ret = TRUE;
		}
	}

	if (!ret) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "No Synaptics MST Device found");
	}

	return ret;
}

static gboolean
synapticsmst_tool_enumerate (SynapticsMSTToolPrivate *priv,
			     gchar **values,
			     guint8 device_index,
			     GError **error)
{
	SynapticsMSTDevice *device = NULL;

	g_print ("\nMST Devices :\n");

	for (guint8 i = 0; i < priv->device_array->len; i++) {
		device = g_ptr_array_index (priv->device_array, i);
		g_print ("[Device %1d]\n", i+1);
		if (synapticsmst_device_enumerate_device (device, error)) {
			const gchar *boardID = synapticsmst_device_boardID_to_string (synapticsmst_device_get_boardID (device));
			if (boardID != NULL) {
				g_print ("Device : %s with Synaptics %s\n",
					 boardID,
					 synapticsmst_device_get_chipID (device));
				g_print ("Connect Type : %s in DP Aux Node %d\n",
					 synapticsmst_device_kind_to_string (synapticsmst_device_get_kind (device)),
					 synapticsmst_device_get_aux_node_to_int (device));
				g_print ("Firmware version : %s\n", synapticsmst_device_get_version (device));
			} else {
				g_print ("Unknown Device\n");
			}
			g_print ("\n");
		} else {
			return FALSE;
		}
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

	/* incorrect args */
	if (g_strv_length (values) != 1) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "Incorrect arguments, expected FILENAME]");
		return FALSE;
	}

	device = g_ptr_array_index(priv->device_array, (device_index - 1));
	if (synapticsmst_device_enumerate_device (device, error)) {
		if (synapticsmst_device_boardID_to_string (synapticsmst_device_get_boardID (device)) != NULL) {
			g_autoptr(GError) error_local = NULL;
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

			fw = g_bytes_new(data, len);
			if (!synapticsmst_device_write_firmware (device, fw, NULL, NULL, error))
				return FALSE;
			g_print ("Update Sucessfully. Please reset device to apply new firmware.\n");
		} else {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "Failed to flash firmware: unknown device");
			return FALSE;
		}
	} else {
		return FALSE;
	}

	return TRUE;
}

static gboolean
synapticsmst_tool_run (SynapticsMSTToolPrivate *priv,
		       const gchar *command,
		       gchar **values,
		       guint8 device_index,
		       GError **error)
{
	guint i;
	FuUtilItem *item;

	/* find command */
	for (i = 0; i < priv->cmd_array->len; i++) {
		item = g_ptr_array_index (priv->cmd_array, i);
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
	g_autoptr(SynapticsMSTToolPrivate) priv = g_new0 (SynapticsMSTToolPrivate, 1);
	//g_autoptr(SynapticsMSTDevice) device = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GOptionContext) context = NULL;
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

	/* check avaliable dp aux nodes and add devices */
	ret = synapticsmst_tool_scan_aux_nodes(priv, &error);
	if (!ret) {
		g_print ("%s\n", error->message);
		return EXIT_FAILURE;
	}

	/* run the specified command */
	if (argc == 4) {
		device_index = strtol(argv[3], NULL, 10);
	}

	ret = synapticsmst_tool_run (priv, argv[1], (gchar**) &argv[2], device_index, &error);
	if (!ret) {
		g_print ("%s\n", error->message);
		return EXIT_FAILURE;
	}

	/* success/ */
	return EXIT_SUCCESS;
}
