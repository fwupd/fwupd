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

typedef struct {
	GPtrArray		*cmd_array;
	gboolean		 reset;
	gchar			*device_vid_pid;
	guint16			 transfer_size;
	guint8			 alt_setting;
} DfuToolPrivate;

/**
 * dfu_tool_private_free:
 **/
static void
dfu_tool_private_free (DfuToolPrivate *priv)
{
	if (priv == NULL)
		return;
	g_free (priv->device_vid_pid);
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
dfu_tool_run (DfuToolPrivate *priv, const gchar *command, gchar **values, GError **error)
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
	guint i;
	g_autoptr(GUsbContext) usb_ctx = NULL;
	g_autoptr(GPtrArray) devices = NULL;

	/* get USB context */
	usb_ctx = g_usb_context_new (NULL);
	g_usb_context_enumerate (usb_ctx);

	/* we specified it manually */
	if (priv->device_vid_pid != NULL) {
		DfuDevice *device;
		g_auto(GStrv) vid_pid = NULL;
		g_autoptr(GUsbDevice) usb_device = NULL;

		/* split up */
		vid_pid = g_strsplit (priv->device_vid_pid, ":", -1);
		if (g_strv_length (vid_pid) != 2) {
			g_set_error_literal (error,
					     DFU_ERROR,
					     DFU_ERROR_INTERNAL,
					     "Invalid format of VID:PID");
			return NULL;
		}

		/* find device */
		usb_device = g_usb_context_find_by_vid_pid (usb_ctx,
							    atoi (vid_pid[0]),
							    atoi (vid_pid[1]),
							    error);
		if (usb_device == NULL)
			return NULL;

		/* get DFU device */
		device = dfu_device_new (usb_device);
		if (device == NULL) {
			g_set_error_literal (error,
					     DFU_ERROR,
					     DFU_ERROR_INVALID_DEVICE,
					     "Not a DFU device");
			return NULL;
		}
		return device;
	}

	/* auto-detect first device */
	devices = g_usb_context_get_devices (usb_ctx);
	for (i = 0; i < devices->len; i++) {
		GUsbDevice *usb_device = g_ptr_array_index (devices, i);
		DfuDevice *device = dfu_device_new (usb_device);
		if (device != NULL)
			return device;
	}

	/* boo-hoo*/
	g_set_error_literal (error,
			     DFU_ERROR,
			     DFU_ERROR_INVALID_DEVICE,
			     "No DFU-capable devices detected");
	return NULL;
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
				      NULL, error)) {
		return FALSE;
	}

	/* parse VID */
	tmp = g_ascii_strtoull (values[1], NULL, 16);
	if (tmp == 0 || tmp > 0xffff) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "Failed to parse VID '%s'", values[1]);
		return FALSE;
	}
	dfu_firmware_set_vid (firmware, tmp);

	/* write out new file */
	return dfu_firmware_write_file (firmware, file, NULL, error);
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
				      NULL, error)) {
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
	return dfu_firmware_write_file (firmware, file, NULL, error);
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
				      NULL, error)) {
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
	return dfu_firmware_write_file (firmware, file, NULL, error);
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
				      NULL, error)) {
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
			     "Failed to parse alternative setting '%s'", values[1]);
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
	return dfu_firmware_write_file (firmware, file, NULL, error);
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
				      NULL, error)) {
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
	return dfu_firmware_write_file (firmware, file, NULL, error);
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
					      NULL, error)) {
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
	return dfu_firmware_write_file (firmware, file, NULL, error);
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
	if (argc < 3) {
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
				      NULL, error)) {
		return FALSE;
	}

	/* set output format */
	if (g_strcmp0 (values[0], "raw") == 0) {
		dfu_firmware_set_format (firmware, DFU_FIRMWARE_FORMAT_RAW);
	} else if (g_strcmp0 (values[0], "dfu") == 0) {
		dfu_firmware_set_format (firmware, DFU_FIRMWARE_FORMAT_DFU_1_0);
	} else if (g_strcmp0 (values[0], "dfuse") == 0) {
		dfu_firmware_set_format (firmware, DFU_FIRMWARE_FORMAT_DFUSE);
	} else {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "unknown format '%s', expected [raw|dfu|dfuse]",
			     values[0]);
		return FALSE;
	}

	/* set target size */
	if (argc > 3) {
		DfuImage *image;
		DfuElement *element;
		tmp = g_ascii_strtoull (values[3], NULL, 16);
		if (tmp > 0xffff) {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "Failed to parse target size '%s'", values[3]);
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
	return dfu_firmware_write_file (firmware, file_out, NULL, error);
}

/**
 * dfu_tool_reset:
 **/
static gboolean
dfu_tool_reset (DfuToolPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(DfuDevice) device = NULL;
	g_autoptr(DfuTarget) target = NULL;

	device = dfu_tool_get_defalt_device (priv, error);
	if (device == NULL)
		return FALSE;
	target = dfu_device_get_target_by_alt_setting (device, priv->alt_setting, error);
	if (target == NULL)
		return FALSE;
	if (!dfu_target_open (target,
			      DFU_TARGET_OPEN_FLAG_NO_AUTO_REFRESH,
			      NULL, error))
		return FALSE;

	if (!dfu_device_reset (device, error))
		return FALSE;

	return TRUE;
}

/**
 * dfu_tool_upload_target:
 **/
static gboolean
dfu_tool_upload_target (DfuToolPrivate *priv, gchar **values, GError **error)
{
	DfuTargetTransferFlags flags = DFU_TARGET_TRANSFER_FLAG_NONE;
	g_autofree gchar *str_debug = NULL;
	g_autoptr(DfuDevice) device = NULL;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(DfuImage) image = NULL;
	g_autoptr(DfuTarget) target = NULL;
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
	target = dfu_device_get_target_by_alt_setting (device, priv->alt_setting, error);
	if (target == NULL)
		return FALSE;

	if (priv->transfer_size > 0)
		dfu_target_set_transfer_size (target, priv->transfer_size);
	if (!dfu_target_open (target, DFU_TARGET_OPEN_FLAG_NONE, NULL, error))
		return FALSE;

	/* APP -> DFU */
	if (dfu_target_get_mode (target) == DFU_MODE_RUNTIME) {
		if (!dfu_device_wait_for_replug (device, 5000, NULL, error))
			return FALSE;
		flags |= DFU_TARGET_TRANSFER_FLAG_BOOT_RUNTIME;
	}

	/* transfer */
	image = dfu_target_upload (target, DFU_TARGET_TRANSFER_FLAG_NONE,
				   flags, NULL, NULL, NULL, error);
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
	if (!dfu_firmware_write_file (firmware, file, NULL, error))
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
 * dfu_tool_upload:
 **/
static gboolean
dfu_tool_upload (DfuToolPrivate *priv, gchar **values, GError **error)
{
	DfuTargetTransferFlags flags = DFU_TARGET_TRANSFER_FLAG_NONE;
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

	/* transfer */
	firmware = dfu_device_upload (device, DFU_TARGET_TRANSFER_FLAG_NONE,
				      flags, NULL, NULL, NULL, error);
	if (firmware == NULL)
		return FALSE;

	/* save file */
	file = g_file_new_for_path (values[0]);
	if (!dfu_firmware_write_file (firmware, file, NULL, error))
		return FALSE;

	/* print the new object */
	str_debug = dfu_firmware_to_string (firmware);
	g_debug ("DFU: %s", str_debug);

	/* success */
	g_print ("%u bytes successfully uploaded from device\n",
		 dfu_firmware_get_size (firmware));
	return TRUE;
}

typedef struct {
	guint		 marks_total;
	guint		 marks_shown;
	DfuState	 last_state;
} DfuToolProgressHelper;

/**
 * fu_tool_transfer_progress_cb:
 **/
static void
fu_tool_transfer_progress_cb (DfuState state, goffset current,
			      goffset total, gpointer user_data)
{
	DfuToolProgressHelper *helper = (DfuToolProgressHelper *) user_data;
	guint marks_now;
	guint i;

	/* changed state */
	if (state != helper->last_state) {
		const gchar *title = NULL;
		switch (state) {
		case DFU_STATE_DFU_DNLOAD_IDLE:
			/* TRANSLATORS: this is when moving from host to device */
			title = _("Downloading");
			break;
		case DFU_STATE_DFU_UPLOAD_IDLE:
			/* TRANSLATORS: this is when moving from device to host */
			title = _("Verifying");
			break;
		default:
			break;
		}
		/* show title and then pad */
		if (title != NULL) {
			g_print ("%s ", title);
			for (i = strlen (title); i < 15; i++)
				g_print (" ");
			g_print (": ");
		}
		helper->marks_shown = 0;
		helper->last_state = state;
	}

	/* add any sections */
	marks_now = current * helper->marks_total / total;
	for (i = helper->marks_shown; i < marks_now; i++)
		g_print ("#");
	helper->marks_shown = marks_now;

	/* this state done */
	if (current == total)
		g_print ("\n");
}

/**
 * dfu_tool_dump:
 **/
static gboolean
dfu_tool_dump (DfuToolPrivate *priv, gchar **values, GError **error)
{
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
				      NULL, error))
		return FALSE;

	/* dump to screen */
	g_print ("%s\n", dfu_firmware_to_string (firmware));
	return TRUE;
}

/**
 * dfu_tool_download_target:
 **/
static gboolean
dfu_tool_download_target (DfuToolPrivate *priv, gchar **values, GError **error)
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
	if (g_strv_length (values) < 1) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "Invalid arguments, expected "
				     "FILENAME [ALT-SETTING]");
		return FALSE;
	}

	/* open file */
	firmware = dfu_firmware_new ();
	file = g_file_new_for_path (values[0]);
	if (!dfu_firmware_parse_file (firmware, file,
				      DFU_FIRMWARE_PARSE_FLAG_NONE,
				      NULL, error))
		return FALSE;

	/* open correct device */
	device = dfu_tool_get_defalt_device (priv, error);
	if (device == NULL)
		return FALSE;
	target = dfu_device_get_target_by_alt_setting (device, priv->alt_setting, error);
	if (target == NULL)
		return FALSE;

	if (priv->transfer_size > 0)
		dfu_target_set_transfer_size (target, priv->transfer_size);
	if (!dfu_target_open (target, DFU_TARGET_OPEN_FLAG_NONE, NULL, error))
		return FALSE;

	/* APP -> DFU */
	if (dfu_target_get_mode (target) == DFU_MODE_RUNTIME) {
		g_debug ("detaching");
		if (!dfu_target_detach (target, NULL, error))
			return FALSE;
		if (!dfu_device_wait_for_replug (device, 5000, NULL, error))
			return FALSE;
	}

	/* print the new object */
	str_debug = dfu_firmware_to_string (firmware);
	g_debug ("DFU: %s", str_debug);

	/* optional reset */
	if (priv->reset) {
		flags |= DFU_TARGET_TRANSFER_FLAG_HOST_RESET;
		flags |= DFU_TARGET_TRANSFER_FLAG_BOOT_RUNTIME;
	}

	/* allow overriding the firmware alt-setting */
	if (g_strv_length (values) > 1) {
		guint64 tmp = g_ascii_strtoull (values[1], NULL, 10);
		if (tmp > 0xff) {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "Failed to parse alt-setting '%s'", values[1]);
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

	/* transfer */
	helper.last_state = DFU_STATE_DFU_ERROR;
	helper.marks_total = 30;
	helper.marks_shown = 0;
	if (!dfu_target_download (target, image, flags, NULL,
				  fu_tool_transfer_progress_cb, &helper,
				  error))
		return FALSE;

	/* success */
	g_print ("%u bytes successfully downloaded to device\n",
		 dfu_image_get_size (image));
	return TRUE;
}

/**
 * dfu_tool_download:
 **/
static gboolean
dfu_tool_download (DfuToolPrivate *priv, gchar **values, GError **error)
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
				      NULL, error))
		return FALSE;

	/* open correct device */
	device = dfu_tool_get_defalt_device (priv, error);
	if (device == NULL)
		return FALSE;

	/* print the new object */
	str_debug = dfu_firmware_to_string (firmware);
	g_debug ("DFU: %s", str_debug);

	/* optional reset */
	if (priv->reset) {
		flags |= DFU_TARGET_TRANSFER_FLAG_HOST_RESET;
		flags |= DFU_TARGET_TRANSFER_FLAG_BOOT_RUNTIME;
	}

	/* transfer */
	helper.last_state = DFU_STATE_DFU_ERROR;
	helper.marks_total = 30;
	helper.marks_shown = 0;
	if (!dfu_device_download (device, firmware, flags, NULL,
				  fu_tool_transfer_progress_cb, &helper,
				  error))
		return FALSE;

	/* success */
	g_print ("%u bytes successfully downloaded to device\n",
		 dfu_firmware_get_size (firmware));
	return TRUE;
}

/**
 * dfu_tool_list:
 **/
static gboolean
dfu_tool_list (DfuToolPrivate *priv, gchar **values, GError **error)
{
	GUsbDevice *usb_device;
	gboolean ret;
	guint i;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GUsbContext) usb_ctx = NULL;

	/* get all the connected USB devices */
	usb_ctx = g_usb_context_new (NULL);
	g_usb_context_enumerate (usb_ctx);
	devices = g_usb_context_get_devices (usb_ctx);
	for (i = 0; i < devices->len; i++) {
		g_autoptr(DfuDevice) device = NULL;
		GPtrArray *dfu_targets;
		DfuTarget *target;
		guint j;

		usb_device = g_ptr_array_index (devices, i);
		g_debug ("PROBING [%04x:%04x]",
			 g_usb_device_get_vid (usb_device),
			 g_usb_device_get_pid (usb_device));
		device = dfu_device_new (usb_device);
		if (device == NULL)
			continue;
		dfu_targets = dfu_device_get_targets (device);
		for (j = 0; j < dfu_targets->len; j++) {
			g_autoptr(GError) error_local = NULL;
			target = g_ptr_array_index (dfu_targets, j);

			if (priv->transfer_size > 0)
				dfu_target_set_transfer_size (target, priv->transfer_size);
			ret = dfu_target_open (target,
					       DFU_TARGET_OPEN_FLAG_NONE,
					       NULL, &error_local);
			g_print ("Found %s: [%04x:%04x] ver=%04x, devnum=%i, cfg=%i, intf=%i, ts=%i, alt=%i, name=%s",
				 dfu_mode_to_string (dfu_target_get_mode (target)),
				 g_usb_device_get_vid (usb_device),
				 g_usb_device_get_pid (usb_device),
				 g_usb_device_get_release (usb_device),
				 g_usb_device_get_address (usb_device),
				 g_usb_device_get_configuration (usb_device, NULL),
				 dfu_target_get_interface_number (target),
				 dfu_target_get_transfer_size (target),
				 dfu_target_get_interface_alt_setting (target),
				 dfu_target_get_interface_alt_name (target));
			if (ret) {
				g_print (", status=%s, state=%s\n",
					 dfu_status_to_string (dfu_target_get_status (target)),
					 dfu_state_to_string (dfu_target_get_state (target)));
			} else {
				g_print (": %s\n", error_local->message);
			}
			dfu_target_close (target, NULL);
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
	g_autoptr(DfuTarget) target = NULL;

	/* open correct device */
	device = dfu_tool_get_defalt_device (priv, error);
	if (device == NULL)
		return FALSE;
	target = dfu_device_get_target_by_alt_setting (device, priv->alt_setting, error);
	if (target == NULL)
		return FALSE;
	if (priv->transfer_size > 0)
		dfu_target_set_transfer_size (target, priv->transfer_size);
	if (!dfu_target_open (target, DFU_TARGET_OPEN_FLAG_NONE, NULL, error))
		return FALSE;

	/* detatch */
	if (!dfu_target_detach (target, NULL, error))
		return FALSE;
	return TRUE;
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
		{ "alt", 'a', 0, G_OPTION_ARG_INT, &priv->alt_setting,
			"Specify the alternate setting of the DFU interface", "NUMBER" },
		{ "transfer-size", 't', 0, G_OPTION_ARG_STRING, &priv->transfer_size,
			"Specify the number of bytes per USB transfer", "BYTES" },
		{ "reset", 'r', 0, G_OPTION_ARG_NONE, &priv->reset,
			"Issue USB host reset once finished", NULL },
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
		     "reset",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Issue USB host reset"),
		     dfu_tool_reset);
	dfu_tool_add (priv->cmd_array,
		     "upload",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Read firmware from device into file"),
		     dfu_tool_upload);
	dfu_tool_add (priv->cmd_array,
		     "upload-target",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Read firmware from target into file"),
		     dfu_tool_upload_target);
	dfu_tool_add (priv->cmd_array,
		     "download",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Write firmware from file into target"),
		     dfu_tool_download_target);
	dfu_tool_add (priv->cmd_array,
		     "download-target",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Write firmware from file into device"),
		     dfu_tool_download);
	dfu_tool_add (priv->cmd_array,
		     "list",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("List currently attached DFU capable device"),
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
