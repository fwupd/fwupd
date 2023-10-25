/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-chunk-private.h"
#include "fu-common.h"
#include "fu-firmware.h"
#include "fu-mem.h"
#include "fu-string.h"

/**
 * FuFirmware:
 *
 * A firmware file which can have children which represent the images within.
 *
 * See also: [class@FuDfuFirmware], [class@FuIhexFirmware], [class@FuSrecFirmware]
 */

typedef struct {
	FuFirmwareFlags flags;
	FuFirmware *parent; /* noref */
	GPtrArray *images;  /* FuFirmware */
	gchar *version;
	guint64 version_raw;
	GBytes *bytes;
	guint8 alignment;
	gchar *id;
	gchar *filename;
	guint64 idx;
	guint64 addr;
	guint64 offset;
	gsize size;
	gsize size_max;
	guint images_max;
	GPtrArray *chunks;  /* nullable, element-type FuChunk */
	GPtrArray *patches; /* nullable, element-type FuFirmwarePatch */
} FuFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuFirmware, fu_firmware, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fu_firmware_get_instance_private(o))

enum { PROP_0, PROP_PARENT, PROP_LAST };

/**
 * fu_firmware_flag_to_string:
 * @flag: a #FuFirmwareFlags, e.g. %FU_FIRMWARE_FLAG_DEDUPE_ID
 *
 * Converts a #FuFirmwareFlags to a string.
 *
 * Returns: identifier string
 *
 * Since: 1.5.0
 **/
const gchar *
fu_firmware_flag_to_string(FuFirmwareFlags flag)
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
	if (flag == FU_FIRMWARE_FLAG_DONE_PARSE)
		return "done-parse";
	if (flag == FU_FIRMWARE_FLAG_HAS_STORED_SIZE)
		return "has-stored-size";
	if (flag == FU_FIRMWARE_FLAG_ALWAYS_SEARCH)
		return "always-search";
	if (flag == FU_FIRMWARE_FLAG_NO_AUTO_DETECTION)
		return "no-auto-detection";
	return NULL;
}

/**
 * fu_firmware_flag_from_string:
 * @flag: a string, e.g. `dedupe-id`
 *
 * Converts a string to a #FuFirmwareFlags.
 *
 * Returns: enumerated value
 *
 * Since: 1.5.0
 **/
FuFirmwareFlags
fu_firmware_flag_from_string(const gchar *flag)
{
	if (g_strcmp0(flag, "dedupe-id") == 0)
		return FU_FIRMWARE_FLAG_DEDUPE_ID;
	if (g_strcmp0(flag, "dedupe-idx") == 0)
		return FU_FIRMWARE_FLAG_DEDUPE_IDX;
	if (g_strcmp0(flag, "has-checksum") == 0)
		return FU_FIRMWARE_FLAG_HAS_CHECKSUM;
	if (g_strcmp0(flag, "has-vid-pid") == 0)
		return FU_FIRMWARE_FLAG_HAS_VID_PID;
	if (g_strcmp0(flag, "done-parse") == 0)
		return FU_FIRMWARE_FLAG_DONE_PARSE;
	if (g_strcmp0(flag, "has-stored-size") == 0)
		return FU_FIRMWARE_FLAG_HAS_STORED_SIZE;
	if (g_strcmp0(flag, "always-search") == 0)
		return FU_FIRMWARE_FLAG_ALWAYS_SEARCH;
	if (g_strcmp0(flag, "no-auto-detection") == 0)
		return FU_FIRMWARE_FLAG_NO_AUTO_DETECTION;
	return FU_FIRMWARE_FLAG_NONE;
}

typedef struct {
	gsize offset;
	GBytes *blob;
} FuFirmwarePatch;

static void
fu_firmware_patch_free(FuFirmwarePatch *ptch)
{
	g_bytes_unref(ptch->blob);
	g_free(ptch);
}

/**
 * fu_firmware_add_flag:
 * @firmware: a #FuFirmware
 * @flag: the firmware flag
 *
 * Adds a specific firmware flag to the firmware.
 *
 * Since: 1.5.0
 **/
void
fu_firmware_add_flag(FuFirmware *firmware, FuFirmwareFlags flag)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(firmware);
	g_return_if_fail(FU_IS_FIRMWARE(firmware));
	priv->flags |= flag;
}

/**
 * fu_firmware_has_flag:
 * @firmware: a #FuFirmware
 * @flag: the firmware flag
 *
 * Finds if the firmware has a specific firmware flag.
 *
 * Returns: %TRUE if the flag is set
 *
 * Since: 1.5.0
 **/
gboolean
fu_firmware_has_flag(FuFirmware *firmware, FuFirmwareFlags flag)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(firmware);
	g_return_val_if_fail(FU_IS_FIRMWARE(firmware), FALSE);
	return (priv->flags & flag) > 0;
}

/**
 * fu_firmware_get_version:
 * @self: a #FuFirmware
 *
 * Gets an optional version that represents the firmware.
 *
 * Returns: a string, or %NULL
 *
 * Since: 1.3.3
 **/
const gchar *
fu_firmware_get_version(FuFirmware *self)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_FIRMWARE(self), NULL);
	return priv->version;
}

/**
 * fu_firmware_set_version:
 * @self: a #FuFirmware
 * @version: (nullable): optional string version
 *
 * Sets an optional version that represents the firmware.
 *
 * Since: 1.3.3
 **/
void
fu_firmware_set_version(FuFirmware *self, const gchar *version)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_FIRMWARE(self));

	/* not changed */
	if (g_strcmp0(priv->version, version) == 0)
		return;

	g_free(priv->version);
	priv->version = g_strdup(version);
}

/**
 * fu_firmware_get_version_raw:
 * @self: a #FuFirmware
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
fu_firmware_get_version_raw(FuFirmware *self)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_FIRMWARE(self), G_MAXUINT64);
	return priv->version_raw;
}

/**
 * fu_firmware_set_version_raw:
 * @self: a #FuFirmware
 * @version_raw: a raw version, or %G_MAXUINT64 for invalid
 *
 * Sets an raw version that represents the firmware.
 *
 * This is optional, and is typically only used for debugging.
 *
 * Since: 1.5.7
 **/
void
fu_firmware_set_version_raw(FuFirmware *self, guint64 version_raw)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_FIRMWARE(self));
	priv->version_raw = version_raw;
}

/**
 * fu_firmware_get_filename:
 * @self: a #FuFirmware
 *
 * Gets an optional filename that represents the image source or destination.
 *
 * Returns: a string, or %NULL
 *
 * Since: 1.6.0
 **/
const gchar *
fu_firmware_get_filename(FuFirmware *self)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_FIRMWARE(self), NULL);
	return priv->filename;
}

/**
 * fu_firmware_set_filename:
 * @self: a #FuFirmware
 * @filename: (nullable): a string filename
 *
 * Sets an optional filename that represents the image source or destination.
 *
 * Since: 1.6.0
 **/
void
fu_firmware_set_filename(FuFirmware *self, const gchar *filename)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_FIRMWARE(self));

	/* not changed */
	if (g_strcmp0(priv->filename, filename) == 0)
		return;

	g_free(priv->filename);
	priv->filename = g_strdup(filename);
}

/**
 * fu_firmware_set_id:
 * @self: a #FuPlugin
 * @id: (nullable): image ID, e.g. `config`
 *
 * Since: 1.6.0
 **/
void
fu_firmware_set_id(FuFirmware *self, const gchar *id)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_FIRMWARE(self));

	/* not changed */
	if (g_strcmp0(priv->id, id) == 0)
		return;

	g_free(priv->id);
	priv->id = g_strdup(id);
}

/**
 * fu_firmware_get_id:
 * @self: a #FuPlugin
 *
 * Gets the image ID, typically set at construction.
 *
 * Returns: image ID, e.g. `config`
 *
 * Since: 1.6.0
 **/
const gchar *
fu_firmware_get_id(FuFirmware *self)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_FIRMWARE(self), NULL);
	return priv->id;
}

/**
 * fu_firmware_set_addr:
 * @self: a #FuPlugin
 * @addr: integer
 *
 * Sets the base address of the image.
 *
 * Since: 1.6.0
 **/
void
fu_firmware_set_addr(FuFirmware *self, guint64 addr)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_FIRMWARE(self));
	priv->addr = addr;
}

/**
 * fu_firmware_get_addr:
 * @self: a #FuPlugin
 *
 * Gets the base address of the image.
 *
 * Returns: integer
 *
 * Since: 1.6.0
 **/
guint64
fu_firmware_get_addr(FuFirmware *self)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_FIRMWARE(self), G_MAXUINT64);
	return priv->addr;
}

/**
 * fu_firmware_set_offset:
 * @self: a #FuPlugin
 * @offset: integer
 *
 * Sets the base offset of the image.
 *
 * Since: 1.6.0
 **/
void
fu_firmware_set_offset(FuFirmware *self, guint64 offset)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_FIRMWARE(self));
	priv->offset = offset;
}

/**
 * fu_firmware_get_offset:
 * @self: a #FuPlugin
 *
 * Gets the base offset of the image.
 *
 * Returns: integer
 *
 * Since: 1.6.0
 **/
guint64
fu_firmware_get_offset(FuFirmware *self)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_FIRMWARE(self), G_MAXUINT64);
	return priv->offset;
}

/**
 * fu_firmware_get_parent:
 * @self: a #FuFirmware
 *
 * Gets the parent.
 *
 * Returns: (transfer none): the parent firmware, or %NULL if unset
 *
 * Since: 1.8.2
 **/
FuFirmware *
fu_firmware_get_parent(FuFirmware *self)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_FIRMWARE(self), NULL);
	return priv->parent;
}

/**
 * fu_firmware_set_parent:
 * @self: a #FuFirmware
 * @parent: (nullable): another #FuFirmware
 *
 * Sets the parent. Only used internally.
 *
 * Since: 1.8.2
 **/
void
fu_firmware_set_parent(FuFirmware *self, FuFirmware *parent)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_FIRMWARE(self));

	if (priv->parent != NULL)
		g_object_remove_weak_pointer(G_OBJECT(priv->parent), (gpointer *)&priv->parent);
	if (parent != NULL)
		g_object_add_weak_pointer(G_OBJECT(parent), (gpointer *)&priv->parent);
	priv->parent = parent;
}

/**
 * fu_firmware_set_size:
 * @self: a #FuPlugin
 * @size: integer
 *
 * Sets the total size of the image, which should be the same size as the
 * data from fu_firmware_write().
 *
 * Since: 1.6.0
 **/
void
fu_firmware_set_size(FuFirmware *self, gsize size)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_FIRMWARE(self));
	priv->size = size;
}

/**
 * fu_firmware_get_size:
 * @self: a #FuPlugin
 *
 * Gets the total size of the image, which is typically the same size as the
 * data from fu_firmware_write().
 *
 * If the size has not been explicitly set, and fu_firmware_set_bytes() has been
 * used then the size of this is used instead.
 *
 * Returns: integer
 *
 * Since: 1.6.0
 **/
gsize
fu_firmware_get_size(FuFirmware *self)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_FIRMWARE(self), G_MAXSIZE);
	if (priv->size != 0)
		return priv->size;
	if (priv->bytes != NULL)
		return g_bytes_get_size(priv->bytes);
	return 0;
}

/**
 * fu_firmware_set_size_max:
 * @self: a #FuPlugin
 * @size_max: integer, or 0 for no limit
 *
 * Sets the maximum size of the image allowed during parsing.
 * Implementations should query fu_firmware_get_size_max() during parsing when adding images to
 * ensure the limit is not exceeded.
 *
 * Since: 1.9.7
 **/
void
fu_firmware_set_size_max(FuFirmware *self, gsize size_max)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_FIRMWARE(self));
	priv->size_max = size_max;
}

/**
 * fu_firmware_get_size_max:
 * @self: a #FuPlugin
 *
 * Gets the maximum size of the image allowed during parsing.
 *
 * Returns: integer, or 0 if not set
 *
 * Since: 1.9.7
 **/
gsize
fu_firmware_get_size_max(FuFirmware *self)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_FIRMWARE(self), G_MAXSIZE);
	return priv->size_max;
}

/**
 * fu_firmware_set_idx:
 * @self: a #FuPlugin
 * @idx: integer
 *
 * Sets the index of the image which is used for ordering.
 *
 * Since: 1.6.0
 **/
void
fu_firmware_set_idx(FuFirmware *self, guint64 idx)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_FIRMWARE(self));
	priv->idx = idx;
}

/**
 * fu_firmware_get_idx:
 * @self: a #FuPlugin
 *
 * Gets the index of the image which is used for ordering.
 *
 * Returns: integer
 *
 * Since: 1.6.0
 **/
guint64
fu_firmware_get_idx(FuFirmware *self)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_FIRMWARE(self), G_MAXUINT64);
	return priv->idx;
}

/**
 * fu_firmware_set_bytes:
 * @self: a #FuPlugin
 * @bytes: data blob
 *
 * Sets the contents of the image if not created with fu_firmware_new_from_bytes().
 *
 * Since: 1.6.0
 **/
void
fu_firmware_set_bytes(FuFirmware *self, GBytes *bytes)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_FIRMWARE(self));
	g_return_if_fail(bytes != NULL);
	if (priv->bytes == bytes)
		return;
	if (priv->bytes != NULL)
		g_bytes_unref(priv->bytes);
	priv->bytes = g_bytes_ref(bytes);
}

/**
 * fu_firmware_get_bytes:
 * @self: a #FuPlugin
 * @error: (nullable): optional return location for an error
 *
 * Gets the firmware payload, which does not have any header or footer included.
 *
 * If there is more than one potential payload or image section then fu_firmware_add_image()
 * should be used instead.
 *
 * Returns: (transfer full): a #GBytes, or %NULL if the payload has never been set
 *
 * Since: 1.6.0
 **/
GBytes *
fu_firmware_get_bytes(FuFirmware *self, GError **error)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_FIRMWARE(self), NULL);
	if (priv->bytes == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no payload set");
		return NULL;
	}
	return g_bytes_ref(priv->bytes);
}

/**
 * fu_firmware_get_bytes_with_patches:
 * @self: a #FuPlugin
 * @error: (nullable): optional return location for an error
 *
 * Gets the firmware payload, with any defined patches applied.
 *
 * Returns: (transfer full): a #GBytes, or %NULL if the payload has never been set
 *
 * Since: 1.7.4
 **/
GBytes *
fu_firmware_get_bytes_with_patches(FuFirmware *self, GError **error)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GByteArray) buf = g_byte_array_new();

	g_return_val_if_fail(FU_IS_FIRMWARE(self), NULL);

	if (priv->bytes == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no payload set");
		return NULL;
	}

	/* usual case */
	if (priv->patches == NULL)
		return g_bytes_ref(priv->bytes);

	/* convert to a mutable buffer, apply each patch, aborting if the offset isn't valid */
	fu_byte_array_append_bytes(buf, priv->bytes);
	for (guint i = 0; i < priv->patches->len; i++) {
		FuFirmwarePatch *ptch = g_ptr_array_index(priv->patches, i);
		if (!fu_memcpy_safe(buf->data,
				    buf->len,
				    ptch->offset, /* dst */
				    g_bytes_get_data(ptch->blob, NULL),
				    g_bytes_get_size(ptch->blob),
				    0x0, /* src */
				    g_bytes_get_size(ptch->blob),
				    error)) {
			g_prefix_error(error, "failed to apply patch @0x%x: ", (guint)ptch->offset);
			return NULL;
		}
	}

	/* success */
	return g_bytes_new(buf->data, buf->len);
}

/**
 * fu_firmware_set_alignment:
 * @self: a #FuFirmware
 * @alignment: integer, or 0 to disable
 *
 * Sets the alignment of the firmware.
 *
 * This allows a firmware to pad to a power of 2 boundary, where @alignment
 * is the bit position to align to.
 *
 * Since: 1.6.0
 **/
void
fu_firmware_set_alignment(FuFirmware *self, guint8 alignment)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_FIRMWARE(self));
	priv->alignment = alignment;
}

/**
 * fu_firmware_get_alignment:
 * @self: a #FuFirmware
 *
 * Gets the alignment of the firmware.
 *
 * This allows a firmware to pad to a power of 2 boundary, where @alignment
 * is the bit position to align to.
 *
 * Returns: integer
 *
 * Since: 1.6.0
 **/
guint8
fu_firmware_get_alignment(FuFirmware *self)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_FIRMWARE(self), G_MAXUINT8);
	return priv->alignment;
}

/**
 * fu_firmware_get_chunks:
 * @self: a #FuFirmware
 * @error: (nullable): optional return location for an error
 *
 * Gets the optional image chunks.
 *
 * Returns: (transfer container) (element-type FuChunk) (nullable): chunk data, or %NULL
 *
 * Since: 1.6.0
 **/
GPtrArray *
fu_firmware_get_chunks(FuFirmware *self, GError **error)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_IS_FIRMWARE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* set */
	if (priv->chunks != NULL)
		return g_ptr_array_ref(priv->chunks);

	/* lets build something plausible */
	if (priv->bytes != NULL) {
		g_autoptr(GPtrArray) chunks = NULL;
		g_autoptr(FuChunk) chk = NULL;
		chunks = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
		chk = fu_chunk_bytes_new(priv->bytes);
		fu_chunk_set_idx(chk, priv->idx);
		fu_chunk_set_address(chk, priv->addr);
		g_ptr_array_add(chunks, g_steal_pointer(&chk));
		return g_steal_pointer(&chunks);
	}

	/* nothing to do */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "no bytes or chunks found in firmware");
	return NULL;
}

/**
 * fu_firmware_add_chunk:
 * @self: a #FuFirmware
 * @chk: a #FuChunk
 *
 * Adds a chunk to the image.
 *
 * Since: 1.6.0
 **/
void
fu_firmware_add_chunk(FuFirmware *self, FuChunk *chk)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_FIRMWARE(self));
	g_return_if_fail(FU_IS_CHUNK(chk));
	if (priv->chunks == NULL)
		priv->chunks = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	g_ptr_array_add(priv->chunks, g_object_ref(chk));
}

/**
 * fu_firmware_get_checksum:
 * @self: a #FuPlugin
 * @csum_kind: a checksum type, e.g. %G_CHECKSUM_SHA256
 * @error: (nullable): optional return location for an error
 *
 * Returns a checksum of the payload data.
 *
 * Returns: (transfer full): a checksum string, or %NULL if the checksum is not available
 *
 * Since: 1.6.0
 **/
gchar *
fu_firmware_get_checksum(FuFirmware *self, GChecksumType csum_kind, GError **error)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	FuFirmwareClass *klass = FU_FIRMWARE_GET_CLASS(self);
	g_autoptr(GBytes) blob = NULL;

	g_return_val_if_fail(FU_IS_FIRMWARE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* subclassed */
	if (klass->get_checksum != NULL)
		return klass->get_checksum(self, csum_kind, error);

	/* internal data */
	if (priv->bytes != NULL)
		return g_compute_checksum_for_bytes(csum_kind, priv->bytes);

	/* write */
	blob = fu_firmware_write(self, error);
	if (blob == NULL)
		return NULL;
	return g_compute_checksum_for_bytes(csum_kind, blob);
}

/**
 * fu_firmware_tokenize:
 * @self: a #FuFirmware
 * @fw: firmware blob
 * @flags: install flags, e.g. %FWUPD_INSTALL_FLAG_FORCE
 * @error: (nullable): optional return location for an error
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
fu_firmware_tokenize(FuFirmware *self, GBytes *fw, FwupdInstallFlags flags, GError **error)
{
	FuFirmwareClass *klass = FU_FIRMWARE_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_FIRMWARE(self), FALSE);
	g_return_val_if_fail(fw != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* optionally subclassed */
	if (klass->tokenize != NULL)
		return klass->tokenize(self, fw, flags, error);
	return TRUE;
}

/**
 * fu_firmware_check_compatible:
 * @self: a #FuFirmware
 * @other: a #FuFirmware
 * @flags: install flags, e.g. %FWUPD_INSTALL_FLAG_FORCE
 * @error: (nullable): optional return location for an error
 *
 * Check a new firmware is compatible with the existing firmware.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.8.4
 **/
gboolean
fu_firmware_check_compatible(FuFirmware *self,
			     FuFirmware *other,
			     FwupdInstallFlags flags,
			     GError **error)
{
	FuFirmwareClass *klass = FU_FIRMWARE_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_FIRMWARE(self), FALSE);
	g_return_val_if_fail(FU_IS_FIRMWARE(other), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* optionally subclassed */
	if (klass->check_compatible == NULL)
		return TRUE;
	return klass->check_compatible(self, other, flags, error);
}

static gboolean
fu_firmware_check_magic_for_offset(FuFirmware *self,
				   GBytes *fw,
				   gsize *offset,
				   FwupdInstallFlags flags,
				   GError **error)
{
	FuFirmwareClass *klass = FU_FIRMWARE_GET_CLASS(self);

	/* not implemented */
	if (klass->check_magic == NULL)
		return TRUE;

	/* fuzzing */
	if (!fu_firmware_has_flag(self, FU_FIRMWARE_FLAG_ALWAYS_SEARCH) &&
	    (flags & FWUPD_INSTALL_FLAG_NO_SEARCH) > 0) {
		if (!klass->check_magic(self, fw, *offset, error))
			return FALSE;
		return TRUE;
	}

	/* limit the size of firmware we search */
	if (g_bytes_get_size(fw) > FU_FIRMWARE_SEARCH_MAGIC_BUFSZ_MAX) {
		if (!klass->check_magic(self, fw, *offset, error)) {
			g_prefix_error(error,
				       "failed to search for magic as firmware size was 0x%x and "
				       "limit was 0x%x: ",
				       (guint)g_bytes_get_size(fw),
				       (guint)FU_FIRMWARE_SEARCH_MAGIC_BUFSZ_MAX);
			return FALSE;
		}
		return TRUE;
	}

	/* increment the offset, looking for the magic */
	for (gsize offset_tmp = *offset; offset_tmp < g_bytes_get_size(fw); offset_tmp++) {
		if (klass->check_magic(self, fw, offset_tmp, NULL)) {
			fu_firmware_set_offset(self, offset_tmp);
			*offset = offset_tmp;
			return TRUE;
		}
	}

	/* did not find what we were looking for */
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "did not find magic");
	return FALSE;
}

/**
 * fu_firmware_parse_full:
 * @self: a #FuFirmware
 * @fw: firmware blob
 * @offset: start offset, useful for ignoring a bootloader
 * @flags: install flags, e.g. %FWUPD_INSTALL_FLAG_FORCE
 * @error: (nullable): optional return location for an error
 *
 * Parses a firmware, typically breaking the firmware into images.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.8.2
 **/
gboolean
fu_firmware_parse_full(FuFirmware *self,
		       GBytes *fw,
		       gsize offset,
		       FwupdInstallFlags flags,
		       GError **error)
{
	FuFirmwareClass *klass = FU_FIRMWARE_GET_CLASS(self);
	FuFirmwarePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_IS_FIRMWARE(self), FALSE);
	g_return_val_if_fail(fw != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* sanity check */
	if (fu_firmware_has_flag(self, FU_FIRMWARE_FLAG_DONE_PARSE)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "firmware object cannot be reused");
		return FALSE;
	}
	if (g_bytes_get_size(fw) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "invalid firmware as zero sized");
		return FALSE;
	}
	if (priv->size_max > 0 && g_bytes_get_size(fw) > priv->size_max) {
		g_autofree gchar *sz_val = g_format_size(g_bytes_get_size(fw));
		g_autofree gchar *sz_max = g_format_size(priv->size_max);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware is too large (%s, limit %s)",
			    sz_val,
			    sz_max);
		return FALSE;
	}

	/* any FuFirmware subclass that gets past this point might have allocated memory in
	 * ->tokenize() or ->parse() and needs to be destroyed before parsing again */
	fu_firmware_add_flag(self, FU_FIRMWARE_FLAG_DONE_PARSE);

	/* subclassed */
	if (klass->tokenize != NULL) {
		if (!klass->tokenize(self, fw, flags, error))
			return FALSE;
	}
	if (!fu_firmware_check_magic_for_offset(self, fw, &offset, flags, error))
		return FALSE;

	/* always set by default */
	if (offset == 0x0) {
		fu_firmware_set_bytes(self, fw);
	} else {
		g_autoptr(GBytes) fw_offset = NULL;
		fw_offset = fu_bytes_new_offset(fw, offset, g_bytes_get_size(fw) - offset, error);
		if (fw_offset == NULL)
			return FALSE;
		fu_firmware_set_bytes(self, fw_offset);
	}

	/* handled by the subclass */
	if (klass->parse != NULL)
		return klass->parse(self, fw, offset, flags, error);

	/* verify alignment */
	if (g_bytes_get_size(fw) % (1ull << priv->alignment) != 0) {
		g_autofree gchar *str = NULL;
		str = g_format_size_full(1ull << priv->alignment, G_FORMAT_SIZE_IEC_UNITS);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "raw firmware is not aligned to 0x%x (%s)",
			    (guint)(1ull << priv->alignment),
			    str);
		return FALSE;
	}

	/* success */
	return TRUE;
}

/**
 * fu_firmware_parse:
 * @self: a #FuFirmware
 * @fw: firmware blob
 * @flags: install flags, e.g. %FWUPD_INSTALL_FLAG_FORCE
 * @error: (nullable): optional return location for an error
 *
 * Parses a firmware, typically breaking the firmware into images.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.3.1
 **/
gboolean
fu_firmware_parse(FuFirmware *self, GBytes *fw, FwupdInstallFlags flags, GError **error)
{
	return fu_firmware_parse_full(self, fw, 0x0, flags, error);
}

/**
 * fu_firmware_build:
 * @self: a #FuFirmware
 * @n: a Xmlb node
 * @error: (nullable): optional return location for an error
 *
 * Builds a firmware from an XML manifest. The manifest would typically have the
 * following form:
 *
 * |[<!-- language="XML" -->
 * <?xml version="1.0" encoding="UTF-8"?>
 * <firmware gtype="FuBcm57xxFirmware">
 *   <version>1.2.3</version>
 *   <firmware gtype="FuBcm57xxStage1Image">
 *     <version>7.8.9</version>
 *     <id>stage1</id>
 *     <idx>0x01</idx>
 *     <filename>stage1.bin</filename>
 *   </firmware>
 *   <firmware gtype="FuBcm57xxStage2Image">
 *     <id>stage2</id>
 *     <data/> <!-- empty! -->
 *   </firmware>
 *   <firmware gtype="FuBcm57xxDictImage">
 *     <id>ape</id>
 *     <addr>0x7</addr>
 *     <data>aGVsbG8gd29ybGQ=</data> <!-- base64 -->
 *   </firmware>
 * </firmware>
 * ]|
 *
 * This would be used in a build-system to merge images from generated files:
 * `fwupdtool firmware-build fw.builder.xml test.fw`
 *
 * Static binary content can be specified in the `<firmware>/<data>` section and
 * is encoded as base64 text if not empty.
 *
 * Additionally, extra nodes can be included under nested `<firmware>` objects
 * which can be parsed by the subclassed objects. You should verify the
 * subclassed object `FuFirmware->build` vfunc for the specific additional
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
fu_firmware_build(FuFirmware *self, XbNode *n, GError **error)
{
	FuFirmwareClass *klass = FU_FIRMWARE_GET_CLASS(self);
	const gchar *tmp;
	guint64 tmpval;
	guint64 version_raw;
	g_autoptr(GPtrArray) chunks = NULL;
	g_autoptr(GPtrArray) xb_images = NULL;
	g_autoptr(XbNode) data = NULL;

	g_return_val_if_fail(FU_IS_FIRMWARE(self), FALSE);
	g_return_val_if_fail(XB_IS_NODE(n), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* set attributes */
	tmp = xb_node_query_text(n, "version", NULL);
	if (tmp != NULL)
		fu_firmware_set_version(self, tmp);
	version_raw = xb_node_query_text_as_uint(n, "version_raw", NULL);
	if (version_raw != G_MAXUINT64)
		fu_firmware_set_version_raw(self, version_raw);
	tmp = xb_node_query_text(n, "id", NULL);
	if (tmp != NULL)
		fu_firmware_set_id(self, tmp);
	tmpval = xb_node_query_text_as_uint(n, "idx", NULL);
	if (tmpval != G_MAXUINT64)
		fu_firmware_set_idx(self, tmpval);
	tmpval = xb_node_query_text_as_uint(n, "addr", NULL);
	if (tmpval != G_MAXUINT64)
		fu_firmware_set_addr(self, tmpval);
	tmpval = xb_node_query_text_as_uint(n, "offset", NULL);
	if (tmpval != G_MAXUINT64)
		fu_firmware_set_offset(self, tmpval);
	tmpval = xb_node_query_text_as_uint(n, "size", NULL);
	if (tmpval != G_MAXUINT64)
		fu_firmware_set_size(self, tmpval);
	tmpval = xb_node_query_text_as_uint(n, "size_max", NULL);
	if (tmpval != G_MAXUINT64)
		fu_firmware_set_size_max(self, tmpval);
	tmpval = xb_node_query_text_as_uint(n, "alignment", NULL);
	if (tmpval != G_MAXUINT64) {
		if (tmpval > FU_FIRMWARE_ALIGNMENT_2G) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_FOUND,
				    "0x%x invalid, maximum is 0x%x",
				    (guint)tmpval,
				    (guint)FU_FIRMWARE_ALIGNMENT_2G);
			return FALSE;
		}
		fu_firmware_set_alignment(self, (guint8)tmpval);
	}
	tmp = xb_node_query_text(n, "filename", NULL);
	if (tmp != NULL) {
		g_autoptr(GBytes) blob = NULL;
		blob = fu_bytes_get_contents(tmp, error);
		if (blob == NULL)
			return FALSE;
		fu_firmware_set_bytes(self, blob);
		fu_firmware_set_filename(self, tmp);
	}
	data = xb_node_query_first(n, "data", NULL);
	if (data != NULL) {
		guint64 sz = xb_node_get_attr_as_uint(data, "size");
		g_autoptr(GBytes) blob = NULL;

		/* base64 encoded data */
		if (xb_node_get_text(data) != NULL) {
			gsize bufsz = 0;
			g_autofree guchar *buf = NULL;
			buf = g_base64_decode(xb_node_get_text(data), &bufsz);
			blob = g_bytes_new(buf, bufsz);
		} else {
			blob = g_bytes_new(NULL, 0);
		}

		/* padding is optional */
		if (sz == 0 || sz == G_MAXUINT64) {
			fu_firmware_set_bytes(self, blob);
		} else {
			g_autoptr(GBytes) blob_padded = fu_bytes_pad(blob, (gsize)sz);
			fu_firmware_set_bytes(self, blob_padded);
		}
	}

	/* optional chunks */
	chunks = xb_node_query(n, "chunks/chunk", 0, NULL);
	if (chunks != NULL) {
		for (guint i = 0; i < chunks->len; i++) {
			XbNode *c = g_ptr_array_index(chunks, i);
			g_autoptr(FuChunk) chk = fu_chunk_bytes_new(NULL);
			fu_chunk_set_idx(chk, i);
			if (!fu_chunk_build(chk, c, error))
				return FALSE;
			fu_firmware_add_chunk(self, chk);
		}
	}

	/* parse images */
	xb_images = xb_node_query(n, "firmware", 0, NULL);
	if (xb_images != NULL) {
		for (guint i = 0; i < xb_images->len; i++) {
			XbNode *xb_image = g_ptr_array_index(xb_images, i);
			g_autoptr(FuFirmware) img = NULL;
			tmp = xb_node_get_attr(xb_image, "gtype");
			if (tmp != NULL) {
				GType gtype = g_type_from_name(tmp);
				if (gtype == G_TYPE_INVALID) {
					g_set_error(error,
						    G_IO_ERROR,
						    G_IO_ERROR_NOT_FOUND,
						    "GType %s not registered",
						    tmp);
					return FALSE;
				}
				img = g_object_new(gtype, NULL);
			} else {
				img = fu_firmware_new();
			}
			if (!fu_firmware_add_image_full(self, img, error))
				return FALSE;
			if (!fu_firmware_build(img, xb_image, error))
				return FALSE;
		}
	}

	/* subclassed */
	if (klass->build != NULL) {
		if (!klass->build(self, n, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

/**
 * fu_firmware_build_from_xml:
 * @self: a #FuFirmware
 * @xml: XML text
 * @error: (nullable): optional return location for an error
 *
 * Builds a firmware from an XML manifest. The manifest would typically have the
 * following form:
 *
 * |[<!-- language="XML" -->
 * <?xml version="1.0" encoding="UTF-8"?>
 * <firmware gtype="FuBcm57xxFirmware">
 *   <version>1.2.3</version>
 *   <firmware gtype="FuBcm57xxStage1Image">
 *     <version>7.8.9</version>
 *     <id>stage1</id>
 *     <idx>0x01</idx>
 *     <filename>stage1.bin</filename>
 *   </firmware>
 *   <firmware gtype="FuBcm57xxStage2Image">
 *     <id>stage2</id>
 *     <data/> <!-- empty! -->
 *   </firmware>
 *   <firmware gtype="FuBcm57xxDictImage">
 *     <id>ape</id>
 *     <addr>0x7</addr>
 *     <data>aGVsbG8gd29ybGQ=</data> <!-- base64 -->
 *   </firmware>
 * </firmware>
 * ]|
 *
 * This would be used in a build-system to merge images from generated files:
 * `fwupdtool firmware-build fw.builder.xml test.fw`
 *
 * Static binary content can be specified in the `<firmware>/<data>` section and
 * is encoded as base64 text if not empty.
 *
 * Additionally, extra nodes can be included under nested `<firmware>` objects
 * which can be parsed by the subclassed objects. You should verify the
 * subclassed object `FuFirmware->build` vfunc for the specific additional
 * options supported.
 *
 * Plugins should manually g_type_ensure() subclassed image objects if not
 * constructed as part of the plugin fu_plugin_init() or fu_plugin_setup()
 * functions.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.6.0
 **/
gboolean
fu_firmware_build_from_xml(FuFirmware *self, const gchar *xml, GError **error)
{
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbNode) n = NULL;
	g_autoptr(XbSilo) silo = NULL;

	/* parse XML */
	if (!xb_builder_source_load_xml(source, xml, XB_BUILDER_SOURCE_FLAG_NONE, error)) {
		g_prefix_error(error, "could not parse XML: ");
		return FALSE;
	}
	xb_builder_import_source(builder, source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, error);
	if (silo == NULL)
		return FALSE;

	/* create FuFirmware of specific GType */
	n = xb_silo_query_first(silo, "firmware", error);
	if (n == NULL)
		return FALSE;
	return fu_firmware_build(self, n, error);
}

/**
 * fu_firmware_parse_file:
 * @self: a #FuFirmware
 * @file: a file
 * @flags: install flags, e.g. %FWUPD_INSTALL_FLAG_FORCE
 * @error: (nullable): optional return location for an error
 *
 * Parses a firmware file, typically breaking the firmware into images.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.3.3
 **/
gboolean
fu_firmware_parse_file(FuFirmware *self, GFile *file, FwupdInstallFlags flags, GError **error)
{
	gchar *buf = NULL;
	gsize bufsz = 0;
	g_autoptr(GBytes) fw = NULL;

	g_return_val_if_fail(FU_IS_FIRMWARE(self), FALSE);
	g_return_val_if_fail(G_IS_FILE(file), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!g_file_load_contents(file, NULL, &buf, &bufsz, NULL, error))
		return FALSE;
	fw = g_bytes_new_take(buf, bufsz);
	return fu_firmware_parse(self, fw, flags, error);
}

/**
 * fu_firmware_write:
 * @self: a #FuFirmware
 * @error: (nullable): optional return location for an error
 *
 * Writes a firmware, typically packing the images into a binary blob.
 *
 * Returns: (transfer full): a data blob
 *
 * Since: 1.3.1
 **/
GBytes *
fu_firmware_write(FuFirmware *self, GError **error)
{
	FuFirmwareClass *klass = FU_FIRMWARE_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_FIRMWARE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* subclassed */
	if (klass->write != NULL) {
		g_autoptr(GByteArray) buf = klass->write(self, error);
		if (buf == NULL)
			return NULL;
		return g_bytes_new(buf->data, buf->len);
	}

	/* just add default blob */
	return fu_firmware_get_bytes_with_patches(self, error);
}

/**
 * fu_firmware_add_patch:
 * @self: a #FuFirmware
 * @offset: an address smaller than fu_firmware_get_size()
 * @blob: (not nullable): bytes to replace
 *
 * Adds a byte patch at a specific offset. If a patch already exists at the specified address then
 * it is replaced.
 *
 * If the @address is larger than the size of the image then an error is returned.
 *
 * Since: 1.7.4
 **/
void
fu_firmware_add_patch(FuFirmware *self, gsize offset, GBytes *blob)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	FuFirmwarePatch *ptch;

	g_return_if_fail(FU_IS_FIRMWARE(self));
	g_return_if_fail(blob != NULL);

	/* ensure exists */
	if (priv->patches == NULL) {
		priv->patches =
		    g_ptr_array_new_with_free_func((GDestroyNotify)fu_firmware_patch_free);
	}

	/* find existing of exact same size */
	for (guint i = 0; i < priv->patches->len; i++) {
		ptch = g_ptr_array_index(priv->patches, i);
		if (ptch->offset == offset &&
		    g_bytes_get_size(ptch->blob) == g_bytes_get_size(blob)) {
			g_bytes_unref(ptch->blob);
			ptch->blob = g_bytes_ref(blob);
			return;
		}
	}

	/* add new */
	ptch = g_new0(FuFirmwarePatch, 1);
	ptch->offset = offset;
	ptch->blob = g_bytes_ref(blob);
	g_ptr_array_add(priv->patches, ptch);
}

/**
 * fu_firmware_write_chunk:
 * @self: a #FuFirmware
 * @address: an address smaller than fu_firmware_get_addr()
 * @chunk_sz_max: the size of the new chunk
 * @error: (nullable): optional return location for an error
 *
 * Gets a block of data from the image. If the contents of the image is
 * smaller than the requested chunk size then the #GBytes will be smaller
 * than @chunk_sz_max. Use fu_bytes_pad() if padding is required.
 *
 * If the @address is larger than the size of the image then an error is returned.
 *
 * Returns: (transfer full): a #GBytes, or %NULL
 *
 * Since: 1.6.0
 **/
GBytes *
fu_firmware_write_chunk(FuFirmware *self, guint64 address, guint64 chunk_sz_max, GError **error)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	gsize chunk_left;
	guint64 offset;

	g_return_val_if_fail(FU_IS_FIRMWARE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* check address requested is larger than base address */
	if (address < priv->addr) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "requested address 0x%x less than base address 0x%x",
			    (guint)address,
			    (guint)priv->addr);
		return NULL;
	}

	/* offset into data */
	offset = address - priv->addr;
	if (offset > g_bytes_get_size(priv->bytes)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "offset 0x%x larger than data size 0x%x",
			    (guint)offset,
			    (guint)g_bytes_get_size(priv->bytes));
		return NULL;
	}

	/* if we have less data than requested */
	chunk_left = g_bytes_get_size(priv->bytes) - offset;
	if (chunk_sz_max > chunk_left) {
		return fu_bytes_new_offset(priv->bytes, offset, chunk_left, error);
	}

	/* check chunk */
	return fu_bytes_new_offset(priv->bytes, offset, chunk_sz_max, error);
}

/**
 * fu_firmware_write_file:
 * @self: a #FuFirmware
 * @file: a file
 * @error: (nullable): optional return location for an error
 *
 * Writes a firmware, typically packing the images into a binary blob.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.3.3
 **/
gboolean
fu_firmware_write_file(FuFirmware *self, GFile *file, GError **error)
{
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GFile) parent = NULL;

	g_return_val_if_fail(FU_IS_FIRMWARE(self), FALSE);
	g_return_val_if_fail(G_IS_FILE(file), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	blob = fu_firmware_write(self, error);
	if (blob == NULL)
		return FALSE;
	parent = g_file_get_parent(file);
	if (!g_file_query_exists(parent, NULL)) {
		if (!g_file_make_directory_with_parents(parent, NULL, error))
			return FALSE;
	}
	return g_file_replace_contents(file,
				       g_bytes_get_data(blob, NULL),
				       g_bytes_get_size(blob),
				       NULL,
				       FALSE,
				       G_FILE_CREATE_NONE,
				       NULL,
				       NULL,
				       error);
}

/**
 * fu_firmware_add_image_full:
 * @self: a #FuPlugin
 * @img: a child firmware image
 * @error: (nullable): optional return location for an error
 *
 * Adds an image to the firmware. This method will fail if the number of images would be
 * above the limit set by fu_firmware_set_images_max().
 *
 * If %FU_FIRMWARE_FLAG_DEDUPE_ID is set, an image with the same ID is already
 * present it is replaced.
 *
 * Returns: %TRUE if the image was added
 *
 * Since: 1.9.3
 **/
gboolean
fu_firmware_add_image_full(FuFirmware *self, FuFirmware *img, GError **error)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_IS_FIRMWARE(self), FALSE);
	g_return_val_if_fail(FU_IS_FIRMWARE(img), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* dedupe */
	for (guint i = 0; i < priv->images->len; i++) {
		FuFirmware *img_tmp = g_ptr_array_index(priv->images, i);
		if (priv->flags & FU_FIRMWARE_FLAG_DEDUPE_ID) {
			if (g_strcmp0(fu_firmware_get_id(img_tmp), fu_firmware_get_id(img)) == 0) {
				g_ptr_array_remove_index(priv->images, i);
				break;
			}
		}
		if (priv->flags & FU_FIRMWARE_FLAG_DEDUPE_IDX) {
			if (fu_firmware_get_idx(img_tmp) == fu_firmware_get_idx(img)) {
				g_ptr_array_remove_index(priv->images, i);
				break;
			}
		}
	}

	/* sanity check */
	if (priv->images_max > 0 && priv->images->len >= priv->images_max) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "too many images, limit is %u",
			    priv->images_max);
		return FALSE;
	}

	g_ptr_array_add(priv->images, g_object_ref(img));

	/* set the other way around */
	fu_firmware_set_parent(img, self);

	/* success */
	return TRUE;
}

/**
 * fu_firmware_add_image:
 * @self: a #FuPlugin
 * @img: a child firmware image
 *
 * Adds an image to the firmware.
 *
 * NOTE: If adding images in a loop of any kind then fu_firmware_add_image_full() should be used
 * instead, and fu_firmware_set_images_max() should be set before adding images.
 *
 * If %FU_FIRMWARE_FLAG_DEDUPE_ID is set, an image with the same ID is already
 * present it is replaced.
 *
 * Since: 1.3.1
 **/
void
fu_firmware_add_image(FuFirmware *self, FuFirmware *img)
{
	g_autoptr(GError) error_local = NULL;

	g_return_if_fail(FU_IS_FIRMWARE(self));
	g_return_if_fail(FU_IS_FIRMWARE(img));

	if (!fu_firmware_add_image_full(self, img, &error_local))
		g_critical("failed to add image: %s", error_local->message);
}

/**
 * fu_firmware_set_images_max:
 * @self: a #FuPlugin
 * @images_max: integer, or 0 for unlimited
 *
 * Sets the maximum number of images this container can hold.
 *
 * Since: 1.9.3
 **/
void
fu_firmware_set_images_max(FuFirmware *self, guint images_max)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_FIRMWARE(self));
	priv->images_max = images_max;
}

/**
 * fu_firmware_get_images_max:
 * @self: a #FuPlugin
 *
 * Gets the maximum number of images this container can hold.
 *
 * Returns: integer, or 0 for unlimited.
 *
 * Since: 1.9.3
 **/
guint
fu_firmware_get_images_max(FuFirmware *self)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_FIRMWARE(self), G_MAXUINT);
	return priv->images_max;
}

/**
 * fu_firmware_remove_image:
 * @self: a #FuPlugin
 * @img: a child firmware image
 * @error: (nullable): optional return location for an error
 *
 * Remove an image from the firmware.
 *
 * Returns: %TRUE if the image was removed
 *
 * Since: 1.5.0
 **/
gboolean
fu_firmware_remove_image(FuFirmware *self, FuFirmware *img, GError **error)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_IS_FIRMWARE(self), FALSE);
	g_return_val_if_fail(FU_IS_FIRMWARE(img), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (g_ptr_array_remove(priv->images, img))
		return TRUE;

	/* did not exist */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_FOUND,
		    "image %s not found in firmware",
		    fu_firmware_get_id(img));
	return FALSE;
}

/**
 * fu_firmware_remove_image_by_idx:
 * @self: a #FuPlugin
 * @idx: index
 * @error: (nullable): optional return location for an error
 *
 * Removes the first image from the firmware matching the index.
 *
 * Returns: %TRUE if an image was removed
 *
 * Since: 1.5.0
 **/
gboolean
fu_firmware_remove_image_by_idx(FuFirmware *self, guint64 idx, GError **error)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_autoptr(FuFirmware) img = NULL;

	g_return_val_if_fail(FU_IS_FIRMWARE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	img = fu_firmware_get_image_by_idx(self, idx, error);
	if (img == NULL)
		return FALSE;
	g_ptr_array_remove(priv->images, img);
	return TRUE;
}

/**
 * fu_firmware_remove_image_by_id:
 * @self: a #FuPlugin
 * @id: (nullable): image ID, e.g. `config`
 * @error: (nullable): optional return location for an error
 *
 * Removes the first image from the firmware matching the ID.
 *
 * Returns: %TRUE if an image was removed
 *
 * Since: 1.5.0
 **/
gboolean
fu_firmware_remove_image_by_id(FuFirmware *self, const gchar *id, GError **error)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_autoptr(FuFirmware) img = NULL;

	g_return_val_if_fail(FU_IS_FIRMWARE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	img = fu_firmware_get_image_by_id(self, id, error);
	if (img == NULL)
		return FALSE;
	g_ptr_array_remove(priv->images, img);
	return TRUE;
}

/**
 * fu_firmware_get_images:
 * @self: a #FuFirmware
 *
 * Returns all the images in the firmware.
 *
 * Returns: (transfer container) (element-type FuFirmware): images
 *
 * Since: 1.3.1
 **/
GPtrArray *
fu_firmware_get_images(FuFirmware *self)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GPtrArray) imgs = NULL;

	g_return_val_if_fail(FU_IS_FIRMWARE(self), NULL);

	imgs = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	for (guint i = 0; i < priv->images->len; i++) {
		FuFirmware *img = g_ptr_array_index(priv->images, i);
		g_ptr_array_add(imgs, g_object_ref(img));
	}
	return g_steal_pointer(&imgs);
}

/**
 * fu_firmware_get_image_by_id:
 * @self: a #FuPlugin
 * @id: (nullable): image ID, e.g. `config`
 * @error: (nullable): optional return location for an error
 *
 * Gets the firmware image using the image ID.
 *
 * Returns: (transfer full): a #FuFirmware, or %NULL if the image is not found
 *
 * Since: 1.3.1
 **/
FuFirmware *
fu_firmware_get_image_by_id(FuFirmware *self, const gchar *id, GError **error)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_IS_FIRMWARE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	for (guint i = 0; i < priv->images->len; i++) {
		FuFirmware *img = g_ptr_array_index(priv->images, i);
		if (g_strcmp0(fu_firmware_get_id(img), id) == 0)
			return g_object_ref(img);
	}
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_FOUND,
		    "no image id %s found in firmware",
		    id);
	return NULL;
}

/**
 * fu_firmware_get_image_by_id_bytes:
 * @self: a #FuPlugin
 * @id: (nullable): image ID, e.g. `config`
 * @error: (nullable): optional return location for an error
 *
 * Gets the firmware image bytes using the image ID.
 *
 * Returns: (transfer full): a #GBytes of a #FuFirmware, or %NULL if the image is not found
 *
 * Since: 1.3.1
 **/
GBytes *
fu_firmware_get_image_by_id_bytes(FuFirmware *self, const gchar *id, GError **error)
{
	g_autoptr(FuFirmware) img = fu_firmware_get_image_by_id(self, id, error);
	if (img == NULL)
		return NULL;
	return fu_firmware_write(img, error);
}

/**
 * fu_firmware_get_image_by_idx:
 * @self: a #FuPlugin
 * @idx: image index
 * @error: (nullable): optional return location for an error
 *
 * Gets the firmware image using the image index.
 *
 * Returns: (transfer full): a #FuFirmware, or %NULL if the image is not found
 *
 * Since: 1.3.1
 **/
FuFirmware *
fu_firmware_get_image_by_idx(FuFirmware *self, guint64 idx, GError **error)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_IS_FIRMWARE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	for (guint i = 0; i < priv->images->len; i++) {
		FuFirmware *img = g_ptr_array_index(priv->images, i);
		if (fu_firmware_get_idx(img) == idx)
			return g_object_ref(img);
	}
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_FOUND,
		    "no image idx %" G_GUINT64_FORMAT " found in firmware",
		    idx);
	return NULL;
}

/**
 * fu_firmware_get_image_by_checksum:
 * @self: a #FuPlugin
 * @checksum: checksum string of any format
 * @error: (nullable): optional return location for an error
 *
 * Gets the firmware image using the image checksum. The checksum type is guessed
 * based on the length of the input string.
 *
 * Returns: (transfer full): a #FuFirmware, or %NULL if the image is not found
 *
 * Since: 1.5.5
 **/
FuFirmware *
fu_firmware_get_image_by_checksum(FuFirmware *self, const gchar *checksum, GError **error)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	GChecksumType csum_kind;

	g_return_val_if_fail(FU_IS_FIRMWARE(self), NULL);
	g_return_val_if_fail(checksum != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	csum_kind = fwupd_checksum_guess_kind(checksum);
	for (guint i = 0; i < priv->images->len; i++) {
		FuFirmware *img = g_ptr_array_index(priv->images, i);
		g_autofree gchar *checksum_tmp = NULL;

		/* if this expensive then the subclassed FuFirmware can
		 * cache the result as required */
		checksum_tmp = fu_firmware_get_checksum(img, csum_kind, error);
		if (checksum_tmp == NULL)
			return NULL;
		if (g_strcmp0(checksum_tmp, checksum) == 0)
			return g_object_ref(img);
	}
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_FOUND,
		    "no image with checksum %s found in firmware",
		    checksum);
	return NULL;
}

/**
 * fu_firmware_get_image_by_idx_bytes:
 * @self: a #FuPlugin
 * @idx: image index
 * @error: (nullable): optional return location for an error
 *
 * Gets the firmware image bytes using the image index.
 *
 * Returns: (transfer full): a #GBytes of a #FuFirmware, or %NULL if the image is not found
 *
 * Since: 1.3.1
 **/
GBytes *
fu_firmware_get_image_by_idx_bytes(FuFirmware *self, guint64 idx, GError **error)
{
	g_autoptr(FuFirmware) img = fu_firmware_get_image_by_idx(self, idx, error);
	if (img == NULL)
		return NULL;
	return fu_firmware_write(img, error);
}
/**
 * fu_firmware_get_image_by_gtype_bytes:
 * @self: a #FuPlugin
 * @gtype: an image #GType
 * @error: (nullable): optional return location for an error
 *
 * Gets the firmware image bytes using the image #GType.
 *
 * Returns: (transfer full): a #GBytes of a #FuFirmware, or %NULL if the image is not found
 *
 * Since: 1.9.3
 **/
GBytes *
fu_firmware_get_image_by_gtype_bytes(FuFirmware *self, GType gtype, GError **error)
{
	g_autoptr(FuFirmware) img = fu_firmware_get_image_by_gtype(self, gtype, error);
	if (img == NULL)
		return NULL;
	return fu_firmware_write(img, error);
}

/**
 * fu_firmware_get_image_by_gtype:
 * @self: a #FuPlugin
 * @gtype: an image #GType
 * @error: (nullable): optional return location for an error
 *
 * Gets the firmware image using the image #GType.
 *
 * Returns: (transfer full): a #FuFirmware, or %NULL if the image is not found
 *
 * Since: 1.9.3
 **/
FuFirmware *
fu_firmware_get_image_by_gtype(FuFirmware *self, GType gtype, GError **error)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_IS_FIRMWARE(self), NULL);
	g_return_val_if_fail(gtype != G_TYPE_INVALID, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	for (guint i = 0; i < priv->images->len; i++) {
		FuFirmware *img = g_ptr_array_index(priv->images, i);
		if (g_type_is_a(G_OBJECT_TYPE(img), gtype))
			return g_object_ref(img);
	}
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_FOUND,
		    "no image GType %s found in firmware",
		    g_type_name(gtype));
	return NULL;
}

/**
 * fu_firmware_export:
 * @self: a #FuFirmware
 * @flags: firmware export flags, e.g. %FU_FIRMWARE_EXPORT_FLAG_INCLUDE_DEBUG
 * @bn: a Xmlb builder node
 *
 * This allows us to build an XML object for the nested firmware.
 *
 * Since: 1.6.0
 **/
void
fu_firmware_export(FuFirmware *self, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuFirmwareClass *klass = FU_FIRMWARE_GET_CLASS(self);
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	const gchar *gtypestr = G_OBJECT_TYPE_NAME(self);

	/* object */
	if (g_strcmp0(gtypestr, "FuFirmware") != 0)
		xb_builder_node_set_attr(bn, "gtype", gtypestr);

	/* subclassed type */
	if (priv->flags != FU_FIRMWARE_FLAG_NONE) {
		g_autoptr(GString) tmp = g_string_new("");
		for (guint i = 0; i < 64; i++) {
			guint64 flag = (guint64)1 << i;
			if (flag == FU_FIRMWARE_FLAG_DONE_PARSE)
				continue;
			if ((priv->flags & flag) == 0)
				continue;
			g_string_append_printf(tmp, "%s|", fu_firmware_flag_to_string(flag));
		}
		if (tmp->len > 0) {
			g_string_truncate(tmp, tmp->len - 1);
			fu_xmlb_builder_insert_kv(bn, "flags", tmp->str);
		}
	}
	fu_xmlb_builder_insert_kv(bn, "id", priv->id);
	fu_xmlb_builder_insert_kx(bn, "idx", priv->idx);
	fu_xmlb_builder_insert_kv(bn, "version", priv->version);
	fu_xmlb_builder_insert_kx(bn, "version_raw", priv->version_raw);
	fu_xmlb_builder_insert_kx(bn, "addr", priv->addr);
	fu_xmlb_builder_insert_kx(bn, "offset", priv->offset);
	fu_xmlb_builder_insert_kx(bn, "alignment", priv->alignment);
	fu_xmlb_builder_insert_kx(bn, "size", priv->size);
	fu_xmlb_builder_insert_kx(bn, "size_max", priv->size_max);
	fu_xmlb_builder_insert_kv(bn, "filename", priv->filename);
	if (priv->bytes != NULL) {
		gsize bufsz = 0;
		const guint8 *buf = g_bytes_get_data(priv->bytes, &bufsz);
		g_autofree gchar *datastr = NULL;
		g_autofree gchar *dataszstr = g_strdup_printf("0x%x", (guint)bufsz);
		if (flags & FU_FIRMWARE_EXPORT_FLAG_ASCII_DATA) {
			datastr = fu_memstrsafe(buf, bufsz, 0x0, MIN(bufsz, 16), NULL);
		} else {
			datastr = g_base64_encode(buf, bufsz);
		}
		xb_builder_node_insert_text(bn, "data", datastr, "size", dataszstr, NULL);
	}
	fu_xmlb_builder_insert_kx(bn, "alignment", priv->alignment);

	/* chunks */
	if (priv->chunks != NULL && priv->chunks->len > 0) {
		g_autoptr(XbBuilderNode) bp = xb_builder_node_insert(bn, "chunks", NULL);
		for (guint i = 0; i < priv->chunks->len; i++) {
			FuChunk *chk = g_ptr_array_index(priv->chunks, i);
			g_autoptr(XbBuilderNode) bc = xb_builder_node_insert(bp, "chunk", NULL);
			fu_chunk_export(chk, flags, bc);
		}
	}

	/* vfunc */
	if (klass->export != NULL)
		klass->export(self, flags, bn);

	/* children */
	if (priv->images->len > 0) {
		for (guint i = 0; i < priv->images->len; i++) {
			FuFirmware *img = g_ptr_array_index(priv->images, i);
			g_autoptr(XbBuilderNode) bc = xb_builder_node_insert(bn, "firmware", NULL);
			fu_firmware_export(img, flags, bc);
		}
	}
}

/**
 * fu_firmware_export_to_xml:
 * @self: a #FuFirmware
 * @flags: firmware export flags, e.g. %FU_FIRMWARE_EXPORT_FLAG_INCLUDE_DEBUG
 * @error: (nullable): optional return location for an error
 *
 * This allows us to build an XML object for the nested firmware.
 *
 * Returns: a string value, or %NULL for invalid.
 *
 * Since: 1.6.0
 **/
gchar *
fu_firmware_export_to_xml(FuFirmware *self, FuFirmwareExportFlags flags, GError **error)
{
	g_autoptr(XbBuilderNode) bn = xb_builder_node_new("firmware");
	fu_firmware_export(self, flags, bn);
	return xb_builder_node_export(bn,
				      XB_NODE_EXPORT_FLAG_FORMAT_MULTILINE |
					  XB_NODE_EXPORT_FLAG_COLLAPSE_EMPTY |
					  XB_NODE_EXPORT_FLAG_FORMAT_INDENT,
				      error);
}

/**
 * fu_firmware_to_string:
 * @self: a #FuFirmware
 *
 * This allows us to easily print the object.
 *
 * Returns: a string value, or %NULL for invalid.
 *
 * Since: 1.3.1
 **/
gchar *
fu_firmware_to_string(FuFirmware *self)
{
	g_autoptr(XbBuilderNode) bn = xb_builder_node_new("firmware");
	fu_firmware_export(self,
			   FU_FIRMWARE_EXPORT_FLAG_INCLUDE_DEBUG |
			       FU_FIRMWARE_EXPORT_FLAG_ASCII_DATA,
			   bn);
	return xb_builder_node_export(bn,
				      XB_NODE_EXPORT_FLAG_FORMAT_MULTILINE |
					  XB_NODE_EXPORT_FLAG_COLLAPSE_EMPTY |
					  XB_NODE_EXPORT_FLAG_FORMAT_INDENT,
				      NULL);
}

static void
fu_firmware_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuFirmware *self = FU_FIRMWARE(object);
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_PARENT:
		g_value_set_object(value, priv->parent);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_firmware_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FuFirmware *self = FU_FIRMWARE(object);
	switch (prop_id) {
	case PROP_PARENT:
		fu_firmware_set_parent(self, g_value_get_object(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_firmware_init(FuFirmware *self)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	priv->images = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
}

static void
fu_firmware_finalize(GObject *object)
{
	FuFirmware *self = FU_FIRMWARE(object);
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_free(priv->version);
	g_free(priv->id);
	g_free(priv->filename);
	if (priv->bytes != NULL)
		g_bytes_unref(priv->bytes);
	if (priv->chunks != NULL)
		g_ptr_array_unref(priv->chunks);
	if (priv->patches != NULL)
		g_ptr_array_unref(priv->patches);
	if (priv->parent != NULL)
		g_object_remove_weak_pointer(G_OBJECT(priv->parent), (gpointer *)&priv->parent);
	g_ptr_array_unref(priv->images);
	G_OBJECT_CLASS(fu_firmware_parent_class)->finalize(object);
}

static void
fu_firmware_class_init(FuFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;

	object_class->finalize = fu_firmware_finalize;
	object_class->get_property = fu_firmware_get_property;
	object_class->set_property = fu_firmware_set_property;

	/**
	 * FuFirmware:parent:
	 *
	 * The firmware parent.
	 *
	 * Since: 1.8.2
	 */
	pspec = g_param_spec_object("parent",
				    NULL,
				    NULL,
				    FU_TYPE_FIRMWARE,
				    G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_PARENT, pspec);
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
fu_firmware_new(void)
{
	FuFirmware *self = g_object_new(FU_TYPE_FIRMWARE, NULL);
	return FU_FIRMWARE(self);
}

/**
 * fu_firmware_new_from_bytes:
 * @fw: firmware blob image
 *
 * Creates a firmware object with the provided image set as default.
 *
 * Returns: a #FuFirmware
 *
 * Since: 1.3.1
 **/
FuFirmware *
fu_firmware_new_from_bytes(GBytes *fw)
{
	FuFirmware *self = fu_firmware_new();
	fu_firmware_set_bytes(self, fw);
	return self;
}

/**
 * fu_firmware_new_from_gtypes:
 * @fw: firmware blob
 * @offset: start offset, useful for ignoring a bootloader
 * @flags: install flags, e.g. %FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM
 * @error: (nullable): optional return location for an error
 * @...: an array of #GTypes, ending with %G_TYPE_INVALID
 *
 * Tries to parse the firmware with each #GType in order.
 *
 * Returns: (transfer full) (nullable): a #FuFirmware, or %NULL
 *
 * Since: 1.5.6
 **/
FuFirmware *
fu_firmware_new_from_gtypes(GBytes *fw, gsize offset, FwupdInstallFlags flags, GError **error, ...)
{
	va_list args;
	g_autoptr(GArray) gtypes = g_array_new(FALSE, FALSE, sizeof(GType));
	g_autoptr(GError) error_all = NULL;

	g_return_val_if_fail(fw != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* create array of GTypes */
	va_start(args, error);
	while (TRUE) {
		GType gtype = va_arg(args, GType);
		if (gtype == G_TYPE_INVALID)
			break;
		g_array_append_val(gtypes, gtype);
	}
	va_end(args);

	/* invalid */
	if (gtypes->len == 0) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_ARGUMENT,
				    "no GTypes specified");
		return NULL;
	}

	/* try each GType in turn */
	for (guint i = 0; i < gtypes->len; i++) {
		GType gtype = g_array_index(gtypes, GType, i);
		g_autoptr(FuFirmware) firmware = g_object_new(gtype, NULL);
		g_autoptr(GError) error_local = NULL;
		if (!fu_firmware_parse_full(firmware, fw, offset, flags, &error_local)) {
			if (error_all == NULL) {
				g_propagate_error(&error_all, g_steal_pointer(&error_local));
			} else {
				g_prefix_error(&error_all, "%s: ", error_local->message);
			}
			continue;
		}
		return g_steal_pointer(&firmware);
	}

	/* failed */
	g_propagate_error(error, g_steal_pointer(&error_all));
	return NULL;
}
