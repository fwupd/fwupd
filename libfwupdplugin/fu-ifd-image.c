/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-common.h"

#include "fu-ifd-image.h"

typedef struct {
	FuIfdAccess		 access[FU_IFD_REGION_MAX];
} FuIfdImagePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuIfdImage, fu_ifd_image, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_ifd_image_get_instance_private (o))

static void
fu_ifd_image_to_string (FuFirmware *image, guint idt, GString *str)
{
	FuIfdImage *self = FU_IFD_IMAGE (image);
	FuIfdImagePrivate *priv = GET_PRIVATE (self);
	for (guint i = 0; i < FU_IFD_REGION_MAX; i++) {
		g_autofree gchar *title = NULL;
		if (priv->access[i] == FU_IFD_ACCESS_NONE)
			continue;
		title = g_strdup_printf ("Access[%s]", fu_ifd_region_to_string (i));
		fu_common_string_append_kv (str, idt, title,
					    fu_ifd_access_to_string (priv->access[i]));
	}
}

/**
 * fu_ifd_image_set_access:
 * @self: a #FuIfdImage
 * @region: a #FuIfdRegion, e.g. %FU_IFD_REGION_BIOS
 * @access: a #FuIfdAccess, e.g. %FU_IFD_ACCESS_NONE
 *
 * Sets the access control for a specific reason.
 *
 * Since: 1.6.0
 **/
void
fu_ifd_image_set_access (FuIfdImage *self, FuIfdRegion region, FuIfdAccess access)
{
	FuIfdImagePrivate *priv = GET_PRIVATE (self);
	priv->access[region] = access;
}

/**
 * fu_ifd_image_get_access:
 * @self: a #FuIfdImage
 * @region: a #FuIfdRegion, e.g. %FU_IFD_REGION_BIOS
 *
 * Gets the access control for a specific reason.
 *
 * Return value: a #FuIfdAccess, e.g. %FU_IFD_ACCESS_NONE
 *
 * Since: 1.6.0
 **/
FuIfdAccess
fu_ifd_image_get_access (FuIfdImage *self, FuIfdRegion region)
{
	FuIfdImagePrivate *priv = GET_PRIVATE (self);
	return priv->access[region];
}

static void
fu_ifd_image_init (FuIfdImage *self)
{
}

static void
fu_ifd_image_class_init (FuIfdImageClass *klass)
{
	FuFirmwareClass *klass_image = FU_FIRMWARE_CLASS (klass);
	klass_image->to_string = fu_ifd_image_to_string;
}

/**
 * fu_ifd_image_new:
 *
 * Creates a new #FuFirmware
 *
 * Since: 1.6.0
 **/
FuFirmware *
fu_ifd_image_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_IFD_IMAGE, NULL));
}
