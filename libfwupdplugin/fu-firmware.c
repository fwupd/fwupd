/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuFirmware"

#include "config.h"

#include "fu-common.h"
#include "fu-firmware.h"
#include "fu-firmware-image-private.h"

/**
 * SECTION:fu-firmware
 * @short_description: a firmware file
 *
 * An object that represents a firmware file.
 * See also: #FuDfuFirmware, #FuIhexFirmware, #FuSrecFirmware
 */

typedef struct {
	GPtrArray			*images;	/* FuFirmwareImage */
	gchar				*version;
} FuFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuFirmware, fu_firmware, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fu_firmware_get_instance_private (o))

/**
 * fu_firmware_get_version:
 * @self: A #FuFirmware
 *
 * Gets an optional version that represents the firmware.
 *
 * Returns: a string, or %NULL
 *
 * Since: 1.3.3
 **/
const gchar *
fu_firmware_get_version (FuFirmware *self)
{
	FuFirmwarePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_FIRMWARE (self), NULL);
	return priv->version;
}

/**
 * fu_firmware_set_version:
 * @self: A #FuFirmware
 * @version: A string version, or %NULL
 *
 * Sets an optional version that represents the firmware.
 *
 * Since: 1.3.3
 **/
void
fu_firmware_set_version (FuFirmware *self, const gchar *version)
{
	FuFirmwarePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_FIRMWARE (self));
	g_free (priv->version);
	priv->version = g_strdup (version);
}

/**
 * fu_firmware_tokenize:
 * @self: A #FuFirmware
 * @fw: A #GBytes
 * @flags: some #FwupdInstallFlags, e.g. %FWUPD_INSTALL_FLAG_FORCE
 * @error: A #GError, or %NULL
 *
 * Tokenizes a firmware, typically breaking the firmware into records.
 *
 * Records can be enumerated using subclass-specific functionality, for example
 * using fu_srec_firmware_get_records().
 *
 * Returns: %TRUE for success
 *
 * Since: 1.3.2
 **/
gboolean
fu_firmware_tokenize (FuFirmware *self, GBytes *fw,
		      FwupdInstallFlags flags, GError **error)
{
	FuFirmwareClass *klass = FU_FIRMWARE_GET_CLASS (self);

	g_return_val_if_fail (FU_IS_FIRMWARE (self), FALSE);
	g_return_val_if_fail (fw != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* optionally subclassed */
	if (klass->tokenize != NULL)
		return klass->tokenize (self, fw, flags, error);
	return TRUE;
}

/**
 * fu_firmware_parse_full:
 * @self: A #FuFirmware
 * @fw: A #GBytes
 * @addr_start: Start address, useful for ignoring a bootloader
 * @addr_end: End address, useful for ignoring config bytes
 * @flags: some #FwupdInstallFlags, e.g. %FWUPD_INSTALL_FLAG_FORCE
 * @error: A #GError, or %NULL
 *
 * Parses a firmware, typically breaking the firmware into images.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.3.1
 **/
gboolean
fu_firmware_parse_full (FuFirmware *self,
			GBytes *fw,
			guint64 addr_start,
			guint64 addr_end,
			FwupdInstallFlags flags,
			GError **error)
{
	FuFirmwareClass *klass = FU_FIRMWARE_GET_CLASS (self);
	g_autoptr(FuFirmwareImage) img = NULL;

	g_return_val_if_fail (FU_IS_FIRMWARE (self), FALSE);
	g_return_val_if_fail (fw != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* subclassed */
	if (klass->tokenize != NULL) {
		if (!klass->tokenize (self, fw, flags, error))
			return FALSE;
	}
	if (klass->parse != NULL)
		return klass->parse (self, fw, addr_start, addr_end, flags, error);

	/* just add entire blob */
	img = fu_firmware_image_new (fw);
	fu_firmware_add_image (self, img);
	return TRUE;
}

/**
 * fu_firmware_parse:
 * @self: A #FuFirmware
 * @fw: A #GBytes
 * @flags: some #FwupdInstallFlags, e.g. %FWUPD_INSTALL_FLAG_FORCE
 * @error: A #GError, or %NULL
 *
 * Parses a firmware, typically breaking the firmware into images.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.3.1
 **/
gboolean
fu_firmware_parse (FuFirmware *self, GBytes *fw, FwupdInstallFlags flags, GError **error)
{
	return fu_firmware_parse_full (self, fw, 0x0, 0x0, flags, error);
}

/**
 * fu_firmware_parse_file:
 * @self: A #FuFirmware
 * @file: A #GFile
 * @flags: some #FwupdInstallFlags, e.g. %FWUPD_INSTALL_FLAG_FORCE
 * @error: A #GError, or %NULL
 *
 * Parses a firmware file, typically breaking the firmware into images.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.3.3
 **/
gboolean
fu_firmware_parse_file (FuFirmware *self, GFile *file, FwupdInstallFlags flags, GError **error)
{
	gchar *buf = NULL;
	gsize bufsz = 0;
	g_autoptr(GBytes) fw = NULL;
	if (!g_file_load_contents (file, NULL, &buf, &bufsz, NULL, error))
		return FALSE;
	fw = g_bytes_new_take (buf, bufsz);
	return fu_firmware_parse (self, fw, flags, error);
}

/**
 * fu_firmware_write:
 * @self: A #FuFirmware
 * @error: A #GError, or %NULL
 *
 * Writes a firmware, typically packing the images into a binary blob.
 *
 * Returns: (transfer full): a #GBytes
 *
 * Since: 1.3.1
 **/
GBytes *
fu_firmware_write (FuFirmware *self, GError **error)
{
	FuFirmwareClass *klass = FU_FIRMWARE_GET_CLASS (self);

	g_return_val_if_fail (FU_IS_FIRMWARE (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* subclassed */
	if (klass->write != NULL)
		return klass->write (self, error);

	/* just add default blob */
	return fu_firmware_get_image_default_bytes (self, error);
}

/**
 * fu_firmware_write_file:
 * @self: A #FuFirmware
 * @file: A #GFile
 * @error: A #GError, or %NULL
 *
 * Writes a firmware, typically packing the images into a binary blob.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.3.3
 **/
gboolean
fu_firmware_write_file (FuFirmware *self, GFile *file, GError **error)
{
	g_autoptr(GBytes) blob = NULL;
	blob = fu_firmware_write (self, error);
	if (blob == NULL)
		return FALSE;
	return g_file_replace_contents (file,
					g_bytes_get_data (blob, NULL),
					g_bytes_get_size (blob),
					NULL, FALSE,
					G_FILE_CREATE_NONE,
					NULL, NULL, error);
}

/**
 * fu_firmware_add_image:
 * @self: a #FuPlugin
 * @img: A #FuFirmwareImage
 *
 * Adds an image to the firmware.
 *
 * If an image with the same ID is already present it is replaced.
 *
 * Since: 1.3.1
 **/
void
fu_firmware_add_image (FuFirmware *self, FuFirmwareImage *img)
{
	FuFirmwarePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_FIRMWARE (self));
	g_return_if_fail (FU_IS_FIRMWARE_IMAGE (img));
	g_ptr_array_add (priv->images, g_object_ref (img));
}

/**
 * fu_firmware_get_images:
 * @self: a #FuFirmware
 *
 * Returns all the images in the firmware.
 *
 * Returns: (transfer container) (element-type FuFirmwareImage): images
 *
 * Since: 1.3.1
 **/
GPtrArray *
fu_firmware_get_images (FuFirmware *self)
{
	FuFirmwarePrivate *priv = GET_PRIVATE (self);
	g_autoptr(GPtrArray) imgs = NULL;

	g_return_val_if_fail (FU_IS_FIRMWARE (self), NULL);

	imgs = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (guint i = 0; i < priv->images->len; i++) {
		FuFirmwareImage *img = g_ptr_array_index (priv->images, i);
		g_ptr_array_add (imgs, g_object_ref (img));
	}
	return g_steal_pointer (&imgs);
}

/**
 * fu_firmware_get_image_by_id:
 * @self: a #FuPlugin
 * @id: (nullable): image ID, e.g. "config"
 * @error: A #GError, or %NULL
 *
 * Gets the firmware image using the image ID.
 *
 * Returns: (transfer full): a #FuFirmwareImage, or %NULL if the image is not found
 *
 * Since: 1.3.1
 **/
FuFirmwareImage *
fu_firmware_get_image_by_id (FuFirmware *self, const gchar *id, GError **error)
{
	FuFirmwarePrivate *priv = GET_PRIVATE (self);

	g_return_val_if_fail (FU_IS_FIRMWARE (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	for (guint i = 0; i < priv->images->len; i++) {
		FuFirmwareImage *img = g_ptr_array_index (priv->images, i);
		if (g_strcmp0 (fu_firmware_image_get_id (img), id) == 0)
			return g_object_ref (img);
	}
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_NOT_FOUND,
		     "no image id %s found in firmware", id);
	return NULL;
}

/**
 * fu_firmware_get_image_by_id_bytes:
 * @self: a #FuPlugin
 * @id: (nullable): image ID, e.g. "config"
 * @error: A #GError, or %NULL
 *
 * Gets the firmware image bytes using the image ID.
 *
 * Returns: (transfer full): a #GBytes of a #FuFirmwareImage, or %NULL if the image is not found
 *
 * Since: 1.3.1
 **/
GBytes *
fu_firmware_get_image_by_id_bytes (FuFirmware *self, const gchar *id, GError **error)
{
	g_autoptr(FuFirmwareImage) img = fu_firmware_get_image_by_id (self, id, error);
	if (img == NULL)
		return NULL;
	return fu_firmware_image_write (img, error);
}

/**
 * fu_firmware_get_image_by_idx:
 * @self: a #FuPlugin
 * @idx: image index
 * @error: A #GError, or %NULL
 *
 * Gets the firmware image using the image index.
 *
 * Returns: (transfer full): a #FuFirmwareImage, or %NULL if the image is not found
 *
 * Since: 1.3.1
 **/
FuFirmwareImage *
fu_firmware_get_image_by_idx (FuFirmware *self, guint64 idx, GError **error)
{
	FuFirmwarePrivate *priv = GET_PRIVATE (self);

	g_return_val_if_fail (FU_IS_FIRMWARE (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	for (guint i = 0; i < priv->images->len; i++) {
		FuFirmwareImage *img = g_ptr_array_index (priv->images, i);
		if (fu_firmware_image_get_idx (img) == idx)
			return g_object_ref (img);
	}
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_NOT_FOUND,
		     "no image idx %" G_GUINT64_FORMAT " found in firmware", idx);
	return NULL;
}

/**
 * fu_firmware_get_image_by_idx_bytes:
 * @self: a #FuPlugin
 * @idx: image index
 * @error: A #GError, or %NULL
 *
 * Gets the firmware image bytes using the image index.
 *
 * Returns: (transfer full): a #GBytes of a #FuFirmwareImage, or %NULL if the image is not found
 *
 * Since: 1.3.1
 **/
GBytes *
fu_firmware_get_image_by_idx_bytes (FuFirmware *self, guint64 idx, GError **error)
{
	g_autoptr(FuFirmwareImage) img = fu_firmware_get_image_by_idx (self, idx, error);
	if (img == NULL)
		return NULL;
	return fu_firmware_image_write (img, error);
}

/**
 * fu_firmware_get_image_default:
 * @self: a #FuPlugin
 * @error: A #GError, or %NULL
 *
 * Gets the default firmware image.
 *
 * NOTE: If the firmware has multiple images included then fu_firmware_get_image_by_id()
 * or fu_firmware_get_image_by_idx() must be used rather than this function.
 *
 * Returns: (transfer full): a #FuFirmwareImage, or %NULL if the image is not found
 *
 * Since: 1.3.1
 **/
FuFirmwareImage *
fu_firmware_get_image_default (FuFirmware *self, GError **error)
{
	FuFirmwarePrivate *priv = GET_PRIVATE (self);
	if (priv->images->len == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "no images in firmware");
		return NULL;
	}
	if (priv->images->len > 1) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "multiple images present in firmware");
		return NULL;
	}
	return g_object_ref (FU_FIRMWARE_IMAGE (g_ptr_array_index (priv->images, 0)));
}

/**
 * fu_firmware_get_image_default_bytes:
 * @self: a #FuPlugin
 * @error: A #GError, or %NULL
 *
 * Gets the default firmware image.
 *
 * Returns: (transfer full): a #GBytes of the image, or %NULL if the image is not found
 *
 * Since: 1.3.1
 **/
GBytes *
fu_firmware_get_image_default_bytes (FuFirmware *self, GError **error)
{
	g_autoptr(FuFirmwareImage) img = fu_firmware_get_image_default (self, error);
	if (img == NULL)
		return NULL;
	return fu_firmware_image_write (img, error);
}

/**
 * fu_firmware_to_string:
 * @self: A #FuFirmware
 *
 * This allows us to easily print the object.
 *
 * Returns: a string value, or %NULL for invalid.
 *
 * Since: 1.3.1
 **/
gchar *
fu_firmware_to_string (FuFirmware *self)
{
	FuFirmwareClass *klass = FU_FIRMWARE_GET_CLASS (self);
	FuFirmwarePrivate *priv = GET_PRIVATE (self);
	GString *str = g_string_new (NULL);

	/* subclassed type */
	fu_common_string_append_kv (str, 0, G_OBJECT_TYPE_NAME (self), NULL);
	if (priv->version != NULL)
		fu_common_string_append_kv (str, 0, "Version", priv->version);

	/* vfunc */
	if (klass->to_string != NULL)
		klass->to_string (self, 0, str);

	for (guint i = 0; i < priv->images->len; i++) {
		FuFirmwareImage *img = g_ptr_array_index (priv->images, i);
		fu_firmware_image_add_string (img, 1, str);
	}

	return g_string_free (str, FALSE);
}

static void
fu_firmware_init (FuFirmware *self)
{
	FuFirmwarePrivate *priv = GET_PRIVATE (self);
	priv->images = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
}

static void
fu_firmware_finalize (GObject *object)
{
	FuFirmware *self = FU_FIRMWARE (object);
	FuFirmwarePrivate *priv = GET_PRIVATE (self);
	g_free (priv->version);
	g_ptr_array_unref (priv->images);
	G_OBJECT_CLASS (fu_firmware_parent_class)->finalize (object);
}

static void
fu_firmware_class_init (FuFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_firmware_finalize;
}

/**
 * fu_firmware_new:
 *
 * Creates an empty firmware object.
 *
 * Returns: a #FuFirmware
 *
 * Since: 1.3.1
 **/
FuFirmware *
fu_firmware_new (void)
{
	FuFirmware *self = g_object_new (FU_TYPE_FIRMWARE, NULL);
	return FU_FIRMWARE (self);
}

/**
 * fu_firmware_new_from_bytes:
 * @fw: A #GBytes image
 *
 * Creates a firmware object with the provided image set as default.
 *
 * Returns: a #FuFirmware
 *
 * Since: 1.3.1
 **/
FuFirmware *
fu_firmware_new_from_bytes (GBytes *fw)
{
	FuFirmware *self = fu_firmware_new ();
	g_autoptr(FuFirmwareImage) img = NULL;
	img = fu_firmware_image_new (fw);
	fu_firmware_add_image (self, img);
	return self;
}
