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

#ifndef __DFU_IMAGE_H
#define __DFU_IMAGE_H

#include <glib-object.h>
#include <gio/gio.h>

#include "dfu-element.h"

G_BEGIN_DECLS

#define DFU_TYPE_IMAGE (dfu_image_get_type ())
G_DECLARE_DERIVABLE_TYPE (DfuImage, dfu_image, DFU, IMAGE, GObject)

struct _DfuImageClass
{
	GObjectClass		 parent_class;
	/*< private >*/
	/* Padding for future expansion */
	void (*_dfu_image_reserved1) (void);
	void (*_dfu_image_reserved2) (void);
	void (*_dfu_image_reserved3) (void);
	void (*_dfu_image_reserved4) (void);
	void (*_dfu_image_reserved5) (void);
	void (*_dfu_image_reserved6) (void);
	void (*_dfu_image_reserved7) (void);
	void (*_dfu_image_reserved8) (void);
	void (*_dfu_image_reserved9) (void);
};

DfuImage	*dfu_image_new		(void);

GPtrArray	*dfu_image_get_elements		(DfuImage	*image);
DfuElement	*dfu_image_get_element		(DfuImage	*image,
						 guint8		 idx);
DfuElement	*dfu_image_get_element_default	(DfuImage	*image);
guint8		 dfu_image_get_alt_setting	(DfuImage	*image);
const gchar	*dfu_image_get_name		(DfuImage	*image);
guint32		 dfu_image_get_size		(DfuImage	*image);

void		 dfu_image_add_element		(DfuImage	*image,
						 DfuElement	*element);

void		 dfu_image_set_alt_setting	(DfuImage	*image,
						 guint8		 alt_setting);
void		 dfu_image_set_name		(DfuImage	*image,
						 const gchar	*name);

gchar		*dfu_image_to_string		(DfuImage	*image);

G_END_DECLS

#endif /* __DFU_IMAGE_H */
