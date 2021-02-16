/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuFirmware"

#include "config.h"

#include "fu-common.h"
#include "fu-chunk-private.h"
#include "fu-firmware-image-private.h"

/**
 * SECTION:fu-firmware-image
 * @short_description: a firmware image section
 *
 * An object that represents an image within the firmware file.
 */

typedef struct {
	gchar			*id;
	GBytes			*bytes;
	guint64			 addr;
	guint64			 offset;
	guint64			 idx;
	gchar			*version;
	gchar			*filename;
	GPtrArray		*chunks;	/* nullable, element-type FuChunk */
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

	/* not changed */
	if (g_strcmp0 (priv->version, version) == 0)
		return;

	g_free (priv->version);
	priv->version = g_strdup (version);
}

/**
 * fu_firmware_image_get_filename:
 * @self: A #FuFirmwareImage
 *
 * Gets an optional filename that represents the image source or destination.
 *
 * Returns: a string, or %NULL
 *
 * Since: 1.5.0
 **/
const gchar *
fu_firmware_image_get_filename (FuFirmwareImage *self)
{
	FuFirmwareImagePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_FIRMWARE_IMAGE (self), NULL);
	return priv->filename;
}

/**
 * fu_firmware_image_set_filename:
 * @self: A #FuFirmwareImage
 * @filename: (nullable): A string filename, or %NULL
 *
 * Sets an optional filename that represents the image source or destination.
 *
 * Since: 1.5.0
 **/
void
fu_firmware_image_set_filename (FuFirmwareImage *self, const gchar *filename)
{
	FuFirmwareImagePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_FIRMWARE_IMAGE (self));

	/* not changed */
	if (g_strcmp0 (priv->filename, filename) == 0)
		return;

	g_free (priv->filename);
	priv->filename = g_strdup (filename);
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

	/* not changed */
	if (g_strcmp0 (priv->id, id) == 0)
		return;

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
 * fu_firmware_image_set_offset:
 * @self: a #FuPlugin
 * @offset: integer
 *
 * Sets the base offset of the image.
 *
 * Since: 1.5.0
 **/
void
fu_firmware_image_set_offset (FuFirmwareImage *self, guint64 offset)
{
	FuFirmwareImagePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_FIRMWARE_IMAGE (self));
	priv->offset = offset;
}

/**
 * fu_firmware_image_get_offset:
 * @self: a #FuPlugin
 *
 * Gets the base offset of the image.
 *
 * Returns: integer
 *
 * Since: 1.5.0
 **/
guint64
fu_firmware_image_get_offset (FuFirmwareImage *self)
{
	FuFirmwareImagePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_FIRMWARE_IMAGE (self), G_MAXUINT64);
	return priv->offset;
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
 * fu_firmware_image_get_bytes:
 * @self: a #FuPlugin
 *
 * Gets the data set using fu_firmware_image_set_bytes().
 *
 * This should only really be used by objects subclassing #FuFirmwareImage as
 * images are normally exported to a file using fu_firmware_image_write().
 *
 * Returns: (transfer full): a #GBytes of the data, or %NULL if the bytes is not set
 *
 * Since: 1.5.0
 **/
GBytes *
fu_firmware_image_get_bytes (FuFirmwareImage *self)
{
	FuFirmwareImagePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_FIRMWARE_IMAGE (self), NULL);
	if (priv->bytes == NULL)
		return NULL;
	return g_bytes_ref (priv->bytes);
}
/**
 * fu_firmware_image_get_chunks:
 * @self: a #FuFirmwareImage
 * @error: A #GError, or %NULL
 *
 * Gets the optional image chunks.
 *
 * Return value: (transfer container) (element-type FuChunk) (nullable): chunk data, or %NULL
 *
 * Since: 1.5.6
 **/
GPtrArray *
fu_firmware_image_get_chunks (FuFirmwareImage *self, GError **error)
{
	FuFirmwareImagePrivate *priv = GET_PRIVATE (self);

	g_return_val_if_fail (FU_IS_FIRMWARE_IMAGE (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* set */
	if (priv->chunks != NULL)
		return g_ptr_array_ref (priv->chunks);

	/* lets build something plausible */
	if (priv->bytes != NULL) {
		g_autoptr(GPtrArray) chunks = NULL;
		g_autoptr(FuChunk) chk = NULL;
		chunks = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
		chk = fu_chunk_bytes_new (priv->bytes);
		fu_chunk_set_idx (chk, priv->idx);
		fu_chunk_set_address (chk, priv->addr);
		g_ptr_array_add (chunks, g_steal_pointer (&chk));
		return g_steal_pointer (&chunks);
	}

	/* nothing to do */
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "no bytes or chunks found in firmware");
	return NULL;
}

/**
 * fu_firmware_image_add_chunk:
 * @self: a #FuFirmwareImage
 * @chk: a #FuChunk
 *
 * Adds a chunk to the image.
 *
 * Since: 1.5.6
 **/
void
fu_firmware_image_add_chunk (FuFirmwareImage *self, FuChunk *chk)
{
	FuFirmwareImagePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_FIRMWARE_IMAGE (self));
	g_return_if_fail (FU_IS_CHUNK (chk));
	if (priv->chunks == NULL)
		priv->chunks = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	g_ptr_array_add (priv->chunks, g_object_ref (chk));
}

/**
 * fu_firmware_image_get_checksum:
 * @self: a #FuPlugin
 * @csum_kind: a #GChecksumType, e.g. %G_CHECKSUM_SHA256
 * @error: A #GError, or %NULL
 *
 * Returns a checksum of the data.
 *
 * Returns: (transfer full): a checksum string, or %NULL if the checksum is not available
 *
 * Since: 1.5.5
 **/
gchar *
fu_firmware_image_get_checksum (FuFirmwareImage *self,
				GChecksumType csum_kind,
				GError **error)
{
	FuFirmwareImagePrivate *priv = GET_PRIVATE (self);
	FuFirmwareImageClass *klass = FU_FIRMWARE_IMAGE_GET_CLASS (self);

	g_return_val_if_fail (FU_IS_FIRMWARE_IMAGE (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* subclassed */
	if (klass->get_checksum != NULL)
		return klass->get_checksum (self, csum_kind, error);

	/* internal data */
	if (priv->bytes == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "no bytes found in firmware bytes %s",
			     priv->id);
		return NULL;
	}
	return g_compute_checksum_for_bytes (csum_kind, priv->bytes);
}

/**
 * fu_firmware_image_parse:
 * @self: A #FuFirmwareImage
 * @fw: A #GBytes
 * @flags: some #FwupdInstallFlags, e.g. %FWUPD_INSTALL_FLAG_FORCE
 * @error: A #GError, or %NULL
 *
 * Parses a firmware image, typically checking image CRCs and/or headers.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fu_firmware_image_parse (FuFirmwareImage *self,
			 GBytes *fw,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuFirmwareImageClass *klass = FU_FIRMWARE_IMAGE_GET_CLASS (self);

	g_return_val_if_fail (FU_IS_FIRMWARE_IMAGE (self), FALSE);
	g_return_val_if_fail (fw != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* subclassed */
	if (klass->parse != NULL)
		return klass->parse (self, fw, flags, error);

	/* just add entire blob */
	fu_firmware_image_set_bytes (self, fw);
	return TRUE;
}

/**
 * fu_firmware_image_build:
 * @self: A #FuFirmwareImage
 * @n: A #XbNode
 * @error: A #GError, or %NULL
 *
 * Builds a firmware image from an XML manifest.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fu_firmware_image_build (FuFirmwareImage *self, XbNode *n, GError **error)
{
	FuFirmwareImageClass *klass = FU_FIRMWARE_IMAGE_GET_CLASS (self);
	guint64 tmpval;
	const gchar *tmp;
	g_autoptr(GPtrArray) chunks = NULL;
	g_autoptr(XbNode) data = NULL;

	g_return_val_if_fail (FU_IS_FIRMWARE_IMAGE (self), FALSE);
	g_return_val_if_fail (XB_IS_NODE (n), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	tmp = xb_node_query_text (n, "version", NULL);
	if (tmp != NULL)
		fu_firmware_image_set_version (self, tmp);
	tmp = xb_node_query_text (n, "id", NULL);
	if (tmp != NULL)
		fu_firmware_image_set_id (self, tmp);
	tmpval = xb_node_query_text_as_uint (n, "idx", NULL);
	if (tmpval != G_MAXUINT64)
		fu_firmware_image_set_idx (self, tmpval);
	tmpval = xb_node_query_text_as_uint (n, "addr", NULL);
	if (tmpval != G_MAXUINT64)
		fu_firmware_image_set_addr (self, tmpval);
	tmpval = xb_node_query_text_as_uint (n, "offset", NULL);
	if (tmpval != G_MAXUINT64)
		fu_firmware_image_set_offset (self, tmpval);
	tmp = xb_node_query_text (n, "filename", NULL);
	if (tmp != NULL) {
		g_autoptr(GBytes) blob = NULL;
		blob = fu_common_get_contents_bytes (tmp, error);
		if (blob == NULL)
			return FALSE;
		fu_firmware_image_set_bytes (self, blob);
		fu_firmware_image_set_filename (self, tmp);
	}
	data = xb_node_query_first (n, "data", NULL);
	if (data != NULL && xb_node_get_text (data) != NULL) {
		gsize bufsz = 0;
		g_autofree guchar *buf = NULL;
		g_autoptr(GBytes) blob = NULL;
		buf = g_base64_decode (xb_node_get_text (data), &bufsz);
		blob = g_bytes_new (buf, bufsz);
		fu_firmware_image_set_bytes (self, blob);
	} else if (data != NULL) {
		g_autoptr(GBytes) blob = NULL;
		blob = g_bytes_new (NULL, 0);
		fu_firmware_image_set_bytes (self, blob);
	}

	/* optional chunks */
	chunks = xb_node_query (n, "chunks/chunk", 0, NULL);
	if (chunks != NULL) {
		for (guint i = 0; i < chunks->len; i++) {
			XbNode *c = g_ptr_array_index (chunks, i);
			g_autoptr(FuChunk) chk = fu_chunk_bytes_new (NULL);
			fu_chunk_set_idx (chk, i);
			if (!fu_chunk_build (chk, c, error))
				return FALSE;
			fu_firmware_image_add_chunk (self, chk);
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

	/* set */
	if (priv->bytes != NULL)
		return g_bytes_ref (priv->bytes);

	/* fall back to chunks */
	if (priv->chunks != NULL && priv->chunks->len == 1) {
		FuChunk *chk = g_ptr_array_index (priv->chunks, 0);
		return fu_chunk_get_bytes (chk);
	}

	/* failed */
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_NOT_FOUND,
		     "no bytes found in firmware bytes %s", priv->id);
	return NULL;
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

	g_return_val_if_fail (FU_IS_FIRMWARE_IMAGE (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

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
	if (chunk_sz_max > chunk_left) {
		return fu_common_bytes_new_offset (priv->bytes,
						   offset,
						   chunk_left,
						   error);
	}

	/* check chunk */
	return fu_common_bytes_new_offset (priv->bytes,
					   offset,
					   chunk_sz_max,
					   error);
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
	if (priv->offset != 0x0)
		fu_common_string_append_kx (str, idt, "Offset", priv->offset);
	if (priv->version != NULL)
		fu_common_string_append_kv (str, idt, "Version", priv->version);
	if (priv->filename != NULL)
		fu_common_string_append_kv (str, idt, "Filename", priv->filename);
	if (priv->bytes != NULL) {
		fu_common_string_append_kx (str, idt, "Data",
					    g_bytes_get_size (priv->bytes));
	}

	/* add chunks */
	if (priv->chunks != NULL) {
		for (guint i = 0; i < priv->chunks->len; i++) {
			FuChunk *chk = g_ptr_array_index (priv->chunks, i);
			fu_chunk_add_string (chk, idt + 1, str);
		}
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
	g_free (priv->filename);
	if (priv->bytes != NULL)
		g_bytes_unref (priv->bytes);
	if (priv->chunks != NULL)
		g_ptr_array_unref (priv->chunks);
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
