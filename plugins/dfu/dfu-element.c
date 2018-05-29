/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

/**
 * SECTION:dfu-element
 * @short_description: Object representing a binary element
 *
 * This object represents an binary blob of data at a specific address.
 *
 * This allows relocatable data segments to be stored in different
 * locations on the device itself.
 *
 * See also: #DfuImage, #DfuFirmware
 */

#include "config.h"

#include <string.h>
#include <stdio.h>

#include "dfu-common.h"
#include "dfu-element.h"

#include "fwupd-error.h"

static void dfu_element_finalize			 (GObject *object);

typedef struct {
	GBytes			*contents;
	guint32			 target_size;
	guint32			 address;
	guint8			 padding_value;
} DfuElementPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (DfuElement, dfu_element, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (dfu_element_get_instance_private (o))

static void
dfu_element_class_init (DfuElementClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = dfu_element_finalize;
}

static void
dfu_element_init (DfuElement *element)
{
}

static void
dfu_element_finalize (GObject *object)
{
	DfuElement *element = DFU_ELEMENT (object);
	DfuElementPrivate *priv = GET_PRIVATE (element);

	if (priv->contents != NULL)
		g_bytes_unref (priv->contents);

	G_OBJECT_CLASS (dfu_element_parent_class)->finalize (object);
}

/**
 * dfu_element_new:
 *
 * Creates a new DFU element object.
 *
 * Return value: a new #DfuElement
 **/
DfuElement *
dfu_element_new (void)
{
	DfuElement *element;
	element = g_object_new (DFU_TYPE_ELEMENT, NULL);
	return element;
}

/**
 * dfu_element_get_contents:
 * @element: a #DfuElement
 *
 * Gets the element data.
 *
 * Return value: (transfer none): element data
 **/
GBytes *
dfu_element_get_contents (DfuElement *element)
{
	DfuElementPrivate *priv = GET_PRIVATE (element);
	g_return_val_if_fail (DFU_IS_ELEMENT (element), NULL);
	return priv->contents;
}

/**
 * dfu_element_get_address:
 * @element: a #DfuElement
 *
 * Gets the offset address of the element.
 *
 * Return value: memory offset value, or 0x00 for unset
 **/
guint32
dfu_element_get_address (DfuElement *element)
{
	DfuElementPrivate *priv = GET_PRIVATE (element);
	g_return_val_if_fail (DFU_IS_ELEMENT (element), 0x00);
	return priv->address;
}

/**
 * dfu_element_set_contents:
 * @element: a #DfuElement
 * @contents: element data
 *
 * Sets the element data.
 **/
void
dfu_element_set_contents (DfuElement *element, GBytes *contents)
{
	DfuElementPrivate *priv = GET_PRIVATE (element);
	g_return_if_fail (DFU_IS_ELEMENT (element));
	g_return_if_fail (contents != NULL);
	if (priv->contents == contents)
		return;
	if (priv->contents != NULL)
		g_bytes_unref (priv->contents);
	priv->contents = g_bytes_ref (contents);
}

/**
 * dfu_element_set_address:
 * @element: a #DfuElement
 * @address: memory offset value
 *
 * Sets the offset address of the element.
 **/
void
dfu_element_set_address (DfuElement *element, guint32 address)
{
	DfuElementPrivate *priv = GET_PRIVATE (element);
	g_return_if_fail (DFU_IS_ELEMENT (element));
	priv->address = address;
}

/**
 * dfu_element_to_string:
 * @element: a #DfuElement
 *
 * Returns a string representaiton of the object.
 *
 * Return value: NULL terminated string, or %NULL for invalid
 **/
gchar *
dfu_element_to_string (DfuElement *element)
{
	DfuElementPrivate *priv = GET_PRIVATE (element);
	GString *str;

	g_return_val_if_fail (DFU_IS_ELEMENT (element), NULL);

	str = g_string_new ("");
	g_string_append_printf (str, "address:     0x%02x\n", priv->address);
	if (priv->target_size > 0) {
		g_string_append_printf (str, "target:      0x%04x\n",
					priv->target_size);
	}
	if (priv->contents != NULL) {
		g_string_append_printf (str, "contents:    0x%04x\n",
					(guint32) g_bytes_get_size (priv->contents));
	}

	g_string_truncate (str, str->len - 1);
	return g_string_free (str, FALSE);
}

/**
 * dfu_element_set_padding_value:
 * @element: a #DfuElement
 * @padding_value: char value, typically 0x00 or 0xff
 *
 * Sets a the value of the padding byte to be used in the function
 * dfu_element_set_target_size().
 **/
void
dfu_element_set_padding_value (DfuElement *element, guint8 padding_value)
{
	DfuElementPrivate *priv = GET_PRIVATE (element);
	g_return_if_fail (DFU_IS_ELEMENT (element));
	priv->padding_value = padding_value;
}

/**
 * dfu_element_set_target_size:
 * @element: a #DfuElement
 * @target_size: size in bytes
 *
 * Sets a target size for the element. If the prepared element is smaller
 * than this then it will be padded up to the required size.
 *
 * If a padding byte other than 0x00 is required then the function
 * dfu_element_set_padding_value() should be used before this function is
 * called.
 **/
void
dfu_element_set_target_size (DfuElement *element, guint32 target_size)
{
	DfuElementPrivate *priv = GET_PRIVATE (element);
	const guint8 *data;
	gsize length;
	guint8 *buf;

	g_return_if_fail (DFU_IS_ELEMENT (element));

	/* save for dump */
	priv->target_size = target_size;

	/* no need to pad */
	if (priv->contents == NULL)
		return;
	if (g_bytes_get_size (priv->contents) >= target_size)
		return;

	/* reallocate and pad */
	data = g_bytes_get_data (priv->contents, &length);
	buf = g_malloc0 (target_size);
	g_assert (buf != NULL);
	memcpy (buf, data, length);

	/* set the pading value */
	if (priv->padding_value != 0x00) {
		memset (buf + length,
			priv->padding_value,
			target_size - length);
	}

	/* replace */
	g_bytes_unref (priv->contents);
	priv->contents = g_bytes_new_take (buf, target_size);
}

/**
 * dfu_element_get_contents_chunk:
 * @element: a #DfuElement
 * @address: an address greater than dfu_element_get_address()
 * @chunk_sz_max: the size of the new chunk
 * @error: a #GError, or %NULL
 *
 * Gets a block of data from the @element. If the contents of the element is
 * smaller than the requested chunk size then the #GBytes will be smaller
 * than @chunk_sz_max. Use dfu_utils_bytes_pad() if padding is required.
 *
 * If the @address is larger than the size of the @element then an error is returned.
 *
 * Return value: (transfer full): a #GBytes, or %NULL
 **/
GBytes *
dfu_element_get_contents_chunk (DfuElement *element,
				guint32 address,
				guint32 chunk_sz_max,
				GError **error)
{
	GBytes *blob;
	gsize chunk_left;
	guint32 offset;

	/* check address requested is larger than base address */
	if (address < dfu_element_get_address (element)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "requested address 0x%x less than base address 0x%x",
			     (guint) address, (guint) dfu_element_get_address (element));
		return NULL;
	}

	/* offset into data */
	offset = address - dfu_element_get_address (element);
	blob = dfu_element_get_contents (element);
	if (offset > g_bytes_get_size (blob)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "offset 0x%x larger than data size 0x%x",
			     (guint) offset,
			     (guint) g_bytes_get_size (blob));
		return NULL;
	}

	/* if we have less data than requested */
	chunk_left = g_bytes_get_size (blob) - offset;
	if (chunk_sz_max > chunk_left)
		return g_bytes_new_from_bytes (blob, offset, chunk_left);

	/* check chunk */
	return g_bytes_new_from_bytes (blob, offset, chunk_sz_max);
}
