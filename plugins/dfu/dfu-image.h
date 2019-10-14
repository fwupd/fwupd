/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>
#include <gio/gio.h>

#include "fu-firmware-image.h"

#include "dfu-element.h"

#define DFU_TYPE_IMAGE (dfu_image_get_type ())
G_DECLARE_DERIVABLE_TYPE (DfuImage, dfu_image, DFU, IMAGE, FuFirmwareImage)

struct _DfuImageClass
{
	FuFirmwareImageClass		 parent_class;
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
