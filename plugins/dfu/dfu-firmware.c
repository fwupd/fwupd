/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

/**
 * SECTION:dfu-firmware
 * @short_description: Object representing a DFU or DfuSe firmware file
 *
 * This object allows reading and writing firmware files either in
 * raw, DFU or DfuSe formats.
 *
 * A #DfuFirmware can be made up of several #DfuImages, although
 * typically there is only one.
 *
 * See also: #DfuImage
 */

#include "config.h"

#include <string.h>
#include <stdio.h>
#include <appstream-glib.h>

#include "dfu-common.h"
#include "dfu-error.h"
#include "dfu-firmware-private.h"
#include "dfu-format-dfu.h"
#include "dfu-format-ihex.h"
#include "dfu-format-raw.h"
#include "dfu-image.h"

static void dfu_firmware_finalize			 (GObject *object);

typedef struct {
	GHashTable		*metadata;
	GHashTable		*symtab;
	GPtrArray		*images;
	guint16			 vid;
	guint16			 pid;
	guint16			 release;
	DfuCipherKind		 cipher_kind;
	DfuFirmwareFormat	 format;
} DfuFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (DfuFirmware, dfu_firmware, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (dfu_firmware_get_instance_private (o))

static void
dfu_firmware_class_init (DfuFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = dfu_firmware_finalize;
}

static void
dfu_firmware_init (DfuFirmware *firmware)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	priv->vid = 0xffff;
	priv->pid = 0xffff;
	priv->release = 0xffff;
	priv->images = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	priv->metadata = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	priv->symtab = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

static void
dfu_firmware_finalize (GObject *object)
{
	DfuFirmware *firmware = DFU_FIRMWARE (object);
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);

	g_ptr_array_unref (priv->images);
	g_hash_table_destroy (priv->metadata);
	g_hash_table_destroy (priv->symtab);

	G_OBJECT_CLASS (dfu_firmware_parent_class)->finalize (object);
}

/**
 * dfu_firmware_new:
 *
 * Creates a new DFU firmware object.
 *
 * Return value: a new #DfuFirmware
 *
 * Since: 0.5.4
 **/
DfuFirmware *
dfu_firmware_new (void)
{
	DfuFirmware *firmware;
	firmware = g_object_new (DFU_TYPE_FIRMWARE, NULL);
	return firmware;
}

/**
 * dfu_firmware_get_image:
 * @firmware: a #DfuFirmware
 * @alt_setting: an alternative setting, typically 0x00
 *
 * Gets an image from the firmware file.
 *
 * Return value: (transfer none): a #DfuImage, or %NULL for not found
 *
 * Since: 0.5.4
 **/
DfuImage *
dfu_firmware_get_image (DfuFirmware *firmware, guint8 alt_setting)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	DfuImage *im;
	guint i;

	g_return_val_if_fail (DFU_IS_FIRMWARE (firmware), NULL);

	/* find correct image */
	for (i = 0; i < priv->images->len; i++) {
		im = g_ptr_array_index (priv->images, i);
		if (dfu_image_get_alt_setting (im) == alt_setting)
			return im;
	}
	return NULL;
}

/**
 * dfu_firmware_get_image_by_name:
 * @firmware: a #DfuFirmware
 * @name: an alternative setting name
 *
 * Gets an image from the firmware file.
 *
 * Return value: (transfer none): a #DfuImage, or %NULL for not found
 *
 * Since: 0.5.4
 **/
DfuImage *
dfu_firmware_get_image_by_name (DfuFirmware *firmware, const gchar *name)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	DfuImage *im;
	guint i;

	g_return_val_if_fail (DFU_IS_FIRMWARE (firmware), NULL);

	/* find correct image */
	for (i = 0; i < priv->images->len; i++) {
		im = g_ptr_array_index (priv->images, i);
		if (g_strcmp0 (dfu_image_get_name (im), name) == 0)
			return im;
	}
	return NULL;
}

/**
 * dfu_firmware_get_image_default:
 * @firmware: a #DfuFirmware
 *
 * Gets the default image from the firmware file.
 *
 * Return value: (transfer none): a #DfuImage, or %NULL for not found
 *
 * Since: 0.5.4
 **/
DfuImage *
dfu_firmware_get_image_default (DfuFirmware *firmware)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_return_val_if_fail (DFU_IS_FIRMWARE (firmware), NULL);
	if (priv->images->len == 0)
		return NULL;
	return g_ptr_array_index (priv->images, 0);
}

/**
 * dfu_firmware_get_images:
 * @firmware: a #DfuFirmware
 *
 * Gets all the images contained in this firmware file.
 *
 * Return value: (transfer none) (element-type DfuImage): list of images
 *
 * Since: 0.5.4
 **/
GPtrArray *
dfu_firmware_get_images (DfuFirmware *firmware)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_return_val_if_fail (DFU_IS_FIRMWARE (firmware), NULL);
	return priv->images;
}

/**
 * dfu_firmware_get_size:
 * @firmware: a #DfuFirmware
 *
 * Gets the size of all the images in the firmware.
 *
 * This only returns actual data that would be sent to the device and
 * does not include any padding.
 *
 * Return value: a integer value, or 0 if there are no images.
 *
 * Since: 0.5.4
 **/
guint32
dfu_firmware_get_size (DfuFirmware *firmware)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	guint32 length = 0;
	guint i;
	g_return_val_if_fail (DFU_IS_FIRMWARE (firmware), 0);
	for (i = 0; i < priv->images->len; i++) {
		DfuImage *image = g_ptr_array_index (priv->images, i);
		length += dfu_image_get_size (image);
	}
	return length;
}

/**
 * dfu_firmware_add_image:
 * @firmware: a #DfuFirmware
 * @image: a #DfuImage
 *
 * Adds an image to the list of images.
 *
 * Since: 0.5.4
 **/
void
dfu_firmware_add_image (DfuFirmware *firmware, DfuImage *image)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_return_if_fail (DFU_IS_FIRMWARE (firmware));
	g_return_if_fail (DFU_IS_IMAGE (image));
	g_ptr_array_add (priv->images, g_object_ref (image));
}

/**
 * dfu_firmware_get_vid:
 * @firmware: a #DfuFirmware
 *
 * Gets the vendor ID.
 *
 * Return value: a vendor ID, or 0xffff for unset
 *
 * Since: 0.5.4
 **/
guint16
dfu_firmware_get_vid (DfuFirmware *firmware)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_return_val_if_fail (DFU_IS_FIRMWARE (firmware), 0xffff);
	return priv->vid;
}

/**
 * dfu_firmware_get_pid:
 * @firmware: a #DfuFirmware
 *
 * Gets the product ID.
 *
 * Return value: a product ID, or 0xffff for unset
 *
 * Since: 0.5.4
 **/
guint16
dfu_firmware_get_pid (DfuFirmware *firmware)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_return_val_if_fail (DFU_IS_FIRMWARE (firmware), 0xffff);
	return priv->pid;
}

/**
 * dfu_firmware_get_release:
 * @firmware: a #DfuFirmware
 *
 * Gets the device ID.
 *
 * Return value: a device ID, or 0xffff for unset
 *
 * Since: 0.5.4
 **/
guint16
dfu_firmware_get_release (DfuFirmware *firmware)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_return_val_if_fail (DFU_IS_FIRMWARE (firmware), 0xffff);
	return priv->release;
}

/**
 * dfu_firmware_get_format:
 * @firmware: a #DfuFirmware
 *
 * Gets the DFU version.
 *
 * Return value: a version, or 0x0 for unset
 *
 * Since: 0.5.4
 **/
guint16
dfu_firmware_get_format (DfuFirmware *firmware)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_return_val_if_fail (DFU_IS_FIRMWARE (firmware), 0xffff);
	return priv->format;
}

/**
 * dfu_firmware_set_vid:
 * @firmware: a #DfuFirmware
 * @vid: vendor ID, or 0xffff for unset
 *
 * Sets the vendor ID.
 *
 * Since: 0.5.4
 **/
void
dfu_firmware_set_vid (DfuFirmware *firmware, guint16 vid)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_return_if_fail (DFU_IS_FIRMWARE (firmware));
	priv->vid = vid;
}

/**
 * dfu_firmware_set_pid:
 * @firmware: a #DfuFirmware
 * @pid: product ID, or 0xffff for unset
 *
 * Sets the product ID.
 *
 * Since: 0.5.4
 **/
void
dfu_firmware_set_pid (DfuFirmware *firmware, guint16 pid)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_return_if_fail (DFU_IS_FIRMWARE (firmware));
	priv->pid = pid;
}

/**
 * dfu_firmware_set_release:
 * @firmware: a #DfuFirmware
 * @release: device ID, or 0xffff for unset
 *
 * Sets the device ID.
 *
 * Since: 0.5.4
 **/
void
dfu_firmware_set_release (DfuFirmware *firmware, guint16 release)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_return_if_fail (DFU_IS_FIRMWARE (firmware));
	priv->release = release;
}

/**
 * dfu_firmware_set_format:
 * @firmware: a #DfuFirmware
 * @format: a #DfuFirmwareFormat, e.g. %DFU_FIRMWARE_FORMAT_DFUSE
 *
 * Sets the DFU version in BCD format.
 *
 * Since: 0.5.4
 **/
void
dfu_firmware_set_format (DfuFirmware *firmware, DfuFirmwareFormat format)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_return_if_fail (DFU_IS_FIRMWARE (firmware));
	priv->format = format;
}

static void
dfu_firmware_parse_altos_vid_pid (DfuFirmware *firmware)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	guint32 addr32;

	/* find symbol */
	addr32 = dfu_firmware_lookup_symbol (firmware, "ao_usb_descriptors");
	if (addr32 == 0x0)
		return;

	/* search each element */
	for (guint i = 0; i < priv->images->len; i++) {
		DfuImage *image = g_ptr_array_index (priv->images, i);
		DfuElement *element;
		GBytes *bytes_tmp;
		const guint8 *data;
		gsize length;
		guint32 element_address;
		guint16 vid, pid, release;

		/* get element data */
		element = dfu_image_get_element_default (image);
		if (element == NULL)
			continue;

		/* check address is in this element */
		element_address = dfu_element_get_address (element);
		if (element_address > addr32)
			continue;
		bytes_tmp = dfu_element_get_contents (element);
		if (bytes_tmp == NULL)
			continue;

		/* check this element is big enough */
		data = g_bytes_get_data (bytes_tmp, &length);
		if (addr32 - element_address > length)
			continue;

		/* read the USB descriptor */
		addr32 -= element_address;
		memcpy (&vid, &data[addr32 + 8], 2);
		memcpy (&pid, &data[addr32 + 10], 2);
		memcpy (&release, &data[addr32 + 12], 2);
		dfu_firmware_set_vid (firmware, GUINT32_FROM_LE (vid));
		dfu_firmware_set_pid (firmware, GUINT32_FROM_LE (pid));
		dfu_firmware_set_release (firmware, GUINT32_FROM_LE (release));
	}
}

/**
 * dfu_firmware_parse_data:
 * @firmware: a #DfuFirmware
 * @bytes: raw firmware data
 * @flags: optional flags, e.g. %DFU_FIRMWARE_PARSE_FLAG_NO_CRC_TEST
 * @error: a #GError, or %NULL
 *
 * Parses firmware data which may have an optional DFU suffix.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.4
 **/
gboolean
dfu_firmware_parse_data (DfuFirmware *firmware, GBytes *bytes,
			 DfuFirmwareParseFlags flags, GError **error)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);

	g_return_val_if_fail (DFU_IS_FIRMWARE (firmware), FALSE);
	g_return_val_if_fail (bytes != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* set defaults */
	priv->vid = 0xffff;
	priv->pid = 0xffff;
	priv->release = 0xffff;

	/* try to get format if not already set */
	if (priv->format == DFU_FIRMWARE_FORMAT_UNKNOWN)
		priv->format = dfu_firmware_detect_ihex (bytes);
	if (priv->format == DFU_FIRMWARE_FORMAT_UNKNOWN)
		priv->format = dfu_firmware_detect_dfu (bytes);
	if (priv->format == DFU_FIRMWARE_FORMAT_UNKNOWN)
		priv->format = dfu_firmware_detect_raw (bytes);

	/* handled easily */
	switch (priv->format) {
	case DFU_FIRMWARE_FORMAT_INTEL_HEX:
		if (!dfu_firmware_from_ihex (firmware, bytes, flags, error))
			return FALSE;
		break;
	case DFU_FIRMWARE_FORMAT_DFU:
	case DFU_FIRMWARE_FORMAT_DFUSE:
		if (!dfu_firmware_from_dfu (firmware, bytes, flags, error))
			return FALSE;
		break;
	default:
		if (!dfu_firmware_from_raw (firmware, bytes, flags, error))
			return FALSE;
		break;
	}

	/* get the VID/PID for altos devices */
	dfu_firmware_parse_altos_vid_pid (firmware);

	return TRUE;
}

/**
 * dfu_firmware_parse_file:
 * @firmware: a #DfuFirmware
 * @file: a #GFile to load and parse
 * @flags: optional flags, e.g. %DFU_FIRMWARE_PARSE_FLAG_NO_CRC_TEST
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Parses a DFU firmware, which may contain an optional footer.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.4
 **/
gboolean
dfu_firmware_parse_file (DfuFirmware *firmware, GFile *file,
			 DfuFirmwareParseFlags flags,
			 GCancellable *cancellable, GError **error)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	gchar *contents = NULL;
	gsize length = 0;
	g_autofree gchar *basename = NULL;
	g_autoptr(GBytes) bytes = NULL;

	g_return_val_if_fail (DFU_IS_FIRMWARE (firmware), FALSE);
	g_return_val_if_fail (G_IS_FILE (file), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* guess cipher kind based on file extension */
	basename = g_file_get_basename (file);
	if (g_str_has_suffix (basename, ".xdfu"))
		priv->cipher_kind = DFU_CIPHER_KIND_XTEA;

	if (!g_file_load_contents (file, cancellable, &contents,
				   &length, NULL, error))
		return FALSE;
	bytes = g_bytes_new_take (contents, length);
	return dfu_firmware_parse_data (firmware, bytes, flags, error);
}

/**
 * dfu_firmware_get_metadata:
 * @firmware: a #DfuFirmware
 * @key: metadata string key
 *
 * Gets metadata from the store with a specific key.
 *
 * Return value: the metadata value, or %NULL for unset
 *
 * Since: 0.5.4
 **/
const gchar *
dfu_firmware_get_metadata (DfuFirmware *firmware, const gchar *key)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	return g_hash_table_lookup (priv->metadata, key);
}

/**
 * dfu_firmware_get_metadata_table:
 * @firmware: a #DfuFirmware
 *
 * Gets all metadata from the store.
 *
 * Return value: (transfer none): the metadata hash table
 *
 * Since: 0.6.3
 **/
GHashTable *
dfu_firmware_get_metadata_table (DfuFirmware *firmware)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	return priv->metadata;
}

/**
 * dfu_firmware_set_metadata:
 * @firmware: a #DfuFirmware
 * @key: metadata string key
 * @value: metadata string value
 *
 * Sets a metadata value with a specific key.
 *
 * Since: 0.5.4
 **/
void
dfu_firmware_set_metadata (DfuFirmware *firmware, const gchar *key, const gchar *value)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_debug ("adding metadata %s=%s", key, value);
	g_hash_table_insert (priv->metadata, g_strdup (key), g_strdup (value));
}

/**
 * dfu_firmware_remove_metadata:
 * @firmware: a #DfuFirmware
 * @key: metadata string key
 *
 * Removes a metadata item from the store
 *
 * Since: 0.5.4
 **/
void
dfu_firmware_remove_metadata (DfuFirmware *firmware, const gchar *key)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_debug ("removing metadata %s", key);
	g_hash_table_remove (priv->metadata, key);
}

static gboolean
dfu_firmware_check_acceptable_for_format (DfuFirmware *firmware, GError **error)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);

	/* always okay */
	if (priv->images->len <= 1)
		return TRUE;
	if (priv->format == DFU_FIRMWARE_FORMAT_DFUSE)
		return TRUE;

	/* one is usual, and 2 is okay if one image is the signature */
	if (priv->format == DFU_FIRMWARE_FORMAT_INTEL_HEX) {
		if (priv->images->len == 2 &&
		    dfu_firmware_get_image_by_name (firmware, "signature") != NULL)
			return TRUE;
	}

	/* unsupported */
	g_set_error (error,
		     DFU_ERROR,
		     DFU_ERROR_INTERNAL,
		     "multiple images (%u) not supported for %s",
		     priv->images->len,
		     dfu_firmware_format_to_string (priv->format));
	return TRUE;
}

/**
 * dfu_firmware_write_data:
 * @firmware: a #DfuFirmware
 * @error: a #GError, or %NULL
 *
 * Writes DFU data to a data blob with a DFU-specific footer.
 *
 * Return value: (transfer none): firmware data
 *
 * Since: 0.5.4
 **/
GBytes *
dfu_firmware_write_data (DfuFirmware *firmware, GError **error)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);

	g_return_val_if_fail (DFU_IS_FIRMWARE (firmware), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* at least one image */
	if (priv->images == 0) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "no image data to write");
		return NULL;
	}

	/* does the format support this many images */
	if (!dfu_firmware_check_acceptable_for_format (firmware, error))
		return NULL;

	/* raw */
	if (priv->format == DFU_FIRMWARE_FORMAT_RAW)
		return dfu_firmware_to_raw (firmware, error);

	/* DFU or DfuSe*/
	if (priv->format == DFU_FIRMWARE_FORMAT_DFU ||
	    priv->format == DFU_FIRMWARE_FORMAT_DFUSE)
		return dfu_firmware_to_dfu (firmware, error);

	/* Intel HEX */
	if (priv->format == DFU_FIRMWARE_FORMAT_INTEL_HEX)
		return dfu_firmware_to_ihex (firmware, error);

	/* invalid */
	g_set_error (error,
		     DFU_ERROR,
		     DFU_ERROR_INTERNAL,
		     "invalid format for write (0x%04x)",
		     priv->format);
	return NULL;
}

/**
 * dfu_firmware_write_file:
 * @firmware: a #DfuFirmware
 * @file: a #GFile
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Writes a DFU firmware with the optional footer.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.4
 **/
gboolean
dfu_firmware_write_file (DfuFirmware *firmware, GFile *file,
			 GCancellable *cancellable, GError **error)
{
	const guint8 *data;
	gsize length = 0;
	g_autoptr(GBytes) bytes = NULL;

	g_return_val_if_fail (DFU_IS_FIRMWARE (firmware), FALSE);
	g_return_val_if_fail (G_IS_FILE (file), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* get blob */
	bytes = dfu_firmware_write_data (firmware, error);
	if (bytes == NULL)
		return FALSE;

	/* save to firmware */
	data = g_bytes_get_data (bytes, &length);
	return g_file_replace_contents (file,
					(const gchar *) data,
					length,
					NULL,
					FALSE,
					G_FILE_CREATE_NONE,
					NULL,
					cancellable,
					error);
}

/**
 * dfu_firmware_to_string:
 * @firmware: a #DfuFirmware
 *
 * Returns a string representaiton of the object.
 *
 * Return value: NULL terminated string, or %NULL for invalid
 *
 * Since: 0.5.4
 **/
gchar *
dfu_firmware_to_string (DfuFirmware *firmware)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	DfuImage *image;
	GList *l;
	GString *str;
	guint i;
	g_autofree gchar *release_str = NULL;
	g_autoptr(GList) keys = NULL;

	g_return_val_if_fail (DFU_IS_FIRMWARE (firmware), NULL);

	release_str = as_utils_version_from_uint16 (priv->release,
						    AS_VERSION_PARSE_FLAG_NONE);
	str = g_string_new ("");
	g_string_append_printf (str, "vid:         0x%04x\n", priv->vid);
	g_string_append_printf (str, "pid:         0x%04x\n", priv->pid);
	g_string_append_printf (str, "release:     0x%04x [%s]\n",
				priv->release, release_str);
	g_string_append_printf (str, "format:      %s [0x%04x]\n",
				dfu_firmware_format_to_string (priv->format),
				priv->format);
	g_string_append_printf (str, "cipher:      %s\n",
				dfu_cipher_kind_to_string (priv->cipher_kind));

	/* print metadata */
	keys = g_hash_table_get_keys (priv->metadata);
	for (l = keys; l != NULL; l = l->next) {
		const gchar *key;
		const gchar *value;
		key = l->data;
		value = g_hash_table_lookup (priv->metadata, key);
		g_string_append_printf (str, "metadata:    %s=%s\n", key, value);
	}

	/* print images */
	for (i = 0; i < priv->images->len; i++) {
		g_autofree gchar *tmp = NULL;
		image = g_ptr_array_index (priv->images, i);
		tmp = dfu_image_to_string (image);
		g_string_append_printf (str, "= IMAGE %u =\n", i);
		g_string_append_printf (str, "%s\n", tmp);
	}

	g_string_truncate (str, str->len - 1);
	return g_string_free (str, FALSE);
}

/**
 * dfu_firmware_format_to_string:
 * @format: a #DfuFirmwareFormat, e.g. %DFU_FIRMWARE_FORMAT_DFU
 *
 * Returns a string representaiton of the format.
 *
 * Return value: NULL terminated string, or %NULL for invalid
 *
 * Since: 0.5.4
 **/
const gchar *
dfu_firmware_format_to_string (DfuFirmwareFormat format)
{
	if (format == DFU_FIRMWARE_FORMAT_RAW)
		return "raw";
	if (format == DFU_FIRMWARE_FORMAT_DFU)
		return "dfu";
	if (format == DFU_FIRMWARE_FORMAT_DFUSE)
		return "dfuse";
	if (format == DFU_FIRMWARE_FORMAT_INTEL_HEX)
		return "ihex";
	return NULL;
}

/**
 * dfu_firmware_format_from_string:
 * @format: a format string, e.g. `dfuse`
 *
 * Returns an enumerated version of the format.
 *
 * Return value: a #DfuFirmwareFormat, e.g. %DFU_FIRMWARE_FORMAT_DFUSE
 *
 * Since: 0.7.3
 **/
DfuFirmwareFormat
dfu_firmware_format_from_string (const gchar *format)
{
	if (g_strcmp0 (format, "raw") == 0)
		return DFU_FIRMWARE_FORMAT_RAW;
	if (g_strcmp0 (format, "dfu") == 0)
		return DFU_FIRMWARE_FORMAT_DFU;
	if (g_strcmp0 (format, "dfuse") == 0)
		return DFU_FIRMWARE_FORMAT_DFUSE;
	if (g_strcmp0 (format, "ihex") == 0)
		return DFU_FIRMWARE_FORMAT_INTEL_HEX;
	return DFU_FIRMWARE_FORMAT_UNKNOWN;
}

/**
 * dfu_firmware_get_cipher_kind:
 * @firmware: a #DfuFirmware
 *
 * Returns the kind of cipher used by the firmware file.
 *
 * NOTE: this value is based on a heuristic, and may not be accurate.
 * The value %DFU_CIPHER_KIND_NONE will be returned when the cipher
 * is not recognised.
 *
 * Return value: NULL terminated string, or %NULL for invalid
 *
 * Since: 0.5.4
 **/
DfuCipherKind
dfu_firmware_get_cipher_kind (DfuFirmware *firmware)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_return_val_if_fail (DFU_IS_FIRMWARE (firmware), 0);
	return priv->cipher_kind;
}

/**
 * dfu_firmware_set_cipher_kind:
 * @firmware: a #DfuFirmware
 * @cipher_kind: a #DfuCipherKind, e.g. %DFU_CIPHER_KIND_XTEA
 *
 * Sets the kind of cipher used by the firmware file.
 *
 * Since: 0.6.3
 **/
void
dfu_firmware_set_cipher_kind (DfuFirmware *firmware, DfuCipherKind cipher_kind)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_return_if_fail (DFU_IS_FIRMWARE (firmware));
	priv->cipher_kind = cipher_kind;
}

/**
 * dfu_firmware_add_symbol:
 * @firmware: a #DfuFirmware
 * @symbol_name: a valid symbol name
 * @symbol_addr: a symbol memory address
 *
 * Adds a symbol to the global map for the firmware.
 *
 * NOTE: Only certain types of firmware can contain a symbol table.
 *
 * Since: 0.7.4
 **/
void
dfu_firmware_add_symbol (DfuFirmware *firmware,
			 const gchar *symbol_name,
			 guint32 symbol_addr)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_hash_table_insert (priv->symtab,
			     g_strdup (symbol_name),
			     GUINT_TO_POINTER (symbol_addr));
}

/**
 * dfu_firmware_lookup_symbol:
 * @firmware: a #DfuFirmware
 * @symbol_name: a valid symbol name
 *
 * Returns the address of a symbol from the global map.
 *
 * NOTE: Only certain types of firmware can contain a symbol table.
 *
 * Return value: address of the symbol, or 0x0 for not found.
 *
 * Since: 0.7.4
 **/
guint32
dfu_firmware_lookup_symbol (DfuFirmware *firmware, const gchar *symbol_name)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	return GPOINTER_TO_UINT (g_hash_table_lookup (priv->symtab,
						      symbol_name));
}

/**
 * dfu_firmware_get_symbols:
 * @firmware: a #DfuFirmware
 *
 * Gets all the symbols currently defined.
 *
 * Return value: (element-type utf8) (transfer container): symbol names
 *
 * Since: 0.7.4
 **/
GPtrArray *
dfu_firmware_get_symbols (DfuFirmware *firmware)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	GPtrArray *array = g_ptr_array_new_with_free_func (g_free);
	GList *l;
	g_autoptr(GList) keys = g_hash_table_get_keys (priv->symtab);
	for (l = keys; l != NULL; l = l->next)
		g_ptr_array_add (array, g_strdup (l->data));
	return array;
}
