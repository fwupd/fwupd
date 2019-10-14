/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

/**
 * SECTION:dfu-image
 * @short_description: Object representing a a firmware image
 *
 * A #DfuImage is typically made up of several #DfuElements, although
 * typically there will only be one.
 *
 * See also: #DfuElement
 */

#include "config.h"

#include <string.h>
#include <stdio.h>

#include "fu-common.h"

#include "dfu-common.h"
#include "dfu-element.h"
#include "dfu-image.h"

static void dfu_image_finalize			 (GObject *object);

typedef struct {
	GPtrArray		*elements;
	gchar			 name[255];
} DfuImagePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (DfuImage, dfu_image, FU_TYPE_FIRMWARE_IMAGE)
#define GET_PRIVATE(o) (dfu_image_get_instance_private (o))

static void
dfu_image_init (DfuImage *image)
{
	DfuImagePrivate *priv = GET_PRIVATE (image);
	priv->elements = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	memset (priv->name, 0x00, 255);
}

static void
dfu_image_finalize (GObject *object)
{
	DfuImage *image = DFU_IMAGE (object);
	DfuImagePrivate *priv = GET_PRIVATE (image);

	g_ptr_array_unref (priv->elements);

	G_OBJECT_CLASS (dfu_image_parent_class)->finalize (object);
}

/**
 * dfu_image_new:
 *
 * Creates a new DFU image object.
 *
 * Return value: a new #DfuImage
 **/
DfuImage *
dfu_image_new (void)
{
	DfuImage *image;
	image = g_object_new (DFU_TYPE_IMAGE, NULL);
	return image;
}

/**
 * dfu_image_get_elements:
 * @image: a #DfuImage
 *
 * Gets the element data.
 *
 * Return value: (transfer none) (element-type DfuElement): element data
 **/
GPtrArray *
dfu_image_get_elements (DfuImage *image)
{
	DfuImagePrivate *priv = GET_PRIVATE (image);
	g_return_val_if_fail (DFU_IS_IMAGE (image), NULL);
	return priv->elements;
}

/**
 * dfu_image_get_element:
 * @image: a #DfuImage
 * @idx: an array index
 *
 * Gets the element.
 *
 * Return value: (transfer none): element data, or %NULL for invalid
 **/
DfuElement *
dfu_image_get_element (DfuImage *image, guint8 idx)
{
	DfuImagePrivate *priv = GET_PRIVATE (image);
	g_return_val_if_fail (DFU_IS_IMAGE (image), NULL);
	if (idx >= priv->elements->len)
		return NULL;
	return g_ptr_array_index (priv->elements, idx);
}

/**
 * dfu_image_get_element_default:
 * @image: a #DfuImage
 *
 * Gets the default element.
 *
 * Return value: (transfer none): element data, or %NULL for invalid
 **/
DfuElement *
dfu_image_get_element_default (DfuImage *image)
{
	DfuImagePrivate *priv = GET_PRIVATE (image);
	g_return_val_if_fail (DFU_IS_IMAGE (image), NULL);
	if (priv->elements->len == 0)
		return NULL;
	return g_ptr_array_index (priv->elements, 0);
}

/**
 * dfu_image_get_alt_setting:
 * @image: a #DfuImage
 *
 * Gets the alternate setting.
 *
 * Return value: integer, or 0x00 for unset
 **/
guint8
dfu_image_get_alt_setting (DfuImage *image)
{
	g_return_val_if_fail (DFU_IS_IMAGE (image), 0xff);
	return fu_firmware_image_get_idx (FU_FIRMWARE_IMAGE (image));
}

/**
 * dfu_image_get_name:
 * @image: a #DfuImage
 *
 * Gets the target name.
 *
 * Return value: a string, or %NULL for unset
 **/
const gchar *
dfu_image_get_name (DfuImage *image)
{
	DfuImagePrivate *priv = GET_PRIVATE (image);
	g_return_val_if_fail (DFU_IS_IMAGE (image), NULL);
	return priv->name;
}

/**
 * dfu_image_get_size:
 * @image: a #DfuImage
 *
 * Gets the size of all the elements in the image.
 *
 * This only returns actual data that would be sent to the device and
 * does not include any padding.
 *
 * Return value: a integer value, or 0 if there are no elements.
 **/
guint32
dfu_image_get_size (DfuImage *image)
{
	DfuImagePrivate *priv = GET_PRIVATE (image);
	guint32 length = 0;
	g_return_val_if_fail (DFU_IS_IMAGE (image), 0);
	for (guint i = 0; i < priv->elements->len; i++) {
		DfuElement *element = g_ptr_array_index (priv->elements, i);
		length += (guint32) g_bytes_get_size (dfu_element_get_contents (element));
	}
	return length;
}

/**
 * dfu_image_add_element:
 * @image: a #DfuImage
 * @element: a #DfuElement
 *
 * Adds an element to the image.
 **/
void
dfu_image_add_element (DfuImage *image, DfuElement *element)
{
	DfuImagePrivate *priv = GET_PRIVATE (image);
	g_return_if_fail (DFU_IS_IMAGE (image));
	g_return_if_fail (DFU_IS_ELEMENT (element));
	g_ptr_array_add (priv->elements, g_object_ref (element));
}

/**
 * dfu_image_set_alt_setting:
 * @image: a #DfuImage
 * @alt_setting: vendor ID, or 0xffff for unset
 *
 * Sets the vendor ID.
 **/
void
dfu_image_set_alt_setting (DfuImage *image, guint8 alt_setting)
{
	fu_firmware_image_set_idx (FU_FIRMWARE_IMAGE (image), alt_setting);
}

/**
 * dfu_image_set_name:
 * @image: a #DfuImage
 * @name: a target string, or %NULL
 *
 * Sets the target name.
 **/
void
dfu_image_set_name (DfuImage *image, const gchar *name)
{
	guint16 sz;
	DfuImagePrivate *priv = GET_PRIVATE (image);
	g_return_if_fail (DFU_IS_IMAGE (image));

	/* this is a hard limit in DfuSe */
	memset (priv->name, 0x00, 0xff);
	if (name != NULL) {
		sz = MIN ((guint16) strlen (name), 0xff - 1);
		memcpy (priv->name, name, sz);
	}

	/* copy junk data in self tests for 1:1 copies */
	if (name != NULL && G_UNLIKELY (g_getenv ("DFU_SELF_TEST_IMAGE_MEMCPY_NAME") != NULL))
		memcpy (priv->name, name, 0xff);
}

static void
dfu_image_to_string (FuFirmwareImage *self, guint idt, GString *str)
{
	DfuImage *image = DFU_IMAGE (self);
	DfuImagePrivate *priv = GET_PRIVATE (image);
	if (priv->name[0] != '\0')
		fu_common_string_append_kv (str, idt, "Name", priv->name);
	fu_common_string_append_ku (str, idt, "Elements", priv->elements->len);

	/* add elements */
	for (guint i = 0; i < priv->elements->len; i++) {
		DfuElement *element = g_ptr_array_index (priv->elements, i);
		g_autofree gchar *tmp = NULL;
		tmp = dfu_element_to_string (element);
		g_string_append_printf (str, "== ELEMENT %u ==\n", i);
		g_string_append_printf (str, "%s\n", tmp);
	}
}

static void
dfu_image_class_init (DfuImageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuFirmwareImageClass *firmware_image_class = FU_FIRMWARE_IMAGE_CLASS (klass);
	object_class->finalize = dfu_image_finalize;
	firmware_image_class->to_string = dfu_image_to_string;
}
