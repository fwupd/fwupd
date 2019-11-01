/*
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-common.h"

#include "dfu-element.h"
#include "dfu-format-dfuse.h"
#include "dfu-image.h"

#include "fwupd-error.h"

/* DfuSe element header */
typedef struct __attribute__((packed)) {
	guint32		 address;
	guint32		 size;
} DfuSeElementPrefix;

/**
 * dfu_element_from_dfuse: (skip)
 * @data: data buffer
 * @length: length of @data we can access
 * @consumed: (out): the number of bytes we consued
 * @error: a #GError, or %NULL
 *
 * Unpacks an element from DfuSe data.
 *
 * Returns: a #DfuElement, or %NULL for error
 **/
static DfuElement *
dfu_element_from_dfuse (const guint8 *data,
			guint32 length,
			guint32 *consumed,
			GError **error)
{
	DfuElement *element = NULL;
	DfuSeElementPrefix *el = (DfuSeElementPrefix *) data;
	guint32 size;
	g_autoptr(GBytes) contents = NULL;

	g_assert_cmpint(sizeof(DfuSeElementPrefix), ==, 8);

	/* check input buffer size */
	if (length < sizeof(DfuSeElementPrefix)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "invalid element data size %u",
			     (guint32) length);
		return NULL;
	}

	/* check size */
	size = GUINT32_FROM_LE (el->size);
	if (size + sizeof(DfuSeElementPrefix) > length) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "invalid element size %u, only %u bytes left",
			     size,
			     (guint32) (length - sizeof(DfuSeElementPrefix)));
		return NULL;
	}

	/* create new element */
	element = dfu_element_new ();
	dfu_element_set_address (element, GUINT32_FROM_LE (el->address));
	contents = g_bytes_new (data + sizeof(DfuSeElementPrefix), size);
	dfu_element_set_contents (element, contents);

	/* return size */
	if (consumed != NULL)
		*consumed = (guint32) sizeof(DfuSeElementPrefix) + size;

	return element;
}

/**
 * dfu_element_to_dfuse: (skip)
 * @element: a #DfuElement
 *
 * Packs a DfuSe element.
 *
 * Returns: (transfer full): the packed data
 **/
static GBytes *
dfu_element_to_dfuse (DfuElement *element)
{
	DfuSeElementPrefix *el;
	const guint8 *data;
	gsize length;
	guint8 *buf;

	data = g_bytes_get_data (dfu_element_get_contents (element), &length);
	buf = g_malloc0 (length + sizeof (DfuSeElementPrefix));
	el = (DfuSeElementPrefix *) buf;
	el->address = GUINT32_TO_LE (dfu_element_get_address (element));
	el->size = GUINT32_TO_LE (length);

	memcpy (buf + sizeof (DfuSeElementPrefix), data, length);
	return g_bytes_new_take (buf, length + sizeof (DfuSeElementPrefix));
}

/* DfuSe image header */
typedef struct __attribute__((packed)) {
	guint8		 sig[6];
	guint8		 alt_setting;
	guint32		 target_named;
	gchar		 target_name[255];
	guint32		 target_size;
	guint32		 elements;
} DfuSeImagePrefix;

/**
 * dfu_image_from_dfuse: (skip)
 * @data: data buffer
 * @length: length of @data we can access
 * @consumed: (out): the number of bytes we consued
 * @error: a #GError, or %NULL
 *
 * Unpacks an image from DfuSe data.
 *
 * Returns: a #DfuImage, or %NULL for error
 **/
static DfuImage *
dfu_image_from_dfuse (const guint8 *data,
		      guint32 length,
		      guint32 *consumed,
		      GError **error)
{
	DfuSeImagePrefix *im;
	guint32 elements;
	guint32 offset = sizeof(DfuSeImagePrefix);
	g_autoptr(DfuImage) image = NULL;

	g_assert_cmpint(sizeof(DfuSeImagePrefix), ==, 274);

	/* check input buffer size */
	if (length < sizeof(DfuSeImagePrefix)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "invalid image data size %u",
			     (guint32) length);
		return NULL;
	}

	/* verify image signature */
	im = (DfuSeImagePrefix *) data;
	if (memcmp (im->sig, "Target", 6) != 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "invalid DfuSe target signature");
		return NULL;
	}

	/* create new image */
	image = dfu_image_new ();
	dfu_image_set_alt_setting (image, im->alt_setting);
	if (GUINT32_FROM_LE (im->target_named) == 0x01)
		dfu_image_set_name (image, im->target_name);

	/* parse elements */
	length -= offset;
	elements = GUINT32_FROM_LE (im->elements);
	for (guint j = 0; j < elements; j++) {
		guint32 consumed_local;
		g_autoptr(DfuElement) element = NULL;
		element = dfu_element_from_dfuse (data + offset, length,
						  &consumed_local, error);
		if (element == NULL)
			return NULL;
		dfu_image_add_element (image, element);
		offset += consumed_local;
		length -= consumed_local;
	}

	/* return size */
	if (consumed != NULL)
		*consumed = offset;

	return g_object_ref (image);
}

/**
 * dfu_image_to_dfuse: (skip)
 * @image: a #DfuImage
 *
 * Packs a DfuSe image
 *
 * Returns: (transfer full): the packed data
 **/
static GBytes *
dfu_image_to_dfuse (DfuImage *image)
{
	DfuSeImagePrefix *im;
	GPtrArray *elements;
	guint32 length_total = 0;
	guint32 offset = sizeof (DfuSeImagePrefix);
	guint8 *buf;
	g_autoptr(GPtrArray) element_array = NULL;

	/* get total size */
	element_array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_bytes_unref);
	elements = dfu_image_get_elements (image);
	for (guint i = 0; i < elements->len; i++) {
		DfuElement *element = g_ptr_array_index (elements, i);
		GBytes *bytes = dfu_element_to_dfuse (element);
		g_ptr_array_add (element_array, bytes);
		length_total += (guint32) g_bytes_get_size (bytes);
	}

	/* add prefix */
	buf = g_malloc0 (length_total + sizeof (DfuSeImagePrefix));
	im = (DfuSeImagePrefix *) buf;
	memcpy (im->sig, "Target", 6);
	im->alt_setting = dfu_image_get_alt_setting (image);
	if (dfu_image_get_name (image) != NULL) {
		im->target_named = GUINT32_TO_LE (0x01);
		memcpy (im->target_name, dfu_image_get_name (image), 255);
	}
	im->target_size = GUINT32_TO_LE (length_total);
	im->elements = GUINT32_TO_LE (elements->len);

	/* copy data */
	for (guint i = 0; i < element_array->len; i++) {
		gsize length;
		GBytes *bytes = g_ptr_array_index (element_array, i);
		const guint8 *data = g_bytes_get_data (bytes, &length);
		g_autoptr(GError) error = NULL;
		if (!fu_memcpy_safe (buf, length_total + sizeof (DfuSeImagePrefix), offset,	/* dst */
				     data, length, 0x0,						/* src */
				     length, &error)) {
			g_critical ("failed to pack buffer: %s", error->message);
			continue;
		}
		offset += (guint32) length;
	}
	return g_bytes_new_take (buf, length_total + sizeof (DfuSeImagePrefix));
}

/* DfuSe header */
typedef struct __attribute__((packed)) {
	guint8		 sig[5];
	guint8		 ver;
	guint32		 image_size;
	guint8		 targets;
} DfuSePrefix;

/**
 * dfu_firmware_to_dfuse: (skip)
 * @firmware: a #DfuFirmware
 * @error: a #GError, or %NULL
 *
 * Packs a DfuSe firmware
 *
 * Returns: (transfer full): the packed data
 **/
GBytes *
dfu_firmware_to_dfuse (DfuFirmware *firmware, GError **error)
{
	DfuSePrefix *prefix;
	guint32 image_size_total = 0;
	guint32 offset = sizeof (DfuSePrefix);
	g_autofree guint8 *buf = NULL;
	g_autoptr(GPtrArray) dfuse_images = NULL;
	g_autoptr(GPtrArray) images = NULL;

	/* get all the image data */
	dfuse_images = g_ptr_array_new_with_free_func ((GDestroyNotify) g_bytes_unref);
	images = fu_firmware_get_images (FU_FIRMWARE (firmware));
	for (guint i = 0; i < images->len; i++) {
		DfuImage *im = g_ptr_array_index (images, i);
		GBytes *contents;
		contents = dfu_image_to_dfuse (im);
		image_size_total += (guint32) g_bytes_get_size (contents);
		g_ptr_array_add (dfuse_images, contents);
	}
	g_debug ("image_size_total: %" G_GUINT32_FORMAT, image_size_total);

	buf = g_malloc0 (sizeof (DfuSePrefix) + image_size_total);

	/* DfuSe header */
	prefix = (DfuSePrefix *) buf;
	memcpy (prefix->sig, "DfuSe", 5);
	prefix->ver = 0x01;
	prefix->image_size = GUINT32_TO_LE (offset + image_size_total);
	if (images->len > G_MAXUINT8) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "too many (%u) images to write DfuSe file",
			     images->len);
		return NULL;
	}
	prefix->targets = (guint8) images->len;

	/* copy images */
	for (guint i = 0; i < dfuse_images->len; i++) {
		GBytes *contents = g_ptr_array_index (dfuse_images, i);
		gsize length;
		const guint8 *data;
		data = g_bytes_get_data (contents, &length);
		if (!fu_memcpy_safe (buf, sizeof (DfuSePrefix) + image_size_total, offset,	/* dst */
				     data, length, 0x0,						/* src */
				     length, error))
			return NULL;
		offset += (guint32) length;
	}

	/* return blob */
	return g_bytes_new (buf, sizeof (DfuSePrefix) + image_size_total);
}

/**
 * dfu_firmware_from_dfuse: (skip)
 * @firmware: a #DfuFirmware
 * @bytes: data to parse
 * @flags: some #FwupdInstallFlags
 * @error: a #GError, or %NULL
 *
 * Unpacks into a firmware object from DfuSe data.
 *
 * Returns: %TRUE for success
 **/
gboolean
dfu_firmware_from_dfuse (DfuFirmware *firmware,
			 GBytes *bytes,
			 FwupdInstallFlags flags,
			 GError **error)
{
	DfuSePrefix *prefix;
	gsize len;
	guint32 offset = sizeof(DfuSePrefix);
	guint8 *data;

	/* check the prefix (BE) */
	data = (guint8 *) g_bytes_get_data (bytes, &len);
	prefix = (DfuSePrefix *) data;
	if (memcmp (prefix->sig, "DfuSe", 5) != 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "invalid DfuSe prefix");
		return FALSE;
	}

	/* check the version */
	if (prefix->ver != 0x01) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "invalid DfuSe version, got %02x",
			     prefix->ver);
		return FALSE;
	}

	/* check image size */
	if (GUINT32_FROM_LE (prefix->image_size) != len) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "invalid DfuSe image size, "
			     "got %" G_GUINT32_FORMAT ", "
			     "expected %" G_GSIZE_FORMAT,
			     GUINT32_FROM_LE (prefix->image_size),
			     len);
		return FALSE;
	}

	/* parse the image targets */
	len -= sizeof(DfuSePrefix);
	for (guint i = 0; i < prefix->targets; i++) {
		guint consumed;
		g_autoptr(DfuImage) image = NULL;
		image = dfu_image_from_dfuse (data + offset, (guint32) len,
					      &consumed, error);
		if (image == NULL)
			return FALSE;
		fu_firmware_add_image (FU_FIRMWARE (firmware), FU_FIRMWARE_IMAGE (image));
		offset += consumed;
		len -= consumed;
	}
	return TRUE;
}
