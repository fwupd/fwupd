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

#include <dfu.h>
#include <libintl.h>
#include <locale.h>
#include <stdlib.h>
#include <glib/gi18n.h>
#include <glib-unix.h>
#include <appstream-glib.h>

#include "dfu-device-private.h"

typedef struct {
	GCancellable		*cancellable;
	GPtrArray		*cmd_array;
	gboolean		 force;
	gchar			*device_vid_pid;
	guint16			 transfer_size;
} DfuToolPrivate;

/**
 * dfu_tool_print_indent:
 **/
static void
dfu_tool_print_indent (const gchar *title, const gchar *message, guint indent)
{
	guint i;
	for (i = 0; i < indent; i++)
		g_print (" ");
	g_print ("%s:", title);
	for (i = strlen (title) + indent; i < 15; i++)
		g_print (" ");
	g_print ("%s\n", message);
}

/**
 * dfu_tool_private_free:
 **/
static void
dfu_tool_private_free (DfuToolPrivate *priv)
{
	if (priv == NULL)
		return;
	g_free (priv->device_vid_pid);
	g_object_unref (priv->cancellable);
	if (priv->cmd_array != NULL)
		g_ptr_array_unref (priv->cmd_array);
	g_free (priv);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC(DfuToolPrivate, dfu_tool_private_free)

typedef gboolean (*FuUtilPrivateCb)	(DfuToolPrivate	*util,
					 gchar		**values,
					 GError		**error);

typedef struct {
	gchar		*name;
	gchar		*arguments;
	gchar		*description;
	FuUtilPrivateCb	 callback;
} FuUtilItem;

/**
 * dfu_tool_item_free:
 **/
static void
dfu_tool_item_free (FuUtilItem *item)
{
	g_free (item->name);
	g_free (item->arguments);
	g_free (item->description);
	g_free (item);
}

/**
 * dfu_tool_sort_command_name_cb:
 **/
static gint
dfu_tool_sort_command_name_cb (FuUtilItem **item1, FuUtilItem **item2)
{
	return g_strcmp0 ((*item1)->name, (*item2)->name);
}

/**
 * dfu_tool_add:
 **/
static void
dfu_tool_add (GPtrArray *array,
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
 * dfu_tool_get_descriptions:
 **/
static gchar *
dfu_tool_get_descriptions (GPtrArray *array)
{
	guint i;
	guint j;
	guint len;
	const guint max_len = 31;
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
 * dfu_tool_run:
 **/
static gboolean
dfu_tool_run (DfuToolPrivate *priv,
	      const gchar *command,
	      gchar **values,
	      GError **error)
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
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     /* TRANSLATORS: error message */
			     _("Command not found"));
	return FALSE;
}

/**
 * dfu_tool_get_defalt_device:
 **/
static DfuDevice *
dfu_tool_get_defalt_device (DfuToolPrivate *priv, GError **error)
{
	DfuDevice *device;
	g_autoptr(DfuContext) dfu_context = NULL;

	/* get all the DFU devices */
	dfu_context = dfu_context_new ();
	dfu_context_enumerate (dfu_context, NULL);

	/* we specified it manually */
	if (priv->device_vid_pid != NULL) {
		gchar *tmp;
		guint64 pid;
		guint64 vid;

		/* parse */
		vid = g_ascii_strtoull (priv->device_vid_pid, &tmp, 16);
		if (vid == 0 || vid > G_MAXUINT16) {
			g_set_error_literal (error,
					     DFU_ERROR,
					     DFU_ERROR_INTERNAL,
					     "Invalid format of VID:PID");
			return NULL;
		}
		if (tmp[0] != ':') {
			g_set_error_literal (error,
					     DFU_ERROR,
					     DFU_ERROR_INTERNAL,
					     "Invalid format of VID:PID");
			return NULL;
		}
		pid = g_ascii_strtoull (tmp + 1, NULL, 16);
		if (vid == 0 || vid > G_MAXUINT16) {
			g_set_error_literal (error,
					     DFU_ERROR,
					     DFU_ERROR_INTERNAL,
					     "Invalid format of VID:PID");
			return NULL;
		}

		/* find device */
		device = dfu_context_get_device_by_vid_pid (dfu_context,
							    vid, pid,
							    error);
		if (device == NULL)
			return NULL;
	} else {
		/* auto-detect first device */
		device = dfu_context_get_device_default (dfu_context, error);
		if (device == NULL)
			return NULL;
	}

	/* this has to be added to the device so we can deal with detach */
	g_object_set_data_full (G_OBJECT (device), "DfuContext",
				g_object_ref (dfu_context),
				(GDestroyNotify) g_object_unref);
	return device;
}

/**
 * dfu_tool_set_vendor:
 **/
static gboolean
dfu_tool_set_vendor (DfuToolPrivate *priv, gchar **values, GError **error)
{
	guint64 tmp;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GFile) file = NULL;

	/* check args */
	if (g_strv_length (values) < 2) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "Invalid arguments, expected FILE VID"
				     " -- e.g. `firmware.dfu 273f");
		return FALSE;
	}

	/* open */
	file = g_file_new_for_path (values[0]);
	firmware = dfu_firmware_new ();
	if (!dfu_firmware_parse_file (firmware, file,
				      DFU_FIRMWARE_PARSE_FLAG_NONE,
				      priv->cancellable,
				      error)) {
		return FALSE;
	}

	/* parse VID */
	tmp = g_ascii_strtoull (values[1], NULL, 16);
	if (tmp == 0 || tmp > 0xffff) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "Failed to parse VID '%s'",
			     values[1]);
		return FALSE;
	}
	dfu_firmware_set_vid (firmware, tmp);

	/* write out new file */
	return dfu_firmware_write_file (firmware,
					file,
					priv->cancellable,
					error);
}

/**
 * dfu_tool_set_product:
 **/
static gboolean
dfu_tool_set_product (DfuToolPrivate *priv, gchar **values, GError **error)
{
	guint64 tmp;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GFile) file = NULL;

	/* check args */
	if (g_strv_length (values) < 2) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "Invalid arguments, expected FILE PID"
				     " -- e.g. `firmware.dfu 1004");
		return FALSE;
	}

	/* open */
	file = g_file_new_for_path (values[0]);
	firmware = dfu_firmware_new ();
	if (!dfu_firmware_parse_file (firmware, file,
				      DFU_FIRMWARE_PARSE_FLAG_NONE,
				      priv->cancellable,
				      error)) {
		return FALSE;
	}

	/* parse VID */
	tmp = g_ascii_strtoull (values[1], NULL, 16);
	if (tmp == 0 || tmp > 0xffff) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "Failed to parse PID '%s'", values[1]);
		return FALSE;
	}
	dfu_firmware_set_pid (firmware, tmp);

	/* write out new file */
	return dfu_firmware_write_file (firmware,
					file,
					priv->cancellable,
					error);
}

/**
 * dfu_tool_set_release:
 **/
static gboolean
dfu_tool_set_release (DfuToolPrivate *priv, gchar **values, GError **error)
{
	guint64 tmp;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GFile) file = NULL;

	/* check args */
	if (g_strv_length (values) < 2) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "Invalid arguments, expected FILE RELEASE"
				     " -- e.g. `firmware.dfu ffff");
		return FALSE;
	}

	/* open */
	file = g_file_new_for_path (values[0]);
	firmware = dfu_firmware_new ();
	if (!dfu_firmware_parse_file (firmware, file,
				      DFU_FIRMWARE_PARSE_FLAG_NONE,
				      priv->cancellable,
				      error)) {
		return FALSE;
	}

	/* parse VID */
	tmp = g_ascii_strtoull (values[1], NULL, 16);
	if (tmp == 0 || tmp > 0xffff) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "Failed to parse release '%s'", values[1]);
		return FALSE;
	}
	dfu_firmware_set_release (firmware, tmp);

	/* write out new file */
	return dfu_firmware_write_file (firmware,
					file,
					priv->cancellable,
					error);
}

/**
 * dfu_tool_set_metadata:
 **/
static gboolean
dfu_tool_set_metadata (DfuToolPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GFile) file = NULL;

	/* check args */
	if (g_strv_length (values) < 3) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "Invalid arguments, expected FILE KEY VALUE"
				     " -- e.g. `firmware.dfu Licence GPL-2.0+");
		return FALSE;
	}

	/* open */
	file = g_file_new_for_path (values[0]);
	firmware = dfu_firmware_new ();
	if (!dfu_firmware_parse_file (firmware, file,
				      DFU_FIRMWARE_PARSE_FLAG_NONE,
				      priv->cancellable,
				      error)) {
		return FALSE;
	}

	/* doesn't make sense for non-DFU */
	if (dfu_firmware_get_format (firmware) == DFU_FIRMWARE_FORMAT_RAW) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "Only possible on DFU/DfuSe images, try convert");
		return FALSE;
	}

	/* set metadata */
	dfu_firmware_set_metadata (firmware, values[1], values[2]);

	/* write out new file */
	return dfu_firmware_write_file (firmware,
					file,
					priv->cancellable,
					error);
}

/**
 * dfu_tool_set_alt_setting:
 **/
static gboolean
dfu_tool_set_alt_setting (DfuToolPrivate *priv, gchar **values, GError **error)
{
	DfuImage *image;
	guint64 tmp;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GFile) file = NULL;

	/* check args */
	if (g_strv_length (values) < 2) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "Invalid arguments, expected FILE ALT-ID"
				     " -- e.g. `firmware.dfu 1");
		return FALSE;
	}

	/* open */
	file = g_file_new_for_path (values[0]);
	firmware = dfu_firmware_new ();
	if (!dfu_firmware_parse_file (firmware, file,
				      DFU_FIRMWARE_PARSE_FLAG_NONE,
				      priv->cancellable,
				      error)) {
		return FALSE;
	}

	/* doesn't make sense for non-DfuSe */
	if (dfu_firmware_get_format (firmware) != DFU_FIRMWARE_FORMAT_DFUSE) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "Only possible on DfuSe images, try convert");
		return FALSE;
	}

	/* parse VID */
	tmp = g_ascii_strtoull (values[1], NULL, 10);
	if (tmp == 0 || tmp > 0xff) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "Failed to parse alternative setting '%s'",
			     values[1]);
		return FALSE;
	}
	image = dfu_firmware_get_image_default (firmware);
	if (image == NULL) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "found no image '%s'", values[1]);
		return FALSE;
	}
	dfu_image_set_alt_setting (image, tmp);

	/* write out new file */
	return dfu_firmware_write_file (firmware,
					file,
					priv->cancellable,
					error);
}

/**
 * dfu_tool_set_alt_setting_name:
 **/
static gboolean
dfu_tool_set_alt_setting_name (DfuToolPrivate *priv, gchar **values, GError **error)
{
	DfuImage *image;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GFile) file = NULL;

	/* check args */
	if (g_strv_length (values) < 2) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "Invalid arguments, expected FILE ALT-NAME"
				     " -- e.g. `firmware.dfu ST");
		return FALSE;
	}

	/* open */
	file = g_file_new_for_path (values[0]);
	firmware = dfu_firmware_new ();
	if (!dfu_firmware_parse_file (firmware, file,
				      DFU_FIRMWARE_PARSE_FLAG_NONE,
				      priv->cancellable,
				      error)) {
		return FALSE;
	}

	/* doesn't make sense for non-DfuSe */
	if (dfu_firmware_get_format (firmware) != DFU_FIRMWARE_FORMAT_DFUSE) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "Only possible on DfuSe images, try convert");
		return FALSE;
	}

	/* parse VID */
	image = dfu_firmware_get_image_default (firmware);
	if (image == NULL) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "found no image '%s'", values[1]);
		return FALSE;
	}
	dfu_image_set_name (image, values[1]);

	/* write out new file */
	return dfu_firmware_write_file (firmware,
					file,
					priv->cancellable,
					error);
}

/**
 * dfu_tool_merge:
 **/
static gboolean
dfu_tool_merge (DfuToolPrivate *priv, gchar **values, GError **error)
{
	guint16 pid = 0xffff;
	guint16 rel = 0xffff;
	guint16 vid = 0xffff;
	guint i;
	g_autofree gchar *str_debug = NULL;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GFile) file = NULL;

	/* check args */
	if (g_strv_length (values) < 3) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "Invalid arguments, expected "
				     "FILE-OUT FILE1 FILE2 [FILE3...]"
				     " -- e.g. `combined.dfu lib.dfu app.dfu`");
		return FALSE;
	}

	/* parse source files */
	firmware = dfu_firmware_new ();
	dfu_firmware_set_format (firmware, DFU_FIRMWARE_FORMAT_DFUSE);
	for (i = 1; values[i] != NULL; i++) {
		GPtrArray *images;
		guint j;
		g_autoptr(GFile) file_tmp = NULL;
		g_autoptr(DfuFirmware) firmware_tmp = NULL;

		/* open up source */
		file_tmp = g_file_new_for_path (values[i]);
		firmware_tmp = dfu_firmware_new ();
		if (!dfu_firmware_parse_file (firmware_tmp, file_tmp,
					      DFU_FIRMWARE_PARSE_FLAG_NONE,
					      priv->cancellable,
					      error)) {
			return FALSE;
		}

		/* check same vid:pid:rel */
		if (vid != 0xffff &&
		    dfu_firmware_get_vid (firmware_tmp) != vid) {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_INVALID_FILE,
				     "Vendor ID was already set as "
				     "0x%04x, %s is 0x%04x",
				     vid, values[i],
				     dfu_firmware_get_vid (firmware_tmp));
			return FALSE;
		}
		if (pid != 0xffff &&
		    dfu_firmware_get_pid (firmware_tmp) != pid) {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_INVALID_FILE,
				     "Product ID was already set as "
				     "0x%04x, %s is 0x%04x",
				     pid, values[i],
				     dfu_firmware_get_pid (firmware_tmp));
			return FALSE;
		}
		if (rel != 0xffff &&
		    dfu_firmware_get_release (firmware_tmp) != rel) {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_INVALID_FILE,
				     "Release was already set as "
				     "0x%04x, %s is 0x%04x",
				     rel, values[i],
				     dfu_firmware_get_release (firmware_tmp));
			return FALSE;
		}

		/* add all images to destination */
		images = dfu_firmware_get_images (firmware_tmp);
		for (j = 0; j < images->len; j++) {
			DfuImage *image;
			guint alt_id;

			/* verify the alt-setting does not already exist */
			image = g_ptr_array_index (images, j);
			alt_id = dfu_image_get_alt_setting (image);
			g_print ("Adding alternative setting ID of 0x%02x\n",
				 alt_id);
			if (dfu_firmware_get_image (firmware, alt_id) != NULL) {
				if (!priv->force) {
					g_set_error (error,
						     DFU_ERROR,
						     DFU_ERROR_INVALID_FILE,
						     "The alternative setting ID "
						     "of 0x%02x has already been added",
						     alt_id);
					return FALSE;
				}
				g_print ("WARNING: The alternative setting "
					 "ID of 0x%02x has already been added\n",
					 alt_id);
			}

			/* add to destination */
			dfu_firmware_add_image (firmware, image);
		}

		/* save last IDs */
		vid = dfu_firmware_get_vid (firmware_tmp);
		pid = dfu_firmware_get_pid (firmware_tmp);
		rel = dfu_firmware_get_release (firmware_tmp);
	}

	/* print the new object */
	str_debug = dfu_firmware_to_string (firmware);
	g_print ("New merged file:\n%s\n", str_debug);

	/* write out new file */
	file = g_file_new_for_path (values[0]);
	return dfu_firmware_write_file (firmware,
					file,
					priv->cancellable,
					error);
}

/**
 * dfu_tool_convert:
 **/
static gboolean
dfu_tool_convert (DfuToolPrivate *priv, gchar **values, GError **error)
{
	guint64 tmp;
	guint argc = g_strv_length (values);
	g_autofree gchar *str_debug = NULL;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GFile) file_in = NULL;
	g_autoptr(GFile) file_out = NULL;

	/* check args */
	if (argc < 3 || argc > 4) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "Invalid arguments, expected "
				     "FORMAT FILE-IN FILE-OUT [SIZE]"
				     " -- e.g. `dfu firmware.hex firmware.dfu 8000`");
		return FALSE;
	}

	/* parse file */
	file_in = g_file_new_for_path (values[1]);
	file_out = g_file_new_for_path (values[2]);
	firmware = dfu_firmware_new ();
	if (!dfu_firmware_parse_file (firmware, file_in,
				      DFU_FIRMWARE_PARSE_FLAG_NONE,
				      priv->cancellable,
				      error)) {
		return FALSE;
	}

	/* set output format */
	if (g_strcmp0 (values[0], "raw") == 0) {
		dfu_firmware_set_format (firmware, DFU_FIRMWARE_FORMAT_RAW);
	} else if (g_strcmp0 (values[0], "dfu") == 0) {
		dfu_firmware_set_format (firmware, DFU_FIRMWARE_FORMAT_DFU_1_0);
	} else if (g_strcmp0 (values[0], "dfuse") == 0) {
		dfu_firmware_set_format (firmware, DFU_FIRMWARE_FORMAT_DFUSE);
	} else if (g_strcmp0 (values[0], "ihex") == 0) {
		dfu_firmware_set_format (firmware, DFU_FIRMWARE_FORMAT_INTEL_HEX);
	} else {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "unknown format '%s', expected [raw|dfu|dfuse|ihex]",
			     values[0]);
		return FALSE;
	}

	/* set target size */
	if (argc > 3) {
		DfuImage *image;
		DfuElement *element;
		gchar *endptr;
		tmp = g_ascii_strtoull (values[3], &endptr, 16);
		if (tmp > 0xffff || endptr[0] != '\0') {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "Failed to parse target size '%s'",
				     values[3]);
			return FALSE;
		}

		/* doesn't make sense for DfuSe */
		if (dfu_firmware_get_format (firmware) == DFU_FIRMWARE_FORMAT_DFUSE) {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "Cannot pad DfuSe image, try DFU");
			return FALSE;
		}

		/* this has to exist */
		if (tmp > 0) {
			image = dfu_firmware_get_image_default (firmware);
			g_assert (image != NULL);
			element = dfu_image_get_element (image, 0);
			dfu_element_set_target_size (element, tmp);
		}
	}

	/* print the new object */
	str_debug = dfu_firmware_to_string (firmware);
	g_debug ("DFU: %s", str_debug);

	/* write out new file */
	return dfu_firmware_write_file (firmware,
					file_out,
					priv->cancellable,
					error);
}

/**
 * dfu_tool_attach:
 **/
static gboolean
dfu_tool_attach (DfuToolPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(DfuDevice) device = NULL;

	device = dfu_tool_get_defalt_device (priv, error);
	if (device == NULL)
		return FALSE;
	if (!dfu_device_open (device,
			      DFU_DEVICE_OPEN_FLAG_NONE,
			      priv->cancellable,
			      error))
		return FALSE;
	if (!dfu_device_attach (device, error))
		return FALSE;
	return TRUE;
}

typedef struct {
	guint		 marks_total;
	guint		 marks_shown;
	DfuState	 last_state;
} DfuToolProgressHelper;

/**
 * fu_tool_state_changed_cb:
 **/
static void
fu_tool_state_changed_cb (DfuDevice *device,
			  DfuState state,
			  DfuToolProgressHelper *helper)
{
	const gchar *title = NULL;
	guint i;

	/* changed state */
	if (state == helper->last_state)
		return;

	/* state was left hanging... */
	if (helper->marks_shown == 0) {
		switch (helper->last_state) {
		case DFU_STATE_APP_DETACH:
		case DFU_STATE_DFU_DNLOAD_IDLE:
		case DFU_STATE_DFU_MANIFEST_WAIT_RESET:
		case DFU_STATE_DFU_UPLOAD_IDLE:
			/* TRANSLATORS: when an action has completed */
			g_print ("%s\n", _("OK"));
			break;
		default:
			g_debug ("ignore last state transition %s",
				 dfu_state_to_string (helper->last_state));
			break;
		}
	}

	switch (state) {
	case DFU_STATE_APP_DETACH:
		/* TRANSLATORS: when moving from runtime to DFU mode */
		title = _("Detaching");
		break;
	case DFU_STATE_DFU_MANIFEST_WAIT_RESET:
		/* TRANSLATORS: when moving from DFU to runtime mode */
		title = _("Attaching");
		break;
	case DFU_STATE_DFU_DNLOAD_IDLE:
		/* TRANSLATORS: when copying from host to device */
		title = _("Downloading");
		break;
	case DFU_STATE_DFU_UPLOAD_IDLE:
		/* TRANSLATORS: when copying from device to host */
		title = _("Uploading");
		break;
	default:
		g_debug ("ignoring %s", dfu_state_to_string (state));
		break;
	}

	/* show title and then pad */
	if (title != NULL) {
		g_print ("%s ", title);
		for (i = strlen (title); i < 15; i++)
			g_print (" ");
		g_print (": ");
	}

	/* reset the progress bar */
	switch (state) {
	case DFU_STATE_APP_DETACH:
	case DFU_STATE_DFU_DNLOAD_IDLE:
	case DFU_STATE_DFU_MANIFEST_WAIT_RESET:
	case DFU_STATE_DFU_UPLOAD_IDLE:
		g_debug ("resetting progress bar");
		helper->marks_shown = 0;
		break;
	default:
		break;
	}

	/* ignore if the same */
	helper->last_state = state;
}

/**
 * fu_tool_percentage_changed_cb:
 **/
static void
fu_tool_percentage_changed_cb (DfuDevice *device,
			       guint percentage,
			       DfuToolProgressHelper *helper)
{
	guint marks_now;
	guint i;

	/* add any sections */
	marks_now = percentage * helper->marks_total / 100;
	for (i = helper->marks_shown; i < marks_now; i++)
		g_print ("#");
	helper->marks_shown = marks_now;

	/* this state done */
	if (percentage == 100)
		g_print ("\n");
}

/**
 * dfu_tool_read_alt:
 **/
static gboolean
dfu_tool_read_alt (DfuToolPrivate *priv, gchar **values, GError **error)
{
	DfuTargetTransferFlags flags = DFU_TARGET_TRANSFER_FLAG_NONE;
	DfuToolProgressHelper helper;
	g_autofree gchar *str_debug = NULL;
	g_autoptr(DfuDevice) device = NULL;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(DfuImage) image = NULL;
	g_autoptr(DfuTarget) target = NULL;
	g_autoptr(GFile) file = NULL;

	/* check args */
	if (g_strv_length (values) < 2) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "Invalid arguments, expected "
				     "FILENAME DEVICE-ALT-NAME|DEVICE-ALT-ID");
		return FALSE;
	}

	/* open correct device */
	device = dfu_tool_get_defalt_device (priv, error);
	if (device == NULL)
		return FALSE;
	if (priv->transfer_size > 0)
		dfu_device_set_transfer_size (device, priv->transfer_size);
	if (!dfu_device_open (device,
			      DFU_DEVICE_OPEN_FLAG_NONE,
			      priv->cancellable,
			      error))
		return FALSE;

	/* set up progress */
	helper.last_state = DFU_STATE_DFU_ERROR;
	helper.marks_total = 30;
	helper.marks_shown = 0;
	g_signal_connect (device, "state-changed",
			  G_CALLBACK (fu_tool_state_changed_cb), &helper);
	g_signal_connect (device, "percentage-changed",
			  G_CALLBACK (fu_tool_percentage_changed_cb), &helper);

	/* APP -> DFU */
	if (dfu_device_get_mode (device) == DFU_MODE_RUNTIME) {
		g_debug ("detaching");
		if (!dfu_device_detach (device, priv->cancellable, error))
			return FALSE;
		if (!dfu_device_wait_for_replug (device,
						 DFU_DEVICE_REPLUG_TIMEOUT,
						 priv->cancellable,
						 error))
			return FALSE;

		/* put back in same state */
		flags |= DFU_TARGET_TRANSFER_FLAG_ATTACH;
		flags |= DFU_TARGET_TRANSFER_FLAG_WAIT_RUNTIME;
	}

	/* transfer */
	target = dfu_device_get_target_by_alt_name (device,
						    values[1],
						    NULL);
	if (target == NULL) {
		gchar *endptr;
		guint64 tmp = g_ascii_strtoull (values[1], &endptr, 10);
		if (tmp > 0xff || endptr[0] != '\0') {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "Failed to parse alt-setting '%s'",
				     values[1]);
			return FALSE;
		}
		target = dfu_device_get_target_by_alt_setting (device,
							       tmp,
							       error);
		if (target == NULL)
			return FALSE;
	}

	/* do transfer */
	image = dfu_target_upload (target, flags, priv->cancellable, error);
	if (image == NULL)
		return FALSE;

	/* create new firmware object */
	firmware = dfu_firmware_new ();
	dfu_firmware_set_format (firmware, DFU_FIRMWARE_FORMAT_DFU_1_0);
	dfu_firmware_set_vid (firmware, dfu_device_get_runtime_vid (device));
	dfu_firmware_set_pid (firmware, dfu_device_get_runtime_pid (device));
	dfu_firmware_add_image (firmware, image);

	/* save file */
	file = g_file_new_for_path (values[0]);
	if (!dfu_firmware_write_file (firmware,
				      file,
				      priv->cancellable,
				      error))
		return FALSE;

	/* print the new object */
	str_debug = dfu_firmware_to_string (firmware);
	g_debug ("DFU: %s", str_debug);

	/* success */
	g_print ("%u bytes successfully uploaded from device\n",
		 dfu_image_get_size (image));
	return TRUE;
}

/**
 * dfu_tool_read:
 **/
static gboolean
dfu_tool_read (DfuToolPrivate *priv, gchar **values, GError **error)
{
	DfuTargetTransferFlags flags = DFU_TARGET_TRANSFER_FLAG_NONE;
	DfuToolProgressHelper helper;
	g_autofree gchar *str_debug = NULL;
	g_autoptr(DfuDevice) device = NULL;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(DfuImage) image = NULL;
	g_autoptr(GFile) file = NULL;

	/* check args */
	if (g_strv_length (values) < 1) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "Invalid arguments, expected FILENAME");
		return FALSE;
	}

	/* open correct device */
	device = dfu_tool_get_defalt_device (priv, error);
	if (device == NULL)
		return FALSE;
	if (!dfu_device_open (device,
			      DFU_DEVICE_OPEN_FLAG_NONE,
			      priv->cancellable,
			      error))
		return FALSE;

	/* optional reset */
	if (dfu_device_get_mode (device) == DFU_MODE_RUNTIME) {
		flags |= DFU_TARGET_TRANSFER_FLAG_DETACH;
		flags |= DFU_TARGET_TRANSFER_FLAG_ATTACH;
		flags |= DFU_TARGET_TRANSFER_FLAG_WAIT_RUNTIME;
	}

	/* transfer */
	helper.last_state = DFU_STATE_DFU_ERROR;
	helper.marks_total = 30;
	helper.marks_shown = 0;
	g_signal_connect (device, "state-changed",
			  G_CALLBACK (fu_tool_state_changed_cb), &helper);
	g_signal_connect (device, "percentage-changed",
			  G_CALLBACK (fu_tool_percentage_changed_cb), &helper);
	firmware = dfu_device_upload (device,
				      flags,
				      priv->cancellable,
				      error);
	if (firmware == NULL)
		return FALSE;

	/* save file */
	file = g_file_new_for_path (values[0]);
	if (!dfu_firmware_write_file (firmware,
				      file,
				      priv->cancellable,
				      error))
		return FALSE;

	/* print the new object */
	str_debug = dfu_firmware_to_string (firmware);
	g_debug ("DFU: %s", str_debug);

	/* success */
	g_print ("%u bytes successfully uploaded from device\n",
		 dfu_firmware_get_size (firmware));
	return TRUE;
}

/**
 * dfu_tool_get_device_string:
 **/
static gchar *
dfu_tool_get_device_string (DfuToolPrivate *priv, DfuDevice *device)
{
	gchar *dstr;
	GUsbDevice *dev;
	g_autoptr(GError) error = NULL;

	/* open, and get status */
	dev = dfu_device_get_usb_dev (device);
	if (dev == NULL) {
		return g_strdup_printf ("%04x:%04x [%s]",
					dfu_device_get_runtime_vid (device),
					dfu_device_get_runtime_pid (device),
					"removed");
	}
	if (!dfu_device_open (device,
			      DFU_DEVICE_OPEN_FLAG_NONE,
			      priv->cancellable,
			      &error)) {
		return g_strdup_printf ("%04x:%04x [%s]",
					g_usb_device_get_vid (dev),
					g_usb_device_get_pid (dev),
					error->message);
	}
	dstr = g_strdup_printf ("%04x:%04x [%s:%s]",
				g_usb_device_get_vid (dev),
				g_usb_device_get_pid (dev),
				dfu_state_to_string (dfu_device_get_state (device)),
				dfu_status_to_string (dfu_device_get_status (device)));
	dfu_device_close (device, NULL);
	return dstr;
}

/**
 * dfu_tool_device_added_cb:
 **/
static void
dfu_tool_device_added_cb (DfuContext *context,
			  DfuDevice *device,
			  gpointer user_data)
{
	DfuToolPrivate *priv = (DfuToolPrivate *) user_data;
	g_autofree gchar *tmp = dfu_tool_get_device_string (priv, device);
	/* TRANSLATORS: this is when a device is hotplugged */
	dfu_tool_print_indent (_("Added"), tmp, 0);
}

/**
 * dfu_tool_device_removed_cb:
 **/
static void
dfu_tool_device_removed_cb (DfuContext *context,
			    DfuDevice *device,
			    gpointer user_data)
{
	DfuToolPrivate *priv = (DfuToolPrivate *) user_data;
	g_autofree gchar *tmp = dfu_tool_get_device_string (priv, device);
	/* TRANSLATORS: this is when a device is hotplugged */
	dfu_tool_print_indent (_("Removed"), tmp, 0);
}

/**
 * dfu_tool_device_changed_cb:
 **/
static void
dfu_tool_device_changed_cb (DfuContext *context, DfuDevice *device, gpointer user_data)
{
	DfuToolPrivate *priv = (DfuToolPrivate *) user_data;
	g_autofree gchar *tmp = dfu_tool_get_device_string (priv, device);
	/* TRANSLATORS: this is when a device is hotplugged */
	dfu_tool_print_indent (_("Changed"), tmp, 0);
}

/**
 * dfu_tool_watch_cancelled_cb:
 **/
static void
dfu_tool_watch_cancelled_cb (GCancellable *cancellable, gpointer user_data)
{
	GMainLoop *loop = (GMainLoop *) user_data;
	/* TRANSLATORS: this is when a device ctrl+c's a watch */
	g_print ("%s\n", _("Cancelled"));
	g_main_loop_quit (loop);
}

/**
 * dfu_tool_parse_xtea_key:
 **/
static gboolean
dfu_tool_parse_xtea_key (const gchar *key, guint32 *keys, GError **error)
{
	guint i;
	guint key_len;
	g_autofree gchar *key_pad = NULL;

	/* too long */
	key_len = strlen (key);
	if (key_len > 32) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_NOT_SUPPORTED,
			     "Key string too long at %i chars, max 16",
			     key_len);
		return FALSE;
	}

	/* parse 4x32b values or generate a hash */
	if (key_len == 32) {
		for (i = 0; i < 4; i++) {
			gchar buf[] = "xxxxxxxx";
			gchar *endptr;
			guint64 tmp;

			/* copy to 4-char buf (with NUL) */
			memcpy (buf, key + i*8, 8);
			tmp = g_ascii_strtoull (buf, &endptr, 16);
			if (endptr && endptr[0] != '\0') {
				g_set_error (error,
					     DFU_ERROR,
					     DFU_ERROR_NOT_SUPPORTED,
					     "Failed to parse key '%s'", key);
				return FALSE;
			}
			keys[3-i] = tmp;
		}
	} else {
		gsize buf_len = 16;
		g_autoptr(GChecksum) csum = NULL;
		csum = g_checksum_new (G_CHECKSUM_MD5);
		g_checksum_update (csum, (const guchar *) key, key_len);
		g_checksum_get_digest (csum, (guint8 *) keys, &buf_len);
		g_assert (buf_len == 16);
	}

	/* success */
	g_debug ("using XTEA key %04x%04x%04x%04x",
		 keys[3], keys[2], keys[1], keys[0]);
	return TRUE;
}

/**
 * dfu_tool_get_firmware_contents_default:
 **/
static guint8 *
dfu_tool_get_firmware_contents_default (DfuFirmware *firmware,
					gsize *length,
					GError **error)
{
	DfuElement *element;
	DfuImage *image;
	GBytes *contents;

	image = dfu_firmware_get_image_default (firmware);
	if (image == NULL) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "No default image");
		return NULL;
	}
	element = dfu_image_get_element (image, 0);
	if (element == NULL) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "No default element");
		return NULL;
	}
	contents = dfu_element_get_contents (element);
	if (contents == NULL) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "No image contents");
		return NULL;
	}
	return (guint8 *) g_bytes_get_data (contents, length);
}

#define XTEA_DELTA		0x9e3779b9
#define XTEA_NUM_ROUNDS		32

/**
 * dfu_tool_encrypt_xtea:
 **/
static void
dfu_tool_encrypt_xtea (const guint32 key[4], guint8 *data, guint16 length)
{
	guint32 sum;
	guint32 *tmp = (guint32 *) data;
	guint32 v0;
	guint32 v1;
	guint8 i;
	guint j;

	for (j = 0; j < length / 4; j += 2) {
		sum = 0;
		v0 = tmp[j];
		v1 = tmp[j+1];
		for (i = 0; i < XTEA_NUM_ROUNDS; i++) {
			v0 += (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + key[sum & 3]);
			sum += XTEA_DELTA;
			v1 += (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + key[(sum >> 11) & 3]);
		}
		tmp[j] = v0;
		tmp[j+1] = v1;
	}
}

/**
 * dfu_tool_decrypt_xtea:
 **/
static void
dfu_tool_decrypt_xtea (const guint32 key[4], guint8 *data, guint16 length)
{
	guint32 sum;
	guint32 *tmp = (guint32 *) data;
	guint32 v0;
	guint32 v1;
	guint8 i;
	guint j;

	for (j = 0; j < length / 4; j += 2) {
		v0 = tmp[j];
		v1 = tmp[j+1];
		sum = XTEA_DELTA * XTEA_NUM_ROUNDS;
		for (i = 0; i < XTEA_NUM_ROUNDS; i++) {
			v1 -= (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + key[(sum >> 11) & 3]);
			sum -= XTEA_DELTA;
			v0 -= (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + key[sum & 3]);
		}
		tmp[j] = v0;
		tmp[j+1] = v1;
	}
}

/**
 * dfu_tool_encrypt:
 **/
static gboolean
dfu_tool_encrypt (DfuToolPrivate *priv, gchar **values, GError **error)
{
	gsize len;
	guint8 *data;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GFile) file_in = NULL;
	g_autoptr(GFile) file_out = NULL;

	/* check args */
	if (g_strv_length (values) < 4) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "Invalid arguments, expected "
				     "FILENAME-IN FILENAME-OUT TYPE KEY"
				     " -- e.g. firmware.dfu firmware.xdfu xtea deadbeef");
		return FALSE;
	}

	/* check extensions */
	if (!priv->force) {
		if (!g_str_has_suffix (values[0], ".dfu")) {
			g_set_error_literal (error,
					     DFU_ERROR,
					     DFU_ERROR_NOT_SUPPORTED,
					     "Invalid filename, expected *.dfu");
			return FALSE;
		}
		if (!g_str_has_suffix (values[1], ".xdfu")) {
			g_set_error_literal (error,
					     DFU_ERROR,
					     DFU_ERROR_NOT_SUPPORTED,
					     "Invalid filename, expected *.xdfu");
			return FALSE;
		}
	}

	/* open */
	file_in = g_file_new_for_path (values[0]);
	firmware = dfu_firmware_new ();
	if (!dfu_firmware_parse_file (firmware, file_in,
				      DFU_FIRMWARE_PARSE_FLAG_NONE,
				      priv->cancellable,
				      error)) {
		return FALSE;
	}

	/* get data */
	data = dfu_tool_get_firmware_contents_default (firmware, &len, error);
	if (data == NULL)
		return FALSE;

	/* check type */
	if (g_strcmp0 (values[2], "xtea") == 0) {
		guint32 key[4];
		if (!dfu_tool_parse_xtea_key (values[3], key, error))
			return FALSE;
		dfu_tool_encrypt_xtea (key, data, len);
		dfu_firmware_set_metadata (firmware,
					   DFU_METADATA_KEY_CIPHER_KIND,
					   "XTEA");
	} else {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "unknown type '%s', expected [xtea]",
			     values[2]);
		return FALSE;
	}

	/* write out new file */
	file_out = g_file_new_for_path (values[1]);
	g_debug ("wrote %s", values[1]);
	return dfu_firmware_write_file (firmware,
					file_out,
					priv->cancellable,
					error);
}

/**
 * dfu_tool_decrypt:
 **/
static gboolean
dfu_tool_decrypt (DfuToolPrivate *priv, gchar **values, GError **error)
{
	gsize len;
	guint8 *data;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GFile) file_in = NULL;
	g_autoptr(GFile) file_out = NULL;

	/* check args */
	if (g_strv_length (values) < 4) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "Invalid arguments, expected "
				     "FILENAME-IN FILENAME-OUT TYPE KEY"
				     " -- e.g. firmware.xdfu firmware.dfu xtea deadbeef");
		return FALSE;
	}

	/* check extensions */
	if (!priv->force) {
		if (!g_str_has_suffix (values[0], ".xdfu")) {
			g_set_error_literal (error,
					     DFU_ERROR,
					     DFU_ERROR_NOT_SUPPORTED,
					     "Invalid filename, expected *.xdfu");
			return FALSE;
		}
		if (!g_str_has_suffix (values[1], ".dfu")) {
			g_set_error_literal (error,
					     DFU_ERROR,
					     DFU_ERROR_NOT_SUPPORTED,
					     "Invalid filename, expected *.dfu");
			return FALSE;
		}
	}

	/* open */
	file_in = g_file_new_for_path (values[0]);
	firmware = dfu_firmware_new ();
	if (!dfu_firmware_parse_file (firmware, file_in,
				      DFU_FIRMWARE_PARSE_FLAG_NONE,
				      priv->cancellable,
				      error)) {
		return FALSE;
	}

	/* get data */
	data = dfu_tool_get_firmware_contents_default (firmware, &len, error);
	if (data == NULL)
		return FALSE;

	/* check type */
	if (g_strcmp0 (values[2], "xtea") == 0) {
		guint32 key[4];
		if (!dfu_tool_parse_xtea_key (values[3], key, error))
			return FALSE;
		dfu_tool_decrypt_xtea (key, data, len);
		dfu_firmware_remove_metadata (firmware,
					      DFU_METADATA_KEY_CIPHER_KIND);
	} else {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "unknown type '%s', expected [xtea]",
			     values[2]);
		return FALSE;
	}

	/* write out new file */
	file_out = g_file_new_for_path (values[1]);
	g_debug ("wrote %s", values[1]);
	return dfu_firmware_write_file (firmware,
					file_out,
					priv->cancellable,
					error);
}

/**
 * dfu_tool_watch:
 **/
static gboolean
dfu_tool_watch (DfuToolPrivate *priv, gchar **values, GError **error)
{
	guint i;
	DfuDevice *device;
	g_autoptr(DfuContext) dfu_context = NULL;
	g_autoptr(GMainLoop) loop = NULL;
	g_autoptr(GPtrArray) devices = NULL;

	/* get all the DFU devices */
	dfu_context = dfu_context_new ();
	dfu_context_enumerate (dfu_context, NULL);

	/* print what's already attached */
	devices = dfu_context_get_devices (dfu_context);
	for (i = 0; i < devices->len; i++) {
		device = g_ptr_array_index (devices, i);
		dfu_tool_device_added_cb (dfu_context, device, priv);
	}

	/* watch for any hotplugged device */
	loop = g_main_loop_new (NULL, FALSE);
	g_signal_connect (dfu_context, "device-added",
			  G_CALLBACK (dfu_tool_device_added_cb), priv);
	g_signal_connect (dfu_context, "device-removed",
			  G_CALLBACK (dfu_tool_device_removed_cb), priv);
	g_signal_connect (dfu_context, "device-changed",
			  G_CALLBACK (dfu_tool_device_changed_cb), priv);
	g_signal_connect (priv->cancellable, "cancelled",
			  G_CALLBACK (dfu_tool_watch_cancelled_cb), loop);
	g_main_loop_run (loop);
	return TRUE;
}

/**
 * dfu_tool_dump:
 **/
static gboolean
dfu_tool_dump (DfuToolPrivate *priv, gchar **values, GError **error)
{
	DfuFirmwareParseFlags flags = DFU_FIRMWARE_PARSE_FLAG_NONE;
	guint i;

	/* check args */
	if (g_strv_length (values) < 1) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "Invalid arguments, expected FILENAME");
		return FALSE;
	}

	/* dump corrupt files */
	if (priv->force) {
		flags |= DFU_FIRMWARE_PARSE_FLAG_NO_CRC_TEST;
		flags |= DFU_FIRMWARE_PARSE_FLAG_NO_VERSION_TEST;
	}

	/* open files */
	for (i = 0; values[i] != NULL; i++) {
		g_autoptr(DfuFirmware) firmware = NULL;
		g_autoptr(GFile) file = NULL;
		g_autoptr(GError) error_local = NULL;

		/* dump to screen */
		g_print ("Loading %s:\n", values[i]);
		firmware = dfu_firmware_new ();
		file = g_file_new_for_path (values[i]);
		if (!dfu_firmware_parse_file (firmware, file, flags,
					      priv->cancellable,
					      &error_local)) {
			g_print ("Failed to load firmware: %s\n",
				 error_local->message);
			continue;
		}
		g_print ("%s\n", dfu_firmware_to_string (firmware));
	}
	return TRUE;
}

/**
 * dfu_tool_write_alt:
 **/
static gboolean
dfu_tool_write_alt (DfuToolPrivate *priv, gchar **values, GError **error)
{
	DfuImage *image;
	DfuTargetTransferFlags flags = DFU_TARGET_TRANSFER_FLAG_VERIFY;
	DfuToolProgressHelper helper;
	g_autofree gchar *str_debug = NULL;
	g_autoptr(DfuDevice) device = NULL;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(DfuTarget) target = NULL;
	g_autoptr(GFile) file = NULL;

	/* check args */
	if (g_strv_length (values) < 2) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "Invalid arguments, expected "
				     "FILENAME DEVICE-ALT-NAME|DEVICE-ALT-ID "
				     "[IMAGE-ALT-NAME|IMAGE-ALT-ID]");
		return FALSE;
	}

	/* open file */
	firmware = dfu_firmware_new ();
	file = g_file_new_for_path (values[0]);
	if (!dfu_firmware_parse_file (firmware, file,
				      DFU_FIRMWARE_PARSE_FLAG_NONE,
				      priv->cancellable, error))
		return FALSE;

	/* open correct device */
	device = dfu_tool_get_defalt_device (priv, error);
	if (device == NULL)
		return FALSE;
	if (priv->transfer_size > 0)
		dfu_device_set_transfer_size (device, priv->transfer_size);
	if (!dfu_device_open (device,
			      DFU_DEVICE_OPEN_FLAG_NONE,
			      priv->cancellable,
			      error))
		return FALSE;

	/* set up progress */
	helper.last_state = DFU_STATE_DFU_ERROR;
	helper.marks_total = 30;
	helper.marks_shown = 0;
	g_signal_connect (device, "state-changed",
			  G_CALLBACK (fu_tool_state_changed_cb), &helper);
	g_signal_connect (device, "percentage-changed",
			  G_CALLBACK (fu_tool_percentage_changed_cb), &helper);

	/* APP -> DFU */
	if (dfu_device_get_mode (device) == DFU_MODE_RUNTIME) {
		g_debug ("detaching");
		if (!dfu_device_detach (device, priv->cancellable, error))
			return FALSE;
		if (!dfu_device_wait_for_replug (device, 5000, priv->cancellable, error))
			return FALSE;

		/* put back in same state */
		flags |= DFU_TARGET_TRANSFER_FLAG_ATTACH;
		flags |= DFU_TARGET_TRANSFER_FLAG_WAIT_RUNTIME;
	}

	/* print the new object */
	str_debug = dfu_firmware_to_string (firmware);
	g_debug ("DFU: %s", str_debug);

	/* get correct target on device */
	target = dfu_device_get_target_by_alt_name (device,
						    values[1],
						    NULL);
	if (target == NULL) {
		gchar *endptr;
		guint64 tmp = g_ascii_strtoull (values[1], &endptr, 10);
		if (tmp > 0xff || endptr[0] != '\0') {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "Failed to parse alt-setting '%s'",
				     values[1]);
			return FALSE;
		}
		target = dfu_device_get_target_by_alt_setting (device,
							       tmp,
							       error);
		if (target == NULL)
			return FALSE;
	}

	/* allow overriding the firmware alt-setting */
	if (g_strv_length (values) > 2) {
		image = dfu_firmware_get_image_by_name (firmware, values[2]);
		if (image == NULL) {
			gchar *endptr;
			guint64 tmp = g_ascii_strtoull (values[2], &endptr, 10);
			if (tmp > 0xff || endptr[0] != '\0') {
				g_set_error (error,
					     DFU_ERROR,
					     DFU_ERROR_INTERNAL,
					     "Failed to parse image alt-setting '%s'",
					     values[2]);
				return FALSE;
			}
			image = dfu_firmware_get_image (firmware, tmp);
			if (image == NULL) {
				g_set_error (error,
					     DFU_ERROR,
					     DFU_ERROR_INVALID_FILE,
					     "could not locate image in firmware for %02x",
					     (guint) tmp);
				return FALSE;
			}
		}
	} else {
		g_print ("WARNING: Using default firmware image\n");
		image = dfu_firmware_get_image_default (firmware);
		if (image == NULL) {
			g_set_error_literal (error,
					     DFU_ERROR,
					     DFU_ERROR_INVALID_FILE,
					     "no default image");
			return FALSE;
		}
	}

	/* allow forcing firmware kinds */
	if (priv->force) {
		flags |= DFU_TARGET_TRANSFER_FLAG_ANY_CIPHER;
	}

	/* transfer */
	if (!dfu_target_download (target,
				  image,
				  flags,
				  priv->cancellable,
				  error))
		return FALSE;

	/* success */
	g_print ("%u bytes successfully downloaded to device\n",
		 dfu_image_get_size (image));
	return TRUE;
}

/**
 * dfu_tool_write:
 **/
static gboolean
dfu_tool_write (DfuToolPrivate *priv, gchar **values, GError **error)
{
	DfuTargetTransferFlags flags = DFU_TARGET_TRANSFER_FLAG_VERIFY;
	DfuToolProgressHelper helper;
	g_autofree gchar *str_debug = NULL;
	g_autoptr(DfuDevice) device = NULL;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GFile) file = NULL;

	/* check args */
	if (g_strv_length (values) < 1) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "Invalid arguments, expected FILENAME");
		return FALSE;
	}

	/* open file */
	firmware = dfu_firmware_new ();
	file = g_file_new_for_path (values[0]);
	if (!dfu_firmware_parse_file (firmware, file,
				      DFU_FIRMWARE_PARSE_FLAG_NONE,
				      priv->cancellable, error))
		return FALSE;

	/* open correct device */
	device = dfu_tool_get_defalt_device (priv, error);
	if (device == NULL)
		return FALSE;
	if (!dfu_device_open (device,
			      DFU_DEVICE_OPEN_FLAG_NONE,
			      priv->cancellable,
			      error))
		return FALSE;

	/* print the new object */
	str_debug = dfu_firmware_to_string (firmware);
	g_debug ("DFU: %s", str_debug);

	/* put in correct mode */
	if (dfu_device_get_mode (device) == DFU_MODE_RUNTIME) {
		flags |= DFU_TARGET_TRANSFER_FLAG_DETACH;
		flags |= DFU_TARGET_TRANSFER_FLAG_ATTACH;
		flags |= DFU_TARGET_TRANSFER_FLAG_WAIT_RUNTIME;
	}

	/* allow wildcards */
	if (priv->force) {
		flags |= DFU_TARGET_TRANSFER_FLAG_WILDCARD_VID;
		flags |= DFU_TARGET_TRANSFER_FLAG_WILDCARD_PID;
		flags |= DFU_TARGET_TRANSFER_FLAG_ANY_CIPHER;
	}

	/* transfer */
	helper.last_state = DFU_STATE_DFU_ERROR;
	helper.marks_total = 30;
	helper.marks_shown = 0;
	g_signal_connect (device, "state-changed",
			  G_CALLBACK (fu_tool_state_changed_cb), &helper);
	g_signal_connect (device, "percentage-changed",
			  G_CALLBACK (fu_tool_percentage_changed_cb), &helper);
	if (!dfu_device_download (device,
				  firmware,
				  flags,
				  priv->cancellable,
				  error))
		return FALSE;

	/* success */
	g_print ("%u bytes successfully downloaded to device\n",
		 dfu_firmware_get_size (firmware));
	return TRUE;
}

/**
 * dfu_tool_list_target:
 **/
static void
dfu_tool_list_target (DfuTarget *target)
{
	DfuCipherKind cipher_kind;
	GPtrArray *sectors;
	const gchar *tmp;
	guint i;
	g_autofree gchar *alt_id = NULL;
	g_autoptr(GError) error_local = NULL;

	/* TRANSLATORS: the identifier name please */
	alt_id = g_strdup_printf ("%i", dfu_target_get_alt_setting (target));
	dfu_tool_print_indent (_("ID"), alt_id, 1);

	/* this is optional */
	tmp = dfu_target_get_alt_name (target, NULL);
	if (tmp != NULL) {
		/* TRANSLATORS: interface name, e.g. "Flash" */
		dfu_tool_print_indent (_("Name"), tmp, 2);
	}

	cipher_kind = dfu_target_get_cipher_kind (target);
	/* TRANSLATORS: this is the encryption method used when writing  */
	dfu_tool_print_indent (_("Cipher"), dfu_cipher_kind_to_string (cipher_kind), 2);

	/* print sector information */
	sectors = dfu_target_get_sectors (target);
	for (i = 0; i < sectors->len; i++) {
		DfuSector *sector;
		g_autofree gchar *msg = NULL;
		g_autofree gchar *title = NULL;
		sector = g_ptr_array_index (sectors, i);
		msg = dfu_sector_to_string (sector);
		/* TRANSLATORS: these are areas of memory on the chip */
		title = g_strdup_printf ("%s 0x%02x", _("Region"), i);
		dfu_tool_print_indent (title, msg, 2);
	}
}

/**
 * dfu_tool_list:
 **/
static gboolean
dfu_tool_list (DfuToolPrivate *priv, gchar **values, GError **error)
{
	guint i;
	g_autoptr(DfuContext) dfu_context = NULL;
	g_autoptr(GPtrArray) devices = NULL;

	/* get all the connected USB devices */
	dfu_context = dfu_context_new ();
	dfu_context_enumerate (dfu_context, NULL);
	devices = dfu_context_get_devices (dfu_context);
	for (i = 0; i < devices->len; i++) {
		DfuDevice *device = NULL;
		DfuTarget *target;
		GUsbDevice *dev;
		GPtrArray *dfu_targets;
		const gchar *tmp;
		guint j;
		g_autofree gchar *quirks = NULL;
		g_autofree gchar *version = NULL;
		g_autoptr(GError) error_local = NULL;

		/* device specific */
		device = g_ptr_array_index (devices, i);
		dev = dfu_device_get_usb_dev (device);
		version = as_utils_version_from_uint16 (g_usb_device_get_release (dev),
							AS_VERSION_PARSE_FLAG_NONE);
		g_print ("%s %04x:%04x [v%s]:\n",
			 /* TRANSLATORS: detected a DFU device */
			 _("Found"),
			 g_usb_device_get_vid (dev),
			 g_usb_device_get_pid (dev),
			 version);

		tmp = dfu_version_to_string (dfu_device_get_version (device));
		if (tmp != NULL) {
			/* TRANSLATORS: DFU protocol version, e.g. 1.1 */
			dfu_tool_print_indent (_("Protocol"), tmp, 1);
		}

		/* open */
		if (!dfu_device_open (device,
				      DFU_DEVICE_OPEN_FLAG_NONE,
				      priv->cancellable,
				      &error_local)) {
			if (g_error_matches (error_local,
					     DFU_ERROR,
					     DFU_ERROR_PERMISSION_DENIED)) {
				/* TRANSLATORS: probably not run as root... */
				dfu_tool_print_indent (_("Status"), _("Unknown: permission denied"), 2);
			} else {
				/* TRANSLATORS: device has failed to report status */
				dfu_tool_print_indent (_("Status"), error_local->message, 2);
			}
			continue;
		}

		tmp = dfu_mode_to_string (dfu_device_get_mode (device));
		/* TRANSLATORS: device mode, e.g. runtime or DFU */
		dfu_tool_print_indent (_("Mode"), tmp, 1);

		tmp = dfu_status_to_string (dfu_device_get_status (device));
		/* TRANSLATORS: device status, e.g. "OK" */
		dfu_tool_print_indent (_("Status"), tmp, 1);

		tmp = dfu_state_to_string (dfu_device_get_state (device));
		/* TRANSLATORS: device state, i.e. appIDLE */
		dfu_tool_print_indent (_("State"), tmp, 1);

		/* quirks are NULL if none are set */
		quirks = dfu_device_get_quirks_as_string (device);
		if (quirks != NULL) {
			/* TRANSLATORS: device quirks, i.e. things that
			 * it does that we have to work around */
			dfu_tool_print_indent (_("Quirks"), quirks, 1);
		}

		/* list targets */
		dfu_targets = dfu_device_get_targets (device);
		for (j = 0; j < dfu_targets->len; j++) {
			target = g_ptr_array_index (dfu_targets, j);
			dfu_tool_list_target (target);
		}
	}
	return TRUE;
}

/**
 * dfu_tool_detach:
 **/
static gboolean
dfu_tool_detach (DfuToolPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(DfuDevice) device = NULL;

	/* open correct device */
	device = dfu_tool_get_defalt_device (priv, error);
	if (device == NULL)
		return FALSE;
	if (priv->transfer_size > 0)
		dfu_device_set_transfer_size (device, priv->transfer_size);

	/* detatch */
	if (!dfu_device_open (device, DFU_DEVICE_OPEN_FLAG_NONE,
			      priv->cancellable, error))
		return FALSE;
	if (!dfu_device_detach (device, priv->cancellable, error))
		return FALSE;
	return TRUE;
}

/**
 * dfu_tool_sigint_cb:
 **/
static gboolean
dfu_tool_sigint_cb (gpointer user_data)
{
	DfuToolPrivate *priv = (DfuToolPrivate *) user_data;
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
	gboolean ret;
	gboolean verbose = FALSE;
	gboolean version = FALSE;
	g_autofree gchar *cmd_descriptions = NULL;
	g_autoptr(DfuToolPrivate) priv = g_new0 (DfuToolPrivate, 1);
	g_autoptr(GError) error = NULL;
	g_autoptr(GOptionContext) context = NULL;
	const GOptionEntry options[] = {
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &version,
			"Print the version number", NULL },
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			"Print verbose debug statements", NULL },
		{ "device", 'd', 0, G_OPTION_ARG_STRING, &priv->device_vid_pid,
			"Specify Vendor/Product ID(s) of DFU device", "VID:PID" },
		{ "transfer-size", 't', 0, G_OPTION_ARG_STRING, &priv->transfer_size,
			"Specify the number of bytes per USB transfer", "BYTES" },
		{ "force", '\0', 0, G_OPTION_ARG_NONE, &priv->force,
			"Force the action ignoring all warnings", NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* add commands */
	priv->cmd_array = g_ptr_array_new_with_free_func ((GDestroyNotify) dfu_tool_item_free);
	dfu_tool_add (priv->cmd_array,
		     "convert",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Convert firmware to DFU format"),
		     dfu_tool_convert);
	dfu_tool_add (priv->cmd_array,
		     "merge",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Merge multiple firmware files into one"),
		     dfu_tool_merge);
	dfu_tool_add (priv->cmd_array,
		     "set-vendor",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Set vendor ID on firmware file"),
		     dfu_tool_set_vendor);
	dfu_tool_add (priv->cmd_array,
		     "set-product",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Set product ID on firmware file"),
		     dfu_tool_set_product);
	dfu_tool_add (priv->cmd_array,
		     "set-release",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Set release version on firmware file"),
		     dfu_tool_set_release);
	dfu_tool_add (priv->cmd_array,
		     "set-alt-setting",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Set alternative number on firmware file"),
		     dfu_tool_set_alt_setting);
	dfu_tool_add (priv->cmd_array,
		     "set-alt-setting-name",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Set alternative name on firmware file"),
		     dfu_tool_set_alt_setting_name);
	dfu_tool_add (priv->cmd_array,
		     "attach",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Attach DFU capable device back to runtime"),
		     dfu_tool_attach);
	dfu_tool_add (priv->cmd_array,
		     "read",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Read firmware from device into a file"),
		     dfu_tool_read);
	dfu_tool_add (priv->cmd_array,
		     "read-alt",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Read firmware from one partition into a file"),
		     dfu_tool_read_alt);
	dfu_tool_add (priv->cmd_array,
		     "write",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Write firmware from file into device"),
		     dfu_tool_write);
	dfu_tool_add (priv->cmd_array,
		     "write-alt",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Write firmware from file into one partition"),
		     dfu_tool_write_alt);
	dfu_tool_add (priv->cmd_array,
		     "list",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("List currently attached DFU capable devices"),
		     dfu_tool_list);
	dfu_tool_add (priv->cmd_array,
		     "detach",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Detach currently attached DFU capable device"),
		     dfu_tool_detach);
	dfu_tool_add (priv->cmd_array,
		     "dump",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Dump details about a firmware file"),
		     dfu_tool_dump);
	dfu_tool_add (priv->cmd_array,
		     "watch",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Watch DFU devices being hotplugged"),
		     dfu_tool_watch);
	dfu_tool_add (priv->cmd_array,
		     "encrypt",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Encrypt firmware data"),
		     dfu_tool_encrypt);
	dfu_tool_add (priv->cmd_array,
		     "decrypt",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Decrypt firmware data"),
		     dfu_tool_decrypt);
	dfu_tool_add (priv->cmd_array,
		     "set-metadata",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Sets metadata on a firmware file"),
		     dfu_tool_set_metadata);

	/* do stuff on ctrl+c */
	priv->cancellable = g_cancellable_new ();
	g_unix_signal_add_full (G_PRIORITY_DEFAULT,
				SIGINT,
				dfu_tool_sigint_cb,
				priv,
				NULL);

	/* sort by command name */
	g_ptr_array_sort (priv->cmd_array,
			  (GCompareFunc) dfu_tool_sort_command_name_cb);

	/* get a list of the commands */
	context = g_option_context_new (NULL);
	cmd_descriptions = dfu_tool_get_descriptions (priv->cmd_array);
	g_option_context_set_summary (context, cmd_descriptions);

	/* TRANSLATORS: DFU stands for device firmware update */
	g_set_application_name (_("DFU Utility"));
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

	/* version */
	if (version) {
		g_print ("%s %s\n", PACKAGE_NAME, PACKAGE_VERSION);
		return EXIT_SUCCESS;
	}

	/* run the specified command */
	ret = dfu_tool_run (priv, argv[1], (gchar**) &argv[2], &error);
	if (!ret) {
		if (g_error_matches (error, DFU_ERROR, DFU_ERROR_INTERNAL)) {
			g_autofree gchar *tmp = NULL;
			tmp = g_option_context_get_help (context, TRUE, NULL);
			g_print ("%s\n\n%s", error->message, tmp);
		} else {
			g_print ("%s\n", error->message);
		}
		return EXIT_FAILURE;
	}

	/* success/ */
	return EXIT_SUCCESS;
}
