/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuFirmware"

#include "config.h"

#include "fu-common.h"
#include "fu-firmware-image-private.h"

/**
 * SECTION:fu-firmware_image
 * @short_description: a firmware_image file
 *
 * An object that represents a firmware_image file.
 */

typedef struct {
	gchar			*id;
	GBytes			*bytes;
	guint64			 addr;
	guint64			 idx;
	gchar			*version;
} FuFirmwareImagePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuFirmwareImage, fu_firmware_image, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fu_firmware_image_get_instance_private (o))

/**
 * fu_firmware_image_get_version:
 * @self: A #FuFirmwareImage
 *
 * Gets an optional version that represents the firmware image.
 *
 * Returns: a string, or %NULL
 *
 * Since: 1.3.4
 **/
const gchar *
fu_firmware_image_get_version (FuFirmwareImage *self)
{
	FuFirmwareImagePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_FIRMWARE_IMAGE (self), NULL);
	return priv->version;
}

/**
 * fu_firmware_image_set_version:
 * @self: A #FuFirmwareImage
 * @version: (nullable): A string version, or %NULL
 *
 * Sets an optional version that represents the firmware image.
 *
 * Since: 1.3.4
 **/
void
fu_firmware_image_set_version (FuFirmwareImage *self, const gchar *version)
{
	FuFirmwareImagePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_FIRMWARE_IMAGE (self));
	g_free (priv->version);
	priv->version = g_strdup (version);
}

/**
 * fu_firmware_image_set_id:
 * @self: a #FuPlugin
 * @id: (nullable): image ID, e.g. "config"
 *
 * Since: 1.3.1
 **/
void
fu_firmware_image_set_id (FuFirmwareImage *self, const gchar *id)
{
	FuFirmwareImagePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_FIRMWARE_IMAGE (self));
	g_free (priv->id);
	priv->id = g_strdup (id);
}

/**
 * fu_firmware_image_get_id:
 * @self: a #FuPlugin
 *
 * Gets the image ID, typically set at construction.
 *
 * Returns: image ID, e.g. "config"
 *
 * Since: 1.3.1
 **/
const gchar *
fu_firmware_image_get_id (FuFirmwareImage *self)
{
	FuFirmwareImagePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_FIRMWARE_IMAGE (self), NULL);
	return priv->id;
}

/**
 * fu_firmware_image_set_addr:
 * @self: a #FuPlugin
 * @addr: integer
 *
 * Sets the base address of the image.
 *
 * Since: 1.3.1
 **/
void
fu_firmware_image_set_addr (FuFirmwareImage *self, guint64 addr)
{
	FuFirmwareImagePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_FIRMWARE_IMAGE (self));
	priv->addr = addr;
}

/**
 * fu_firmware_image_get_addr:
 * @self: a #FuPlugin
 *
 * Gets the base address of the image.
 *
 * Returns: integer
 *
 * Since: 1.3.1
 **/
guint64
fu_firmware_image_get_addr (FuFirmwareImage *self)
{
	FuFirmwareImagePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_FIRMWARE_IMAGE (self), G_MAXUINT64);
	return priv->addr;
}

/**
 * fu_firmware_image_set_idx:
 * @self: a #FuPlugin
 * @idx: integer
 *
 * Sets the index of the image which is used for ordering.
 *
 * Since: 1.3.1
 **/
void
fu_firmware_image_set_idx (FuFirmwareImage *self, guint64 idx)
{
	FuFirmwareImagePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_FIRMWARE_IMAGE (self));
	priv->idx = idx;
}

/**
 * fu_firmware_image_get_idx:
 * @self: a #FuPlugin
 *
 * Gets the index of the image which is used for ordering.
 *
 * Returns: integer
 *
 * Since: 1.3.1
 **/
guint64
fu_firmware_image_get_idx (FuFirmwareImage *self)
{
	FuFirmwareImagePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_FIRMWARE_IMAGE (self), G_MAXUINT64);
	return priv->idx;
}

/**
 * fu_firmware_image_set_bytes:
 * @self: a #FuPlugin
 * @bytes: A #GBytes
 *
 * Sets the contents of the image if not created with fu_firmware_image_new().
 *
 * Since: 1.3.1
 **/
void
fu_firmware_image_set_bytes (FuFirmwareImage *self, GBytes *bytes)
{
	FuFirmwareImagePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_FIRMWARE_IMAGE (self));
	g_return_if_fail (bytes != NULL);
	g_return_if_fail (priv->bytes == NULL);
	priv->bytes = g_bytes_ref (bytes);
}

/**
 * fu_firmware_image_write:
 * @self: a #FuPlugin
 * @error: A #GError, or %NULL
 *
 * Writes the image, which will try to call a superclassed ->write() function.
 *
 * By default (and in most cases) this just provides the value set by the
 * fu_firmware_image_set_bytes() function.
 *
 * Returns: (transfer full): a #GBytes of the bytes, or %NULL if the bytes is not set
 *
 * Since: 1.3.3
 **/
GBytes *
fu_firmware_image_write (FuFirmwareImage *self, GError **error)
{
	FuFirmwareImageClass *klass = FU_FIRMWARE_IMAGE_GET_CLASS (self);
	FuFirmwareImagePrivate *priv = GET_PRIVATE (self);

	g_return_val_if_fail (FU_IS_FIRMWARE_IMAGE (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* optional vfunc */
	if (klass->write != NULL)
		return klass->write (self, error);

	/* fall back to what was set manually */
	if (priv->bytes == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "no bytes found in firmware bytes %s", priv->id);
		return NULL;
	}
	return g_bytes_ref (priv->bytes);
}

/**
 * fu_firmware_image_write_chunk:
 * @self: a #FuFirmwareImage
 * @address: an address greater than dfu_element_get_address()
 * @chunk_sz_max: the size of the new chunk
 * @error: a #GError, or %NULL
 *
 * Gets a block of data from the image. If the contents of the image is
 * smaller than the requested chunk size then the #GBytes will be smaller
 * than @chunk_sz_max. Use fu_common_bytes_pad() if padding is required.
 *
 * If the @address is larger than the size of the image then an error is returned.
 *
 * Return value: (transfer full): a #GBytes, or %NULL
 *
 * Since: 1.3.1
 **/
GBytes *
fu_firmware_image_write_chunk (FuFirmwareImage *self,
			       guint64 address,
			       guint64 chunk_sz_max,
			       GError **error)
{
	FuFirmwareImagePrivate *priv = GET_PRIVATE (self);
	gsize chunk_left;
	guint64 offset;

	/* check address requested is larger than base address */
	if (address < priv->addr) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "requested address 0x%x less than base address 0x%x",
			     (guint) address, (guint) priv->addr);
		return NULL;
	}

	/* offset into data */
	offset = address - priv->addr;
	if (offset > g_bytes_get_size (priv->bytes)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "offset 0x%x larger than data size 0x%x",
			     (guint) offset,
			     (guint) g_bytes_get_size (priv->bytes));
		return NULL;
	}

	/* if we have less data than requested */
	chunk_left = g_bytes_get_size (priv->bytes) - offset;
	if (chunk_sz_max > chunk_left)
		return g_bytes_new_from_bytes (priv->bytes, offset, chunk_left);

	/* check chunk */
	return g_bytes_new_from_bytes (priv->bytes, offset, chunk_sz_max);
}

void
fu_firmware_image_add_string (FuFirmwareImage *self, guint idt, GString *str)
{
	FuFirmwareImagePrivate *priv = GET_PRIVATE (self);
	FuFirmwareImageClass *klass = FU_FIRMWARE_IMAGE_GET_CLASS (self);

	fu_common_string_append_kv (str, idt, G_OBJECT_TYPE_NAME (self), NULL);
	if (priv->id != NULL)
		fu_common_string_append_kv (str, idt, "ID", priv->id);
	if (priv->idx != 0x0)
		fu_common_string_append_kx (str, idt, "Index", priv->idx);
	if (priv->addr != 0x0)
		fu_common_string_append_kx (str, idt, "Address", priv->addr);
	if (priv->version != NULL)
		fu_common_string_append_kv (str, 0, "Version", priv->version);
	if (priv->bytes != NULL) {
		fu_common_string_append_kx (str, idt, "Data",
					    g_bytes_get_size (priv->bytes));
	}

	/* vfunc */
	if (klass->to_string != NULL)
		klass->to_string (self, idt, str);
}

/**
 * fu_firmware_image_to_string:
 * @self: A #FuFirmwareImage
 *
 * This allows us to easily print the object.
 *
 * Returns: a string value, or %NULL for invalid.
 *
 * Since: 1.3.1
 **/
gchar *
fu_firmware_image_to_string (FuFirmwareImage *self)
{
	GString *str = g_string_new (NULL);
	fu_firmware_image_add_string (self, 0, str);
	return g_string_free (str, FALSE);
}

static void
fu_firmware_image_init (FuFirmwareImage *self)
{
}

static void
fu_firmware_image_finalize (GObject *object)
{
	FuFirmwareImage *self = FU_FIRMWARE_IMAGE (object);
	FuFirmwareImagePrivate *priv = GET_PRIVATE (self);
	g_free (priv->id);
	g_free (priv->version);
	if (priv->bytes != NULL)
		g_bytes_unref (priv->bytes);
	G_OBJECT_CLASS (fu_firmware_image_parent_class)->finalize (object);
}

static void
fu_firmware_image_class_init (FuFirmwareImageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_firmware_image_finalize;
}

/**
 * fu_firmware_image_new:
 * @bytes: Optional #GBytes
 *
 * Creates an empty firmware_image object.
 *
 * Returns: a #FuFirmwareImage
 *
 * Since: 1.3.1
 **/
FuFirmwareImage *
fu_firmware_image_new (GBytes *bytes)
{
	FuFirmwareImage *self = g_object_new (FU_TYPE_FIRMWARE_IMAGE, NULL);
	if (bytes != NULL)
		fu_firmware_image_set_bytes (self, bytes);
	return self;
}
