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

#include <libintl.h>
#include <locale.h>
#include <stdlib.h>
#include <glib/gi18n.h>
#include <glib-unix.h>
#include <appstream-glib.h>

#include "dfu-cipher-xtea.h"
#include "dfu-context.h"
#include "dfu-device-private.h"
#include "dfu-patch.h"
#include "dfu-sector.h"

#include "fu-device-locker.h"
#include "fu-progressbar.h"

#include "fwupd-error.h"

typedef struct {
	GCancellable		*cancellable;
	GPtrArray		*cmd_array;
	gboolean		 force;
	gchar			*device_vid_pid;
	guint16			 transfer_size;
	FuProgressbar		*progressbar;
} DfuToolPrivate;

static void
dfu_tool_print_indent (const gchar *title, const gchar *message, guint indent)
{
	for (gsize i = 0; i < indent; i++)
		g_print (" ");
	g_print ("%s:", title);
	for (gsize i = strlen (title) + indent; i < 15; i++)
		g_print (" ");
	g_print ("%s\n", message);
}

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

static void
dfu_tool_item_free (FuUtilItem *item)
{
	g_free (item->name);
	g_free (item->arguments);
	g_free (item->description);
	g_free (item);
}

static gint
dfu_tool_sort_command_name_cb (FuUtilItem **item1, FuUtilItem **item2)
{
	return g_strcmp0 ((*item1)->name, (*item2)->name);
}

static void
dfu_tool_add (GPtrArray *array,
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
dfu_tool_get_descriptions (GPtrArray *array)
{
	gsize len;
	const gsize max_len = 31;
	FuUtilItem *item;
	GString *string;

	/* print each command */
	string = g_string_new ("");
	for (guint i = 0; i < array->len; i++) {
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
			for (guint j = len; j < max_len + 1; j++)
				g_string_append_c (string, ' ');
			g_string_append (string, item->description);
			g_string_append_c (string, '\n');
		} else {
			g_string_append_c (string, '\n');
			for (guint j = 0; j < max_len + 1; j++)
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
dfu_tool_run (DfuToolPrivate *priv,
	      const gchar *command,
	      gchar **values,
	      GError **error)
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
			     FWUPD_ERROR_INTERNAL,
			     /* TRANSLATORS: error message */
			     _("Command not found"));
	return FALSE;
}

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
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "Invalid format of VID:PID");
			return NULL;
		}
		if (tmp[0] != ':') {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "Invalid format of VID:PID");
			return NULL;
		}
		pid = g_ascii_strtoull (tmp + 1, NULL, 16);
		if (pid == 0 || pid > G_MAXUINT16) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "Invalid format of VID:PID");
			return NULL;
		}

		/* find device */
		device = dfu_context_get_device_by_vid_pid (dfu_context,
							    (guint16) vid,
							    (guint16) pid,
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

static gboolean
dfu_tool_set_vendor (DfuToolPrivate *priv, gchar **values, GError **error)
{
	guint64 tmp;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GFile) file = NULL;

	/* check args */
	if (g_strv_length (values) < 2) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
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
	if (tmp == 0 || tmp > G_MAXUINT16) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Failed to parse VID '%s'",
			     values[1]);
		return FALSE;
	}
	dfu_firmware_set_vid (firmware, (guint16) tmp);

	/* write out new file */
	return dfu_firmware_write_file (firmware,
					file,
					priv->cancellable,
					error);
}

static gboolean
dfu_tool_set_product (DfuToolPrivate *priv, gchar **values, GError **error)
{
	guint64 tmp;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GFile) file = NULL;

	/* check args */
	if (g_strv_length (values) < 2) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
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
	if (tmp == 0 || tmp > G_MAXUINT16) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Failed to parse PID '%s'", values[1]);
		return FALSE;
	}
	dfu_firmware_set_pid (firmware, (guint16) tmp);

	/* write out new file */
	return dfu_firmware_write_file (firmware,
					file,
					priv->cancellable,
					error);
}

static guint16
dfu_tool_parse_release_uint16 (const gchar *version, GError **error)
{
	gchar *endptr = NULL;
	guint64 tmp_lsb, tmp_msb;
	g_auto(GStrv) split = g_strsplit (version, ".", -1);

	/* check if valid */
	if (g_strv_length (split) != 2) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "invalid format, expected 'major.minor'");
		return 0xffff;
	}

	/* parse MSB & LSB */
	tmp_msb = g_ascii_strtoull (split[0], &endptr, 10);
	if (tmp_msb > 0xff || endptr[0] != '\0') {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Failed to parse version '%s'",
			     version);
		return 0xffff;
	}
	tmp_lsb = g_ascii_strtoull (split[1], &endptr, 10);
	if (tmp_lsb > 0xff || endptr[0] != '\0') {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Failed to parse version '%s'",
			     version);
		return 0xffff;
	}
	return (tmp_msb << 8) + tmp_lsb;
}

static gboolean
dfu_tool_set_release (DfuToolPrivate *priv, gchar **values, GError **error)
{
	gchar *endptr = NULL;
	guint64 tmp;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GFile) file = NULL;

	/* check args */
	if (g_strv_length (values) < 2) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
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

	/* parse release */
	tmp = g_ascii_strtoull (values[1], &endptr, 16);
	if (tmp > G_MAXUINT16 || endptr[0] != '\0') {
		tmp = dfu_tool_parse_release_uint16 (values[1], error);
		if (tmp == 0xffff)
			return FALSE;
	}
	dfu_firmware_set_release (firmware, (guint16) tmp);

	/* write out new file */
	return dfu_firmware_write_file (firmware,
					file,
					priv->cancellable,
					error);
}

static GBytes *
dfu_tool_parse_hex_string (const gchar *val, GError **error)
{
	gsize result_size;
	g_autofree guint8 *result = NULL;

	/* sanity check */
	if (val == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "nothing to parse");
		return NULL;
	}

	/* parse each hex byte */
	result_size = strlen (val) / 2;
	result = g_malloc (result_size);
	for (guint i = 0; i < result_size; i++) {
		gchar buf[3] = { "xx" };
		gchar *endptr = NULL;
		guint64 tmp;

		/* copy two bytes and parse as hex */
		memcpy (buf, val + (i * 2), 2);
		tmp = g_ascii_strtoull (buf, &endptr, 16);
		if (tmp > 0xff || endptr[0] != '\0') {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "failed to parse '%s'", val);
			return NULL;
		}
		result[i] = tmp;
	}
	return g_bytes_new (result, result_size);
}

static guint
dfu_tool_bytes_replace (GBytes *data, GBytes *search, GBytes *replace)
{
	gsize data_sz;
	gsize replace_sz;
	gsize search_sz;
	guint8 *data_buf;
	guint8 *replace_buf;
	guint8 *search_buf;
	guint cnt = 0;

	data_buf = (gpointer) g_bytes_get_data (data, &data_sz);
	search_buf = (gpointer) g_bytes_get_data (search, &search_sz);
	replace_buf = (gpointer) g_bytes_get_data (replace, &replace_sz);

	g_return_val_if_fail (search_sz == replace_sz, FALSE);

	/* find and replace each one */
	for (gsize i = 0; i < data_sz - search_sz; i++) {
		if (memcmp (data_buf + i, search_buf, search_sz) == 0) {
			g_print ("Replacing %" G_GSIZE_FORMAT " bytes @0x%04x\n",
				 replace_sz, (guint) i);
			memcpy (data_buf + i, replace_buf, replace_sz);
			i += replace_sz;
			cnt++;
		}
	}
	return cnt;
}

static gboolean
dfu_tool_patch_dump (DfuToolPrivate *priv, gchar **values, GError **error)
{
	gsize sz = 0;
	g_autofree gchar *data = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(DfuPatch) patch = NULL;
	g_autoptr(GBytes) blob = NULL;

	if (g_strv_length (values) != 1) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments, expected FILE.bdiff");
		return FALSE;
	}

	/* load file */
	if (!g_file_get_contents (values[0], &data, &sz, error))
		return FALSE;
	blob = g_bytes_new (data, sz);

	/* dump the patch to disk */
	patch = dfu_patch_new ();
	if (!dfu_patch_import (patch, blob, error))
		return FALSE;
	str = dfu_patch_to_string (patch);
	g_print ("%s\n", str);

	/* success */
	return TRUE;
}

static gboolean
dfu_tool_patch_apply (DfuToolPrivate *priv, gchar **values, GError **error)
{
	DfuPatchApplyFlags flags = DFU_PATCH_APPLY_FLAG_NONE;
	const gchar *data_new;
	gsize sz_diff = 0;
	gsize sz_new = 0;
	gsize sz_old = 0;
	g_autofree gchar *data_diff = NULL;
	g_autofree gchar *data_old = NULL;
	g_autoptr(DfuPatch) patch = NULL;
	g_autoptr(GBytes) blob_diff = NULL;
	g_autoptr(GBytes) blob_new = NULL;
	g_autoptr(GBytes) blob_old = NULL;

	if (g_strv_length (values) != 3) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments, expected OLD.bin OUT.bdiff NEW.bin");
		return FALSE;
	}

	/* allow the user to shoot themselves in the foot */
	if (priv->force)
		flags |= DFU_PATCH_APPLY_FLAG_IGNORE_CHECKSUM;

	if (!g_file_get_contents (values[0], &data_old, &sz_old, error))
		return FALSE;
	blob_old = g_bytes_new (data_old, sz_old);
	if (!g_file_get_contents (values[1], &data_diff, &sz_diff, error))
		return FALSE;
	blob_diff = g_bytes_new (data_diff, sz_diff);
	patch = dfu_patch_new ();
	if (!dfu_patch_import (patch, blob_diff, error))
		return FALSE;
	blob_new = dfu_patch_apply (patch, blob_old, flags, error);
	if (blob_new == NULL)
		return FALSE;

	/* save to disk */
	data_new = g_bytes_get_data (blob_new, &sz_new);
	return g_file_set_contents (values[2], data_new, sz_new, error);
}

static gboolean
dfu_tool_patch_create (DfuToolPrivate *priv, gchar **values, GError **error)
{
	const gchar *data_diff;
	gsize sz_diff = 0;
	gsize sz_new = 0;
	gsize sz_old = 0;
	g_autofree gchar *data_new = NULL;
	g_autofree gchar *data_old = NULL;
	g_autoptr(DfuPatch) patch = NULL;
	g_autoptr(GBytes) blob_diff = NULL;
	g_autoptr(GBytes) blob_new = NULL;
	g_autoptr(GBytes) blob_old = NULL;

	if (g_strv_length (values) != 3) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments, expected OLD.bin NEW.bin OUT.bdiff");
		return FALSE;
	}

	/* read files */
	if (!g_file_get_contents (values[0], &data_old, &sz_old, error))
		return FALSE;
	blob_old = g_bytes_new (data_old, sz_old);
	if (!g_file_get_contents (values[1], &data_new, &sz_new, error))
		return FALSE;
	blob_new = g_bytes_new (data_new, sz_new);

	/* create patch */
	patch = dfu_patch_new ();
	if (!dfu_patch_create (patch, blob_old, blob_new, error))
		return FALSE;
	blob_diff = dfu_patch_export (patch, error);
	if (blob_diff == NULL)
		return FALSE;

	/* save to disk */
	data_diff = g_bytes_get_data (blob_diff, &sz_diff);
	return g_file_set_contents (values[2], data_diff, sz_diff, error);
}

static gboolean
dfu_tool_replace_data (DfuToolPrivate *priv, gchar **values, GError **error)
{
	GPtrArray *images;
	guint cnt = 0;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GBytes) data_search = NULL;
	g_autoptr(GBytes) data_replace = NULL;

	/* check args */
	if (g_strv_length (values) < 3) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments, expected FILE SEARCH REPLACE"
				     " -- e.g. `firmware.dfu deadbeef beefdead");
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

	/* parse hex values */
	data_search = dfu_tool_parse_hex_string (values[1], error);
	if (data_search == NULL)
		return FALSE;
	data_replace = dfu_tool_parse_hex_string (values[2], error);
	if (data_replace == NULL)
		return FALSE;
	if (g_bytes_get_size (data_search) != g_bytes_get_size (data_replace)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "search and replace were different sizes");
		return FALSE;
	}

	/* get each data segment */
	images = dfu_firmware_get_images (firmware);
	for (guint i = 0; i < images->len; i++) {
		DfuImage *image = g_ptr_array_index (images, i);
		GPtrArray *elements = dfu_image_get_elements (image);
		for (guint j = 0; j < elements->len; j++) {
			DfuElement *element = g_ptr_array_index (elements, j);
			GBytes *contents = dfu_element_get_contents (element);
			if (contents == NULL)
				continue;
			cnt += dfu_tool_bytes_replace (contents, data_search, data_replace);
		}
	}

	/* nothing done */
	if (cnt == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "search string was not found");
		return FALSE;
	}

	/* write out new file */
	return dfu_firmware_write_file (firmware,
					file,
					priv->cancellable,
					error);
}

static gboolean
dfu_tool_set_target_size (DfuToolPrivate *priv, gchar **values, GError **error)
{
	DfuElement *element;
	DfuImage *image;
	gchar *endptr;
	guint64 padding_char = 0x00;
	guint64 target_size;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GFile) file = NULL;

	/* check args */
	if (g_strv_length (values) < 2) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments, expected FILE SIZE [VAL]"
				     " -- e.g. `firmware.dfu 8000 ff");
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

	/* doesn't make sense for DfuSe */
	if (dfu_firmware_get_format (firmware) == DFU_FIRMWARE_FORMAT_DFUSE) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Cannot pad DfuSe image, try DFU");
		return FALSE;
	}

	/* parse target size */
	target_size = g_ascii_strtoull (values[1], &endptr, 16);
	if (target_size > 0xffff || endptr[0] != '\0') {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Failed to parse target size '%s'",
			     values[1]);
		return FALSE;
	}

	/* parse padding value */
	if (g_strv_length (values) > 3) {
		padding_char = g_ascii_strtoull (values[2], &endptr, 16);
		if (padding_char > 0xff || endptr[0] != '\0') {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Failed to parse padding value '%s'",
				     values[2]);
			return FALSE;
		}
	}

	/* this has to exist */
	if (target_size > 0) {
		image = dfu_firmware_get_image_default (firmware);
		g_assert (image != NULL);
		element = dfu_image_get_element (image, 0);
		dfu_element_set_padding_value (element, (guint8) padding_char);
		dfu_element_set_target_size (element, (guint32) target_size);
	}

	/* write out new file */
	return dfu_firmware_write_file (firmware,
					file,
					priv->cancellable,
					error);
}

static gboolean
dfu_tool_set_address (DfuToolPrivate *priv, gchar **values, GError **error)
{
	DfuElement *element;
	DfuFirmwareFormat firmware_format;
	DfuImage *image;
	gchar *endptr;
	guint64 address;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GFile) file = NULL;

	/* check args */
	if (g_strv_length (values) < 2) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments, expected FILE ADDR"
				     " -- e.g. `firmware.dfu 8000");
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

	/* only makes sense for DfuSe */
	firmware_format = dfu_firmware_get_format (firmware);
	if (firmware_format != DFU_FIRMWARE_FORMAT_DFUSE) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Cannot set address of %s image, try DfuSe",
			     dfu_firmware_format_to_string (firmware_format));
		return FALSE;
	}

	/* parse address */
	address = g_ascii_strtoull (values[1], &endptr, 16);
	if (address > G_MAXUINT32 || endptr[0] != '\0') {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Failed to parse address '%s'",
			     values[1]);
		return FALSE;
	}

	/* this has to exist */
	if (address > 0) {
		image = dfu_firmware_get_image_default (firmware);
		g_assert (image != NULL);
		element = dfu_image_get_element (image, 0);
		dfu_element_set_address (element, (guint32) address);
	}

	/* write out new file */
	return dfu_firmware_write_file (firmware,
					file,
					priv->cancellable,
					error);
}

static gboolean
dfu_tool_set_metadata (DfuToolPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GFile) file = NULL;

	/* check args */
	if (g_strv_length (values) < 3) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
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
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
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
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
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
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Only possible on DfuSe images, try convert");
		return FALSE;
	}

	/* parse VID */
	tmp = g_ascii_strtoull (values[1], NULL, 10);
	if (tmp == 0 || tmp > G_MAXUINT8) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Failed to parse alternative setting '%s'",
			     values[1]);
		return FALSE;
	}
	image = dfu_firmware_get_image_default (firmware);
	if (image == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "found no image '%s'", values[1]);
		return FALSE;
	}
	dfu_image_set_alt_setting (image, (guint8) tmp);

	/* write out new file */
	return dfu_firmware_write_file (firmware,
					file,
					priv->cancellable,
					error);
}

static gboolean
dfu_tool_set_alt_setting_name (DfuToolPrivate *priv, gchar **values, GError **error)
{
	DfuImage *image;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GFile) file = NULL;

	/* check args */
	if (g_strv_length (values) < 2) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
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
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Only possible on DfuSe images, try convert");
		return FALSE;
	}

	/* parse VID */
	image = dfu_firmware_get_image_default (firmware);
	if (image == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
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

static gboolean
dfu_tool_merge (DfuToolPrivate *priv, gchar **values, GError **error)
{
	guint16 pid = 0xffff;
	guint16 rel = 0xffff;
	guint16 vid = 0xffff;
	g_autofree gchar *str_debug = NULL;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GFile) file = NULL;

	/* check args */
	if (g_strv_length (values) < 3) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments, expected "
				     "FILE-OUT FILE1 FILE2 [FILE3...]"
				     " -- e.g. `combined.dfu lib.dfu app.dfu`");
		return FALSE;
	}

	/* parse source files */
	firmware = dfu_firmware_new ();
	dfu_firmware_set_format (firmware, DFU_FIRMWARE_FORMAT_DFUSE);
	for (guint i = 1; values[i] != NULL; i++) {
		GPtrArray *images;
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
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "Vendor ID was already set as "
				     "0x%04x, %s is 0x%04x",
				     vid, values[i],
				     dfu_firmware_get_vid (firmware_tmp));
			return FALSE;
		}
		if (pid != 0xffff &&
		    dfu_firmware_get_pid (firmware_tmp) != pid) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "Product ID was already set as "
				     "0x%04x, %s is 0x%04x",
				     pid, values[i],
				     dfu_firmware_get_pid (firmware_tmp));
			return FALSE;
		}
		if (rel != 0xffff &&
		    dfu_firmware_get_release (firmware_tmp) != rel) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "Release was already set as "
				     "0x%04x, %s is 0x%04x",
				     rel, values[i],
				     dfu_firmware_get_release (firmware_tmp));
			return FALSE;
		}

		/* add all images to destination */
		images = dfu_firmware_get_images (firmware_tmp);
		for (guint j = 0; j < images->len; j++) {
			DfuImage *image;
			guint8 alt_id;

			/* verify the alt-setting does not already exist */
			image = g_ptr_array_index (images, j);
			alt_id = dfu_image_get_alt_setting (image);
			g_print ("Adding alternative setting ID of 0x%02x\n",
				 alt_id);
			if (dfu_firmware_get_image (firmware, alt_id) != NULL) {
				if (!priv->force) {
					g_set_error (error,
						     FWUPD_ERROR,
						     FWUPD_ERROR_INVALID_FILE,
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

static gboolean
dfu_tool_convert (DfuToolPrivate *priv, gchar **values, GError **error)
{
	DfuFirmwareFormat format;
	guint argc = g_strv_length (values);
	g_autofree gchar *str_debug = NULL;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GFile) file_in = NULL;
	g_autoptr(GFile) file_out = NULL;

	/* check args */
	if (argc != 3) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments, expected "
				     "FORMAT FILE-IN FILE-OUT"
				     " -- e.g. `dfu firmware.hex firmware.dfu`");
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
	format = dfu_firmware_format_from_string (values[0]);
	dfu_firmware_set_format (firmware, format);
	if (format == DFU_FIRMWARE_FORMAT_UNKNOWN) {
		g_autoptr(GString) tmp = g_string_new (NULL);
		for (guint i = 1; i < DFU_FIRMWARE_FORMAT_LAST; i++) {
			if (tmp->len > 0)
				g_string_append (tmp, "|");
			g_string_append (tmp, dfu_firmware_format_to_string (i));
		}
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "unknown format '%s', expected [%s]",
			     values[0], tmp->str);
		return FALSE;
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

static gboolean
dfu_tool_attach (DfuToolPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(DfuDevice) device = NULL;
	g_autoptr(FuDeviceLocker) locker  = NULL;

	device = dfu_tool_get_defalt_device (priv, error);
	if (device == NULL)
		return FALSE;
	locker = fu_device_locker_new_full (device,
					    (FuDeviceLockerFunc) dfu_device_open,
					    (FuDeviceLockerFunc) dfu_device_close,
					    error);
	if (locker == NULL)
		return FALSE;
	return dfu_device_attach (device, error);
}

static gboolean
dfu_tool_reset (DfuToolPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(DfuDevice) device = NULL;
	g_autoptr(FuDeviceLocker) locker  = NULL;

	device = dfu_tool_get_defalt_device (priv, error);
	if (device == NULL)
		return FALSE;
	locker = fu_device_locker_new_full (device,
					    (FuDeviceLockerFunc) dfu_device_open,
					    (FuDeviceLockerFunc) dfu_device_close,
					    error);
	if (locker == NULL)
		return FALSE;
	return dfu_device_reset (device, error);
}

static void
fu_tool_percentage_changed_cb (DfuDevice *device,
			       guint percentage,
			       DfuToolPrivate *priv)
{
	fu_progressbar_update (priv->progressbar, FWUPD_STATUS_UNKNOWN, percentage);
}

static void
fu_tool_action_changed_cb (DfuDevice *device,
			   FwupdStatus action,
			   DfuToolPrivate *priv)
{
	fu_progressbar_update (priv->progressbar, action, 0);
}

static gboolean
dfu_tool_read_alt (DfuToolPrivate *priv, gchar **values, GError **error)
{
	DfuTargetTransferFlags flags = DFU_TARGET_TRANSFER_FLAG_NONE;
	g_autofree gchar *str_debug = NULL;
	g_autoptr(DfuDevice) device = NULL;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(DfuImage) image = NULL;
	g_autoptr(DfuTarget) target = NULL;
	g_autoptr(FuDeviceLocker) locker  = NULL;
	g_autoptr(GFile) file = NULL;

	/* check args */
	if (g_strv_length (values) < 2) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
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
	locker = fu_device_locker_new_full (device,
					    (FuDeviceLockerFunc) dfu_device_open,
					    (FuDeviceLockerFunc) dfu_device_close,
					    error);
	if (locker == NULL)
		return FALSE;

	/* set up progress */
	g_signal_connect (device, "action-changed",
			  G_CALLBACK (fu_tool_action_changed_cb), priv);
	g_signal_connect (device, "percentage-changed",
			  G_CALLBACK (fu_tool_percentage_changed_cb), priv);

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
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Failed to parse alt-setting '%s'",
				     values[1]);
			return FALSE;
		}
		target = dfu_device_get_target_by_alt_setting (device,
							       (guint8) tmp,
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
	dfu_firmware_set_format (firmware, DFU_FIRMWARE_FORMAT_DFU);
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

static gboolean
dfu_tool_read (DfuToolPrivate *priv, gchar **values, GError **error)
{
	DfuFirmwareFormat format;
	DfuTargetTransferFlags flags = DFU_TARGET_TRANSFER_FLAG_NONE;
	g_autofree gchar *str_debug = NULL;
	g_autoptr(DfuDevice) device = NULL;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(FuDeviceLocker) locker  = NULL;
	g_autoptr(GFile) file = NULL;

	/* check args */
	if (g_strv_length (values) == 1) {
		/* guess output format */
		if (g_str_has_suffix (values[0], ".dfu")) {
			format = DFU_FIRMWARE_FORMAT_DFU;
		} else if (g_str_has_suffix (values[0], ".bin") ||
			   g_str_has_suffix (values[0], ".rom")) {
			format = DFU_FIRMWARE_FORMAT_RAW;
		} else if (g_str_has_suffix (values[0], ".ihex") ||
			   g_str_has_suffix (values[0], ".hex")) {
			format = DFU_FIRMWARE_FORMAT_INTEL_HEX;
		} else {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "Could not guess a file format");
			return FALSE;
		}
	} else if (g_strv_length (values) == 2) {
		format = dfu_firmware_format_from_string (values[1]);
		if (format == DFU_FIRMWARE_FORMAT_UNKNOWN) {
			g_autoptr(GString) tmp = g_string_new (NULL);
			for (guint i = 1; i < DFU_FIRMWARE_FORMAT_LAST; i++) {
				if (tmp->len > 0)
					g_string_append (tmp, "|");
				g_string_append (tmp, dfu_firmware_format_to_string (i));
			}
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "unknown format '%s', expected [%s]",
				     values[0], tmp->str);
			return FALSE;
		}
	} else {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments, expected FILENAME [FORMAT]");
		return FALSE;
	}

	/* open correct device */
	device = dfu_tool_get_defalt_device (priv, error);
	if (device == NULL)
		return FALSE;
	locker = fu_device_locker_new_full (device,
					    (FuDeviceLockerFunc) dfu_device_open,
					    (FuDeviceLockerFunc) dfu_device_close,
					    error);
	if (locker == NULL)
		return FALSE;

	/* optional reset */
	if (dfu_device_get_mode (device) == DFU_MODE_RUNTIME) {
		flags |= DFU_TARGET_TRANSFER_FLAG_DETACH;
		flags |= DFU_TARGET_TRANSFER_FLAG_ATTACH;
		flags |= DFU_TARGET_TRANSFER_FLAG_WAIT_RUNTIME;
	}

	/* transfer */
	g_signal_connect (device, "action-changed",
			  G_CALLBACK (fu_tool_action_changed_cb), priv);
	g_signal_connect (device, "percentage-changed",
			  G_CALLBACK (fu_tool_percentage_changed_cb), priv);
	firmware = dfu_device_upload (device,
				      flags,
				      priv->cancellable,
				      error);
	if (firmware == NULL)
		return FALSE;

	/* save file */
	file = g_file_new_for_path (values[0]);
	dfu_firmware_set_format (firmware, format);
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

static gchar *
dfu_tool_get_device_string (DfuToolPrivate *priv, DfuDevice *device)
{
	GUsbDevice *dev;
	g_autoptr(FuDeviceLocker) locker  = NULL;

	/* open if required, and get status */
	dev = dfu_device_get_usb_dev (device);
	if (dev == NULL) {
		return g_strdup_printf ("%04x:%04x [%s]",
					dfu_device_get_runtime_vid (device),
					dfu_device_get_runtime_pid (device),
					"removed");
	}
	if (!dfu_device_is_open (device)) {
		g_autoptr(GError) error = NULL;
		locker = fu_device_locker_new_full (device,
						    (FuDeviceLockerFunc) dfu_device_open,
						    (FuDeviceLockerFunc) dfu_device_close,
						    &error);
		if (locker == NULL) {
			return g_strdup_printf ("%04x:%04x [%s]",
						g_usb_device_get_vid (dev),
						g_usb_device_get_pid (dev),
						error->message);
		}
	}
	return g_strdup_printf ("%04x:%04x [%s:%s]",
				g_usb_device_get_vid (dev),
				g_usb_device_get_pid (dev),
				dfu_state_to_string (dfu_device_get_state (device)),
				dfu_status_to_string (dfu_device_get_status (device)));
}

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

static void
dfu_tool_device_changed_cb (DfuContext *context, DfuDevice *device, gpointer user_data)
{
	DfuToolPrivate *priv = (DfuToolPrivate *) user_data;
	g_autofree gchar *tmp = dfu_tool_get_device_string (priv, device);
	/* TRANSLATORS: this is when a device is hotplugged */
	dfu_tool_print_indent (_("Changed"), tmp, 0);
}

static void
dfu_tool_watch_cancelled_cb (GCancellable *cancellable, gpointer user_data)
{
	GMainLoop *loop = (GMainLoop *) user_data;
	/* TRANSLATORS: this is when a device ctrl+c's a watch */
	g_print ("%s\n", _("Cancelled"));
	g_main_loop_quit (loop);
}

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
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "No default image");
		return NULL;
	}
	element = dfu_image_get_element (image, 0);
	if (element == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "No default element");
		return NULL;
	}
	contents = dfu_element_get_contents (element);
	if (contents == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "No image contents");
		return NULL;
	}
	return (guint8 *) g_bytes_get_data (contents, length);
}

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
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments, expected "
				     "FILENAME-IN FILENAME-OUT TYPE KEY"
				     " -- e.g. firmware.dfu firmware.xdfu xtea deadbeef");
		return FALSE;
	}

	/* check extensions */
	if (!priv->force) {
		if (!g_str_has_suffix (values[0], ".dfu")) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "Invalid filename, expected *.dfu");
			return FALSE;
		}
		if (!g_str_has_suffix (values[1], ".xdfu")) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
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
		if (!dfu_cipher_encrypt_xtea (values[3], data, (guint32) len, error))
			return FALSE;
		dfu_firmware_set_metadata (firmware,
					   DFU_METADATA_KEY_CIPHER_KIND,
					   "XTEA");
	} else {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
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
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments, expected "
				     "FILENAME-IN FILENAME-OUT TYPE KEY"
				     " -- e.g. firmware.xdfu firmware.dfu xtea deadbeef");
		return FALSE;
	}

	/* check extensions */
	if (!priv->force) {
		if (!g_str_has_suffix (values[0], ".xdfu")) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "Invalid filename, expected *.xdfu");
			return FALSE;
		}
		if (!g_str_has_suffix (values[1], ".dfu")) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
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
		if (!dfu_cipher_decrypt_xtea (values[3], data, (guint32) len, error))
			return FALSE;
		dfu_firmware_remove_metadata (firmware,
					      DFU_METADATA_KEY_CIPHER_KIND);
	} else {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
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

static gboolean
dfu_tool_watch (DfuToolPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(DfuContext) dfu_context = NULL;
	g_autoptr(GMainLoop) loop = NULL;
	g_autoptr(GPtrArray) devices = NULL;

	/* get all the DFU devices */
	dfu_context = dfu_context_new ();
	dfu_context_enumerate (dfu_context, NULL);

	/* print what's already attached */
	devices = dfu_context_get_devices (dfu_context);
	for (guint i = 0; i < devices->len; i++) {
		DfuDevice *device = g_ptr_array_index (devices, i);
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

static gboolean
dfu_tool_dump (DfuToolPrivate *priv, gchar **values, GError **error)
{
	DfuFirmwareParseFlags flags = DFU_FIRMWARE_PARSE_FLAG_NONE;

	/* check args */
	if (g_strv_length (values) < 1) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments, expected FILENAME");
		return FALSE;
	}

	/* dump corrupt files */
	if (priv->force) {
		flags |= DFU_FIRMWARE_PARSE_FLAG_NO_CRC_TEST;
		flags |= DFU_FIRMWARE_PARSE_FLAG_NO_VERSION_TEST;
	}

	/* open files */
	for (guint i = 0; values[i] != NULL; i++) {
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

static gboolean
dfu_tool_write_alt (DfuToolPrivate *priv, gchar **values, GError **error)
{
	DfuImage *image;
	DfuTargetTransferFlags flags = DFU_TARGET_TRANSFER_FLAG_VERIFY;
	g_autofree gchar *str_debug = NULL;
	g_autoptr(DfuDevice) device = NULL;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(DfuTarget) target = NULL;
	g_autoptr(FuDeviceLocker) locker  = NULL;
	g_autoptr(GFile) file = NULL;

	/* check args */
	if (g_strv_length (values) < 2) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
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
	locker = fu_device_locker_new_full (device,
					    (FuDeviceLockerFunc) dfu_device_open,
					    (FuDeviceLockerFunc) dfu_device_close,
					    error);
	if (locker == NULL)
		return FALSE;

	/* set up progress */
	g_signal_connect (device, "action-changed",
			  G_CALLBACK (fu_tool_action_changed_cb), priv);
	g_signal_connect (device, "percentage-changed",
			  G_CALLBACK (fu_tool_percentage_changed_cb), priv);

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
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Failed to parse alt-setting '%s'",
				     values[1]);
			return FALSE;
		}
		target = dfu_device_get_target_by_alt_setting (device,
							       (guint8) tmp,
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
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "Failed to parse image alt-setting '%s'",
					     values[2]);
				return FALSE;
			}
			image = dfu_firmware_get_image (firmware, (guint8) tmp);
			if (image == NULL) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
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
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
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

static gboolean
dfu_tool_write (DfuToolPrivate *priv, gchar **values, GError **error)
{
	DfuTargetTransferFlags flags = DFU_TARGET_TRANSFER_FLAG_VERIFY;
	g_autofree gchar *str_debug = NULL;
	g_autoptr(DfuDevice) device = NULL;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(FuDeviceLocker) locker  = NULL;
	g_autoptr(GFile) file = NULL;

	/* check args */
	if (g_strv_length (values) < 1) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
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
	locker = fu_device_locker_new_full (device,
					    (FuDeviceLockerFunc) dfu_device_open,
					    (FuDeviceLockerFunc) dfu_device_close,
					    error);
	if (locker == NULL)
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
	g_signal_connect (device, "action-changed",
			  G_CALLBACK (fu_tool_action_changed_cb), priv);
	g_signal_connect (device, "percentage-changed",
			  G_CALLBACK (fu_tool_percentage_changed_cb), priv);
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

static void
dfu_tool_list_target (DfuTarget *target)
{
	DfuCipherKind cipher_kind;
	GPtrArray *sectors;
	const gchar *tmp;
	g_autofree gchar *alt_id = NULL;
	g_autoptr(GError) error_local = NULL;

	/* TRANSLATORS: the identifier name please */
	alt_id = g_strdup_printf ("%i", dfu_target_get_alt_setting (target));
	dfu_tool_print_indent (_("ID"), alt_id, 1);

	/* this is optional */
	tmp = dfu_target_get_alt_name_for_display (target, &error_local);
	if (tmp != NULL) {
		/* TRANSLATORS: interface name, e.g. "Flash" */
		dfu_tool_print_indent (_("Name"), tmp, 2);
	} else if (!g_error_matches (error_local,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND)) {
		g_autofree gchar *str = NULL;
		str = g_strdup_printf ("Error: %s", error_local->message);
		dfu_tool_print_indent (_("Name"), str, 2);
	}

	/* this is optional */
	cipher_kind = dfu_target_get_cipher_kind (target);
	if (cipher_kind != DFU_CIPHER_KIND_NONE) {
		/* TRANSLATORS: this is the encryption method used when writing  */
		dfu_tool_print_indent (_("Cipher"),
				       dfu_cipher_kind_to_string (cipher_kind),
				       2);
	}

	/* print sector information */
	sectors = dfu_target_get_sectors (target);
	for (guint i = 0; i < sectors->len; i++) {
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

static gchar *
_bcd_version_from_uint16 (guint16 val)
{
#if AS_CHECK_VERSION(0,7,3)
	return as_utils_version_from_uint16 (val, AS_VERSION_PARSE_FLAG_USE_BCD);
#else
	guint maj = ((val >> 12) & 0x0f) * 10 + ((val >> 8) & 0x0f);
	guint min = ((val >> 4) & 0x0f) * 10 + (val & 0x0f);
	return g_strdup_printf ("%u.%u", maj, min);
#endif
}

static gboolean
dfu_tool_list (DfuToolPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(DfuContext) dfu_context = NULL;
	g_autoptr(GPtrArray) devices = NULL;

	/* get all the connected USB devices */
	dfu_context = dfu_context_new ();
	dfu_context_enumerate (dfu_context, NULL);
	devices = dfu_context_get_devices (dfu_context);
	for (guint i = 0; i < devices->len; i++) {
		DfuDevice *device = NULL;
		DfuTarget *target;
		GUsbDevice *dev;
		GPtrArray *dfu_targets;
		const gchar *tmp;
		guint16 transfer_size;
		g_autofree gchar *attrs = NULL;
		g_autofree gchar *quirks = NULL;
		g_autofree gchar *version = NULL;
		g_autoptr(FuDeviceLocker) locker  = NULL;
		g_autoptr(GError) error_local = NULL;

		/* device specific */
		device = g_ptr_array_index (devices, i);
		dev = dfu_device_get_usb_dev (device);
		version = _bcd_version_from_uint16 (g_usb_device_get_release (dev));
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
		locker = fu_device_locker_new_full (device,
						    (FuDeviceLockerFunc) dfu_device_open,
						    (FuDeviceLockerFunc) dfu_device_close,
						    &error_local);
		if (locker == NULL) {
			if (g_error_matches (error_local,
					     FWUPD_ERROR,
					     FWUPD_ERROR_PERMISSION_DENIED)) {
				/* TRANSLATORS: probably not run as root... */
				dfu_tool_print_indent (_("Status"), _("Permission denied"), 1);
				continue;
			}
			if (g_error_matches (error_local,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED)) {
				g_debug ("ignoring warning, continuing...");
			} else {
				/* TRANSLATORS: device has failed to report status */
				dfu_tool_print_indent (_("Status"), error_local->message, 1);
			}
		}

		tmp = dfu_device_get_display_name (device);
		if (tmp != NULL) {
			/* TRANSLATORS: device name, e.g. 'ColorHug2' */
			dfu_tool_print_indent (_("Name"), tmp, 1);
		}

		tmp = dfu_device_get_serial_number (device);
		if (tmp != NULL) {
			/* TRANSLATORS: serial number, e.g. '00012345' */
			dfu_tool_print_indent (_("Serial"), tmp, 1);
		}

		tmp = dfu_mode_to_string (dfu_device_get_mode (device));
		if (tmp != NULL) {
			/* TRANSLATORS: device mode, e.g. runtime or DFU */
			dfu_tool_print_indent (_("Mode"), tmp, 1);
		}
		tmp = dfu_status_to_string (dfu_device_get_status (device));
		/* TRANSLATORS: device status, e.g. "OK" */
		dfu_tool_print_indent (_("Status"), tmp, 1);

		tmp = dfu_state_to_string (dfu_device_get_state (device));
		/* TRANSLATORS: device state, i.e. appIDLE */
		dfu_tool_print_indent (_("State"), tmp, 1);

		transfer_size = dfu_device_get_transfer_size (device);
		if (transfer_size > 0) {
			g_autofree gchar *str = NULL;
			str = g_format_size_full (transfer_size,
						  G_FORMAT_SIZE_LONG_FORMAT);
			/* TRANSLATORS: transfer size in bytes */
			dfu_tool_print_indent (_("Transfer Size"), str, 1);
		}

		/* attributes can be an empty string */
		attrs = dfu_device_get_attributes_as_string (device);
		if (attrs != NULL && attrs[0] != '\0') {
			/* TRANSLATORS: device attributes, i.e. things that
			 * the device can do */
			dfu_tool_print_indent (_("Attributes"), attrs, 1);
		}

		/* quirks are NULL if none are set */
		quirks = dfu_device_get_quirks_as_string (device);
		if (quirks != NULL) {
			/* TRANSLATORS: device quirks, i.e. things that
			 * it does that we have to work around */
			dfu_tool_print_indent (_("Quirks"), quirks, 1);
		}

		/* this is optional */
		tmp = dfu_device_get_chip_id (device);
		if (tmp != NULL) {
			/* TRANSLATORS: chip ID, e.g. "0x58200204" */
			dfu_tool_print_indent (_("Chip ID"), tmp, 1);
		}

		/* list targets */
		dfu_targets = dfu_device_get_targets (device);
		for (guint j = 0; j < dfu_targets->len; j++) {
			target = g_ptr_array_index (dfu_targets, j);
			dfu_tool_list_target (target);
		}
	}
	return TRUE;
}

static gboolean
dfu_tool_detach (DfuToolPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(DfuDevice) device = NULL;
	g_autoptr(FuDeviceLocker) locker  = NULL;

	/* open correct device */
	device = dfu_tool_get_defalt_device (priv, error);
	if (device == NULL)
		return FALSE;
	if (priv->transfer_size > 0)
		dfu_device_set_transfer_size (device, priv->transfer_size);

	/* detatch */
	locker = fu_device_locker_new_full (device,
					    (FuDeviceLockerFunc) dfu_device_open,
					    (FuDeviceLockerFunc) dfu_device_close,
					    error);
	if (locker == NULL)
		return FALSE;
	return dfu_device_detach (device, priv->cancellable, error);
}

static gboolean
dfu_tool_sigint_cb (gpointer user_data)
{
	DfuToolPrivate *priv = (DfuToolPrivate *) user_data;
	g_debug ("Handling SIGINT");
	g_cancellable_cancel (priv->cancellable);
	return FALSE;
}

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
		     "set-address",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Set element address on firmware file"),
		     dfu_tool_set_address);
	dfu_tool_add (priv->cmd_array,
		     "set-target-size",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Set the firmware size for the target"),
		     dfu_tool_set_target_size);
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
		     "reset",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Reset a DFU device"),
		     dfu_tool_reset);
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
	dfu_tool_add (priv->cmd_array,
		     "replace-data",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Replace data in an existing firmware file"),
		     dfu_tool_replace_data);
	dfu_tool_add (priv->cmd_array,
		     "patch-create",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Create a binary patch using two files"),
		     dfu_tool_patch_create);
	dfu_tool_add (priv->cmd_array,
		     "patch-apply",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Apply a binary patch"),
		     dfu_tool_patch_apply);
	dfu_tool_add (priv->cmd_array,
		     "patch-dump",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Dump information about a binary patch to the screen"),
		     dfu_tool_patch_dump);

	/* use animated progress bar */
	priv->progressbar = fu_progressbar_new ();
	fu_progressbar_set_length_percentage (priv->progressbar, 50);
	fu_progressbar_set_length_status (priv->progressbar, 20);

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
		if (g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL)) {
			g_autofree gchar *tmp = NULL;
			tmp = g_option_context_get_help (context, TRUE, NULL);
			g_print ("%s\n\n%s", error->message, tmp);
		} else {
			g_print ("%s\n", error->message);
		}
		return EXIT_FAILURE;
	}

	/* success/ */
	g_object_unref (priv->progressbar);
	return EXIT_SUCCESS;
}
