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
#include "dfu-element-private.h"

static void dfu_element_finalize			 (GObject *object);

/**
 * DfuElementPrivate:
 *
 * Private #DfuElement data
 **/
typedef struct {
	GBytes			*contents;
	guint32			 target_size;
	guint32			 address;
} DfuElementPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (DfuElement, dfu_element, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (dfu_element_get_instance_private (o))

/**
 * dfu_element_class_init:
 **/
static void
dfu_element_class_init (DfuElementClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = dfu_element_finalize;
}

/**
 * dfu_element_init:
 **/
static void
dfu_element_init (DfuElement *element)
{
}

/**
 * dfu_element_finalize:
 **/
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
 *
 * Since: 0.5.4
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
 *
 * Since: 0.5.4
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
 * Gets the alternate setting.
 *
 * Return value: integer, or 0x00 for unset
 *
 * Since: 0.5.4
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
 *
 * Since: 0.5.4
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
 * @address: vendor ID, or 0xffff for unset
 *
 * Sets the vendor ID.
 *
 * Since: 0.5.4
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
 *
 * Since: 0.5.4
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
 * dfu_element_set_target_size:
 * @element: a #DfuElement
 * @target_size: size in bytes
 *
 * Sets a target size for the element. If the prepared element is smaller
 * than this then it will be padded with NUL bytes up to the required size.
 *
 * Since: 0.5.4
 **/
void
dfu_element_set_target_size (DfuElement *element, guint32 target_size)
{
	DfuElementPrivate *priv = GET_PRIVATE (element);
	const guint8 *data;
	gsize length;
	guint8 *buf;
	g_autoptr(GBytes) contents_padded = NULL;

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

	/* replace */
	g_bytes_unref (priv->contents);
	priv->contents = g_bytes_new_take (buf, target_size);
}

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
DfuElement *
dfu_element_from_dfuse (const guint8 *data,
			gsize length,
			guint32 *consumed,
			GError **error)
{
	DfuElement *element = NULL;
	DfuElementPrivate *priv;
	DfuSeElementPrefix *el = (DfuSeElementPrefix *) data;

	g_assert_cmpint(sizeof(DfuSeElementPrefix), ==, 8);

	/* create new element */
	element = dfu_element_new ();
	priv = GET_PRIVATE (element);
	priv->address = GUINT32_FROM_LE (el->address);
	priv->contents = g_bytes_new (data + sizeof(DfuSeElementPrefix),
				      GUINT32_FROM_LE (el->size));

	/* return size */
	if (consumed != NULL)
		*consumed = sizeof(DfuSeElementPrefix) + GUINT32_FROM_LE (el->size);

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
GBytes *
dfu_element_to_dfuse (DfuElement *element)
{
	DfuElementPrivate *priv = GET_PRIVATE (element);
	DfuSeElementPrefix *el;
	const guint8 *data;
	gsize length;
	guint8 *buf;

	data = g_bytes_get_data (priv->contents, &length);
	buf = g_malloc0 (length + sizeof (DfuSeElementPrefix));
	el = (DfuSeElementPrefix *) buf;
	el->address = GUINT32_TO_LE (priv->address);
	el->size = GUINT32_TO_LE (length);

	memcpy (buf + sizeof (DfuSeElementPrefix), data, length);
	return g_bytes_new_take (buf, length + sizeof (DfuSeElementPrefix));
}
