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
fu_ifd_image_export (FuFirmware *firmware,
		     FuFirmwareExportFlags flags,
		     XbBuilderNode *bn)
{
	FuIfdImage *self = FU_IFD_IMAGE (firmware);
	FuIfdImagePrivate *priv = GET_PRIVATE (self);
	for (guint i = 0; i < FU_IFD_REGION_MAX; i++) {
		if (priv->access[i] == FU_IFD_ACCESS_NONE)
			continue;
		xb_builder_node_insert_text (bn, "access",
					     fu_ifd_access_to_string (priv->access[i]),
					     "region", fu_ifd_region_to_string (i),
					     NULL);
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

static GBytes *
fu_ifd_image_write (FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new ();
	g_autoptr(GPtrArray) images = fu_firmware_get_images (firmware);

	/* add each volume */
	if (images->len > 0) {
		for (guint i = 0; i < images->len; i++) {
			FuFirmware *img = g_ptr_array_index (images, i);
			g_autoptr(GBytes) bytes = fu_firmware_write (img, error);
			if (bytes == NULL)
				return NULL;
			fu_byte_array_append_bytes (buf, bytes);
		}
	} else {
		g_autoptr(GBytes) bytes = NULL;
		bytes = fu_firmware_get_bytes (firmware, error);
		if (bytes == NULL)
			return NULL;
		fu_byte_array_append_bytes (buf, bytes);
	}

	/* align up */
	fu_byte_array_set_size (buf, fu_common_align_up (buf->len,
				fu_firmware_get_alignment (firmware)));

	/* success */
	return g_byte_array_free_to_bytes (g_steal_pointer (&buf));

}

static void
fu_ifd_image_init (FuIfdImage *self)
{
	fu_firmware_set_alignment (FU_FIRMWARE (self), FU_FIRMWARE_ALIGNMENT_4K);
}

static void
fu_ifd_image_class_init (FuIfdImageClass *klass)
{
	FuFirmwareClass *klass_image = FU_FIRMWARE_CLASS (klass);
	klass_image->export = fu_ifd_image_export;
	klass_image->write = fu_ifd_image_write;
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
