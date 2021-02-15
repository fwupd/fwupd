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
	FuFirmwareFlags			 flags;
	GPtrArray			*images;	/* FuFirmwareImage */
	gchar				*version;
	guint64				 version_raw;
} FuFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuFirmware, fu_firmware, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fu_firmware_get_instance_private (o))

/**
 * fu_firmware_flag_to_string:
 * @flag: A #FuFirmwareFlags, e.g. %FU_FIRMWARE_FLAG_DEDUPE_ID
 *
 * Converts a #FuFirmwareFlags to a string.
 *
 * Return value: identifier string
 *
 * Since: 1.5.0
 **/
const gchar *
fu_firmware_flag_to_string (FuFirmwareFlags flag)
{
	if (flag == FU_FIRMWARE_FLAG_NONE)
		return "none";
	if (flag == FU_FIRMWARE_FLAG_DEDUPE_ID)
		return "dedupe-id";
	if (flag == FU_FIRMWARE_FLAG_DEDUPE_IDX)
		return "dedupe-idx";
	if (flag == FU_FIRMWARE_FLAG_HAS_CHECKSUM)
		return "has-checksum";
	if (flag == FU_FIRMWARE_FLAG_HAS_VID_PID)
		return "has-vid-pid";
	return NULL;
}

/**
 * fu_firmware_flag_from_string:
 * @flag: A string, e.g. `dedupe-id`
 *
 * Converts a string to a #FuFirmwareFlags.
 *
 * Return value: enumerated value
 *
 * Since: 1.5.0
 **/
FuFirmwareFlags
fu_firmware_flag_from_string (const gchar *flag)
{
	if (g_strcmp0 (flag, "dedupe-id") == 0)
		return FU_FIRMWARE_FLAG_DEDUPE_ID;
	if (g_strcmp0 (flag, "dedupe-idx") == 0)
		return FU_FIRMWARE_FLAG_DEDUPE_IDX;
	if (g_strcmp0 (flag, "has-checksum") == 0)
		return FU_FIRMWARE_FLAG_HAS_CHECKSUM;
	if (g_strcmp0 (flag, "has-vid-pid") == 0)
		return FU_FIRMWARE_FLAG_HAS_VID_PID;
	return FU_FIRMWARE_FLAG_NONE;
}

/**
 * fu_firmware_add_flag:
 * @firmware: A #FuFirmware
 * @flag: the #FuFirmwareFlags
 *
 * Adds a specific firmware flag to the firmware.
 *
 * Since: 1.5.0
 **/
void
fu_firmware_add_flag (FuFirmware *firmware, FuFirmwareFlags flag)
{
	FuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_return_if_fail (FU_IS_FIRMWARE (firmware));
	priv->flags |= flag;
}


/**
 * fu_firmware_has_flag:
 * @firmware: A #FuFirmware
 * @flag: the #FuFirmwareFlags
 *
 * Finds if the firmware has a specific firmware flag.
 *
 * Returns: %TRUE if the flag is set
 *
 * Since: 1.5.0
 **/
gboolean
fu_firmware_has_flag (FuFirmware *firmware, FuFirmwareFlags flag)
{
	FuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_return_val_if_fail (FU_IS_FIRMWARE (firmware), FALSE);
	return (priv->flags & flag) > 0;
}

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

	/* not changed */
	if (g_strcmp0 (priv->version, version) == 0)
		return;

	g_free (priv->version);
	priv->version = g_strdup (version);
}

/**
 * fu_firmware_get_version_raw:
 * @self: A #FuFirmware
 *
 * Gets an raw version that represents the firmware. This is most frequently
 * used when building firmware with `<version_raw>0x123456</version_raw>` in a
 * `firmware.builder.xml` file to avoid string splitting and sanity checks.
 *
 * Returns: an integer, or %G_MAXUINT64 for invalid
 *
 * Since:  1.5.7
 **/
guint64
fu_firmware_get_version_raw (FuFirmware *self)
{
	FuFirmwarePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_FIRMWARE (self), G_MAXUINT64);
	return priv->version_raw;
}

/**
 * fu_firmware_set_version_raw:
 * @self: A #FuFirmware
 * @version: A raw version, or %G_MAXUINT64 for invalid
 *
 * Sets an raw version that represents the firmware.
 *
 * This is optional, and is typically only used for debugging.
 *
 * Since: 1.5.7
 **/
void
fu_firmware_set_version_raw (FuFirmware *self, guint64 version_raw)
{
	FuFirmwarePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_FIRMWARE (self));
	priv->version_raw = version_raw;
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

	/* sanity check */
	if (g_bytes_get_size (fw) == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "invalid firmware as zero sized");
		return FALSE;
	}

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
 * fu_firmware_build:
 * @self: A #FuFirmware
 * @n: A #XbNode
 * @error: A #GError, or %NULL
 *
 * Builds a firmware from an XML manifest. The manifest would typically have the
 * following form:
 *
 * |[<!-- language="XML" -->
 * <?xml version="1.0" encoding="UTF-8"?>
 * <firmware gtype="FuBcm57xxFirmware">
 *   <version>1.2.3</version>
 *   <image gtype="FuBcm57xxStage1Image">
 *     <version>7.8.9</version>
 *     <id>stage1</id>
 *     <idx>0x01</idx>
 *     <filename>stage1.bin</filename>
 *   </image>
 *   <image gtype="FuBcm57xxStage2Image">
 *     <id>stage2</id>
 *     <data/> <!-- empty! -->
 *   </image>
 *   <image gtype="FuBcm57xxDictImage">
 *     <id>ape</id>
 *     <addr>0x7</addr>
 *     <data>aGVsbG8gd29ybGQ=</data> <!-- base64 -->
 *   </image>
 * </firmware>
 * ]|
 *
 * This would be used in a build-system to merge images from generated files:
 * `fwupdtool firmware-build fw.builder.xml test.fw`
 *
 * Static binary content can be specified in the `<image>/<data>` sections and
 * is encoded as base64 text if not empty.
 *
 * Additionally, extra nodes can be included under `<image>` and `<firmware>`
 * which can be parsed by the subclassed objects. You should verify the
 * subclassed object `FuFirmwareImage->build` vfunc for the specific additional
 * options supported.
 *
 * Plugins should manually g_type_ensure() subclassed image objects if not
 * constructed as part of the plugin fu_plugin_init() or fu_plugin_setup()
 * functions.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fu_firmware_build (FuFirmware *self, XbNode *n, GError **error)
{
	FuFirmwareClass *klass = FU_FIRMWARE_GET_CLASS (self);
	const gchar *tmp;
	guint64 version_raw;
	g_autoptr(GPtrArray) xb_images = NULL;

	g_return_val_if_fail (FU_IS_FIRMWARE (self), FALSE);
	g_return_val_if_fail (XB_IS_NODE (n), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* set attributes */
	tmp = xb_node_query_text (n, "version", NULL);
	if (tmp != NULL)
		fu_firmware_set_version (self, tmp);
	version_raw = xb_node_query_text_as_uint (n, "version_raw", NULL);
	if (version_raw != G_MAXUINT64)
		fu_firmware_set_version_raw (self, version_raw);

	/* parse images */
	xb_images = xb_node_query (n, "image", 0, NULL);
	if (xb_images != NULL) {
		for (guint i = 0; i < xb_images->len; i++) {
			XbNode *xb_image = g_ptr_array_index (xb_images, i);
			g_autoptr(FuFirmwareImage) img = NULL;
			tmp = xb_node_get_attr (xb_image, "gtype");
			if (tmp != NULL) {
				GType gtype = g_type_from_name (tmp);
				if (gtype == G_TYPE_INVALID) {
					g_set_error (error,
						     G_IO_ERROR,
						     G_IO_ERROR_NOT_FOUND,
						     "GType %s not registered", tmp);
					return FALSE;
				}
				img = g_object_new (gtype, NULL);
			} else {
				img = fu_firmware_image_new (NULL);
			}
			if (!fu_firmware_image_build (img, xb_image, error))
				return FALSE;
			fu_firmware_add_image (self, img);
		}
	}

	/* subclassed */
	if (klass->build != NULL) {
		if (!klass->build (self, n, error))
			return FALSE;
	}

	/* success */
	return TRUE;
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

	g_return_val_if_fail (FU_IS_FIRMWARE (self), FALSE);
	g_return_val_if_fail (G_IS_FILE (file), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

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

	g_return_val_if_fail (FU_IS_FIRMWARE (self), FALSE);
	g_return_val_if_fail (G_IS_FILE (file), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

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
 * If %FU_FIRMWARE_FLAG_DEDUPE_ID is set, an image with the same ID is already
 * present it is replaced.
 *
 * Since: 1.3.1
 **/
void
fu_firmware_add_image (FuFirmware *self, FuFirmwareImage *img)
{
	FuFirmwarePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_FIRMWARE (self));
	g_return_if_fail (FU_IS_FIRMWARE_IMAGE (img));

	/* dedupe */
	for (guint i = 0; i < priv->images->len; i++) {
		FuFirmwareImage *img_tmp = g_ptr_array_index (priv->images, i);
		if (priv->flags & FU_FIRMWARE_FLAG_DEDUPE_ID) {
			if (g_strcmp0 (fu_firmware_image_get_id (img_tmp),
				       fu_firmware_image_get_id (img)) == 0) {
				g_ptr_array_remove_index (priv->images, i);
				break;
			}
		}
		if (priv->flags & FU_FIRMWARE_FLAG_DEDUPE_IDX) {
			if (fu_firmware_image_get_idx (img_tmp) ==
			    fu_firmware_image_get_idx (img)) {
				g_ptr_array_remove_index (priv->images, i);
				break;
			}
		}
	}

	g_ptr_array_add (priv->images, g_object_ref (img));
}

/**
 * fu_firmware_remove_image:
 * @self: a #FuPlugin
 * @img: A #FuFirmwareImage
 * @error: A #GError, or %NULL
 *
 * Remove an image from the firmware.
 *
 * Returns: %TRUE if the image was removed
 *
 * Since: 1.5.0
 **/
gboolean
fu_firmware_remove_image (FuFirmware *self, FuFirmwareImage *img, GError **error)
{
	FuFirmwarePrivate *priv = GET_PRIVATE (self);

	g_return_val_if_fail (FU_IS_FIRMWARE (self), FALSE);
	g_return_val_if_fail (FU_IS_FIRMWARE_IMAGE (img), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (g_ptr_array_remove (priv->images, img))
		return TRUE;

	/* did not exist */
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_NOT_FOUND,
		     "image %s not found in firmware",
		     fu_firmware_image_get_id (img));
	return FALSE;
}

/**
 * fu_firmware_remove_image_by_idx:
 * @self: a #FuPlugin
 * @idx: index
 * @error: A #GError, or %NULL
 *
 * Removes the first image from the firmware matching the index.
 *
 * Returns: %TRUE if an image was removed
 *
 * Since: 1.5.0
 **/
gboolean
fu_firmware_remove_image_by_idx (FuFirmware *self, guint64 idx, GError **error)
{
	FuFirmwarePrivate *priv = GET_PRIVATE (self);
	g_autoptr(FuFirmwareImage) img = NULL;

	g_return_val_if_fail (FU_IS_FIRMWARE (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	img = fu_firmware_get_image_by_idx (self, idx, error);
	if (img == NULL)
		return FALSE;
	g_ptr_array_remove (priv->images, img);
	return TRUE;
}

/**
 * fu_firmware_remove_image_by_id:
 * @self: a #FuPlugin
 * @id: (nullable): image ID, e.g. "config"
 * @error: A #GError, or %NULL
 *
 * Removes the first image from the firmware matching the ID.
 *
 * Returns: %TRUE if an image was removed
 *
 * Since: 1.5.0
 **/
gboolean
fu_firmware_remove_image_by_id (FuFirmware *self, const gchar *id, GError **error)
{
	FuFirmwarePrivate *priv = GET_PRIVATE (self);
	g_autoptr(FuFirmwareImage) img = NULL;

	g_return_val_if_fail (FU_IS_FIRMWARE (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	img = fu_firmware_get_image_by_id (self, id, error);
	if (img == NULL)
		return FALSE;
	g_ptr_array_remove (priv->images, img);
	return TRUE;
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
 * fu_firmware_get_image_by_checksum:
 * @self: a #FuPlugin
 * @checksum: checksum string of any format
 * @error: A #GError, or %NULL
 *
 * Gets the firmware image using the image checksum. The checksum type is guessed
 * based on the length of the input string.
 *
 * Returns: (transfer full): a #FuFirmwareImage, or %NULL if the image is not found
 *
 * Since: 1.5.5
 **/
FuFirmwareImage *
fu_firmware_get_image_by_checksum (FuFirmware *self,
				   const gchar *checksum,
				   GError **error)
{
	FuFirmwarePrivate *priv = GET_PRIVATE (self);
	GChecksumType csum_kind;

	g_return_val_if_fail (FU_IS_FIRMWARE (self), NULL);
	g_return_val_if_fail (checksum != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	csum_kind = fwupd_checksum_guess_kind (checksum);
	for (guint i = 0; i < priv->images->len; i++) {
		FuFirmwareImage *img = g_ptr_array_index (priv->images, i);
		g_autofree gchar *checksum_tmp = NULL;

		/* if this expensive then the subclassed FuFirmwareImage can
		 * cache the result as required */
		checksum_tmp = fu_firmware_image_get_checksum (img, csum_kind, error);
		if (checksum_tmp == NULL)
			return NULL;
		if (g_strcmp0 (checksum_tmp, checksum) == 0)
			return g_object_ref (img);
	}
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_NOT_FOUND,
		     "no image with checksum %s found in firmware", checksum);
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
	if (priv->flags != FU_FIRMWARE_FLAG_NONE) {
		g_autoptr(GString) tmp = g_string_new ("");
		for (guint i = 0; i < 64; i++) {
			if ((priv->flags & ((guint64) 1 << i)) == 0)
				continue;
			g_string_append_printf (tmp, "%s|",
						fu_firmware_flag_to_string ((guint64) 1 << i));
		}
		if (tmp->len > 0)
			g_string_truncate (tmp, tmp->len - 1);
		fu_common_string_append_kv (str, 0, "Flags", tmp->str);
	}
	if (priv->version != NULL)
		fu_common_string_append_kv (str, 0, "Version", priv->version);
	if (priv->version_raw != 0x0)
		fu_common_string_append_kx (str, 0, "VersionRaw", priv->version_raw);

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

/**
 * fu_firmware_new_from_gtypes:
 * @fw: a #GBytes
 * @flags: a #FwupdInstallFlags, e.g. %FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM
 * @error: (nullable): a #GError or %NULL
 * @...: An array of #GTypes, ending with %G_TYPE_INVALID
 *
 * Tries to parse the firmware with each #GType in order.
 *
 * Return value: (transfer full) (nullable): A #FuFirmware, or %NULL
 *
 * Since: 1.5.6
 **/
FuFirmware *
fu_firmware_new_from_gtypes (GBytes *fw, FwupdInstallFlags flags, GError **error, ...)
{
	va_list args;
	g_autoptr(GArray) gtypes = g_array_new (FALSE, FALSE, sizeof(GType));
	g_autoptr(GError) error_all = NULL;

	g_return_val_if_fail (fw != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create array of GTypes */
	va_start (args, error);
	for (guint i = 0; ; i++) {
		GType gtype = va_arg (args, GType);
		if (gtype == G_TYPE_INVALID)
			break;
		g_array_append_val (gtypes, gtype);
	}
	va_end (args);

	/* invalid */
	if (gtypes->len == 0) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_ARGUMENT,
				     "no GTypes specified");
		return NULL;
	}

	/* try each GType in turn */
	for (guint i = 0; i < gtypes->len; i++) {
		GType gtype = g_array_index (gtypes, GType, i);
		g_autoptr(FuFirmware) firmware = g_object_new (gtype, NULL);
		g_autoptr(GError) error_local = NULL;
		if (!fu_firmware_parse (firmware, fw, flags, &error_local)) {
			if (error_all == NULL) {
				g_propagate_error (&error_all,
						   g_steal_pointer (&error_local));
			} else {
				g_prefix_error (&error_all, "%s: ",
						error_local->message);
			}
			continue;
		}
		return g_steal_pointer (&firmware);
	}

	/* failed */
	g_propagate_error (error, g_steal_pointer (&error_all));
	return NULL;
}
