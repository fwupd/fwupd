/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-chunk-private.h"
#include "fu-common.h"
#include "fu-firmware-private.h"
#include "fu-fuzzer.h"
#include "fu-input-stream.h"
#include "fu-mem.h"
#include "fu-partial-input-stream.h"
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
	FwupdVersionFormat version_format;
	GBytes *bytes;
	FuInputStream *stream;
	gsize streamsz;
	FuFirmwareAlignment alignment;
	gchar *id;
	gchar *filename;
	guint64 idx;
	guint64 addr;
	guint64 offset;
	gsize size;
	guint depth;
	GPtrArray *chunks;  /* nullable, element-type FuChunk */
	GPtrArray *patches; /* nullable, element-type FuFirmwarePatch */
	GPtrArray *magic;   /* nullable, element-type FuFirmwarePatch */
} FuFirmwarePrivate;

#define FU_FIRMWARE_IMAGE_GTYPES_MAX 10

typedef struct {
	guint images_max;
	gsize size_max;
	GType image_gtypes[FU_FIRMWARE_IMAGE_GTYPES_MAX];
	guint image_gtypes_cnt;
} FuFirmwareClassPrivate;

static void
fu_firmware_fuzzer_iface_init(FuFuzzerInterface *iface);

/* nocheck:name */
G_DEFINE_TYPE_EXTENDED(FuFirmware, fu_firmware, G_TYPE_OBJECT, 0, G_ADD_PRIVATE(FuFirmware);
		       g_type_add_class_private(g_define_type_id, sizeof(FuFirmwareClassPrivate));
		       G_IMPLEMENT_INTERFACE(FU_TYPE_FUZZER, fu_firmware_fuzzer_iface_init));

#define GET_PRIVATE(o) (fu_firmware_get_instance_private(o))

static FuFirmwareClassPrivate *
fu_firmware_get_class_private(FuFirmwareClass *klass)
{
	return G_TYPE_CLASS_GET_PRIVATE(klass, FU_TYPE_FIRMWARE, FuFirmwareClassPrivate);
}

enum { PROP_0, PROP_PARENT, PROP_LAST };

#define FU_FIRMWARE_IMAGE_DEPTH_MAX 50

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
 * @self: a #FuFirmware
 * @flag: the firmware flag
 *
 * Adds a specific firmware flag to the firmware.
 **/
void
fu_firmware_add_flag(FuFirmware *self, FuFirmwareFlags flag)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_FIRMWARE(self));
	priv->flags |= flag;
}

/**
 * fu_firmware_has_flag:
 * @self: a #FuFirmware
 * @flag: the firmware flag
 *
 * Finds if the firmware has a specific firmware flag.
 *
 * Returns: %TRUE if the flag is set
 **/
gboolean
fu_firmware_has_flag(FuFirmware *self, FuFirmwareFlags flag)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_FIRMWARE(self), FALSE);
	return (priv->flags & flag) > 0;
}

/**
 * fu_firmware_get_version:
 * @self: a #FuFirmware
 *
 * Gets an optional version that represents the firmware.
 *
 * Returns: a string, or %NULL
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
 **/
void
fu_firmware_set_version(FuFirmware *self, const gchar *version)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_FIRMWARE(self));
	g_set_str(&priv->version, version);
}

/**
 * fu_firmware_get_version_raw:
 * @self: a #FuFirmware
 *
 * Gets an raw version that represents the firmware. This is most frequently
 * used when building firmware with `<version_raw>0x123456</version_raw>` in a
 * `firmware.builder.json` file to avoid string splitting and sanity checks.
 *
 * Returns: an integer, or %G_MAXUINT64 for invalid
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
 **/
void
fu_firmware_set_version_raw(FuFirmware *self, guint64 version_raw)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	FuFirmwareClass *klass = FU_FIRMWARE_GET_CLASS(self);

	g_return_if_fail(FU_IS_FIRMWARE(self));

	priv->version_raw = version_raw;

	/* convert this */
	if (klass->convert_version != NULL) {
		g_autofree gchar *version = klass->convert_version(self, version_raw);
		if (version != NULL)
			fu_firmware_set_version(self, version); /* nocheck:set-version */
	}
}

/**
 * fu_firmware_get_version_format:
 * @self: a #FuFirmware
 *
 * Gets the version format.
 *
 * Returns: the version format, or %FWUPD_VERSION_FORMAT_UNKNOWN if unset
 **/
FwupdVersionFormat
fu_firmware_get_version_format(FuFirmware *self)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_FIRMWARE(self), FWUPD_VERSION_FORMAT_UNKNOWN);
	return priv->version_format;
}

/**
 * fu_firmware_set_version_format:
 * @self: a #FuFirmware
 * @version_format: the version format, e.g. %FWUPD_VERSION_FORMAT_NUMBER
 *
 * Sets the version format.
 **/
void
fu_firmware_set_version_format(FuFirmware *self, FwupdVersionFormat version_format)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	FuFirmwareClass *klass = FU_FIRMWARE_GET_CLASS(self);

	g_return_if_fail(FU_IS_FIRMWARE(self));

	/* same */
	if (priv->version_format == version_format)
		return;
	priv->version_format = version_format;

	/* convert this, now we know */
	if (klass->convert_version != NULL && priv->version != NULL && priv->version_raw != 0) {
		g_autofree gchar *version = klass->convert_version(self, priv->version_raw);
		fu_firmware_set_version(self, version); /* nocheck:set-version */
	}
}

/**
 * fu_firmware_get_filename:
 * @self: a #FuFirmware
 *
 * Gets an optional filename that represents the image source or destination.
 *
 * Returns: a string, or %NULL
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
 **/
void
fu_firmware_set_filename(FuFirmware *self, const gchar *filename)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_FIRMWARE(self));
	g_set_str(&priv->filename, filename);
}

/**
 * fu_firmware_set_id:
 * @self: a #FuPlugin
 * @id: (nullable): image ID, e.g. `config`
 **/
void
fu_firmware_set_id(FuFirmware *self, const gchar *id)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_FIRMWARE(self));
	g_set_str(&priv->id, id);
}

/**
 * fu_firmware_get_id:
 * @self: a #FuPlugin
 *
 * Gets the image ID, typically set at construction.
 *
 * Returns: image ID, e.g. `config`
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
 **/
gsize
fu_firmware_get_size(FuFirmware *self)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_FIRMWARE(self), G_MAXSIZE);
	if (priv->size != 0)
		return priv->size;
	if (priv->stream != NULL && priv->streamsz != 0)
		return priv->streamsz;
	if (priv->bytes != NULL)
		return g_bytes_get_size(priv->bytes);
	return 0;
}

/**
 * fu_firmware_set_size_max:
 * @klass: a #FuFirmwareClass
 * @size_max: integer, or 0 for no limit
 *
 * Sets the maximum size of the image allowed during parsing.
 * Implementations should query fu_firmware_get_size_max() during parsing when adding images to
 * ensure the limit is not exceeded.
 **/
void
fu_firmware_set_size_max(FuFirmwareClass *klass, gsize size_max)
{
	FuFirmwareClassPrivate *cpriv = fu_firmware_get_class_private(klass);
	cpriv->size_max = size_max;
}

/**
 * fu_firmware_get_size_max:
 * @self: a #FuFirmware
 *
 * Gets the maximum size of the image allowed during parsing.
 *
 * Returns: integer, or the default of ~100MiB if not set.
 **/
gsize
fu_firmware_get_size_max(FuFirmware *self)
{
	FuFirmwareClassPrivate *cpriv;
	g_return_val_if_fail(FU_IS_FIRMWARE(self), G_MAXSIZE);
	cpriv = fu_firmware_get_class_private(FU_FIRMWARE_GET_CLASS(self));
	return cpriv->size_max;
}

/**
 * fu_firmware_set_idx:
 * @self: a #FuPlugin
 * @idx: integer
 *
 * Sets the index of the image which is used for ordering.
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

	/* the input stream is no longer valid */
	g_clear_object(&priv->stream);
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
 **/
GBytes *
fu_firmware_get_bytes(FuFirmware *self, GError **error)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_FIRMWARE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	if (priv->bytes != NULL)
		return g_bytes_ref(priv->bytes);
	if (priv->stream != NULL) {
		if (priv->streamsz == 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "stream size unknown");
			return NULL;
		}
		return fu_input_stream_read_bytes(priv->stream, 0x0, priv->streamsz, NULL, error);
	}
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no payload set");
	return NULL;
}

/**
 * fu_firmware_get_bytes_with_patches:
 * @self: a #FuPlugin
 * @error: (nullable): optional return location for an error
 *
 * Gets the firmware payload, with any defined patches applied.
 *
 * Returns: (transfer full): a #GBytes, or %NULL if the payload has never been set
 **/
GBytes *
fu_firmware_get_bytes_with_patches(FuFirmware *self, GError **error)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GByteArray) buf = g_byte_array_new();

	g_return_val_if_fail(FU_IS_FIRMWARE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (priv->bytes == NULL) {
		if (priv->stream != NULL)
			return fu_firmware_get_bytes(self, error);
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no payload set");
		return NULL;
	}

	/* usual case */
	if (priv->patches == NULL)
		return fu_firmware_get_bytes(self, error);

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
 * @alignment: a #FuFirmwareAlignment
 *
 * Sets the alignment of the firmware.
 *
 * This allows a firmware to pad to a power of 2 boundary, where @alignment
 * is the bit position to align to.
 **/
void
fu_firmware_set_alignment(FuFirmware *self, FuFirmwareAlignment alignment)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_FIRMWARE(self));
	priv->alignment = alignment;
}

/**
 * fu_firmware_get_stream:
 * @self: a #FuPlugin
 * @error: (nullable): optional return location for an error
 *
 * Gets the input stream which was used to parse the firmware.
 *
 * Returns: (transfer full): a #FuInputStream, or %NULL if the payload has never been set
 **/
FuInputStream *
fu_firmware_get_stream(FuFirmware *self, GError **error)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_FIRMWARE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	if (priv->stream != NULL)
		return g_object_ref(priv->stream);
	if (priv->bytes != NULL)
		return FWUPD_INPUT_STREAM(fwupd_memory_input_stream_new_from_bytes(priv->bytes));
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no stream or bytes set");
	return NULL;
}

/**
 * fu_firmware_set_stream:
 * @self: a #FuPlugin
 * @stream: (nullable): #FuInputStream
 * @error: (nullable): optional return location for an error
 *
 * Sets the input stream.
 *
 * Returns: %TRUE on success
 **/
gboolean
fu_firmware_set_stream(FuFirmware *self, FuInputStream *stream, GError **error)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_FIRMWARE(self), FALSE);
	g_return_val_if_fail(stream == NULL || FWUPD_IS_INPUT_STREAM(stream), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	if (stream != NULL) {
		if (!fu_input_stream_size(stream, &priv->streamsz, error))
			return FALSE;
	} else {
		priv->streamsz = 0;
	}
	g_set_object(&priv->stream, stream);
	return TRUE;
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
 * Returns: a #FuFirmwareAlignment
 **/
FuFirmwareAlignment
fu_firmware_get_alignment(FuFirmware *self)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_FIRMWARE(self), FU_FIRMWARE_ALIGNMENT_LAST);
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
 * fu_firmware_add_magic:
 * @self: a #FuFirmware
 * @buf: some data
 * @bufsz: sizeof @buf
 * @offset: offset to start parsing, typically 0x0
 *
 * Adds a possible magic signature to the image.
 **/
void
fu_firmware_add_magic(FuFirmware *self, const guint8 *buf, gsize bufsz, gsize offset)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_autofree FuFirmwarePatch *patch = g_new0(FuFirmwarePatch, 1);

	g_return_if_fail(FU_IS_FIRMWARE(self));
	g_return_if_fail(buf != NULL);
	g_return_if_fail(bufsz != 0);

	if (priv->magic == NULL)
		priv->magic =
		    g_ptr_array_new_with_free_func((GDestroyNotify)fu_firmware_patch_free);
	patch->blob = g_bytes_new(buf, bufsz);
	patch->offset = offset;
	g_ptr_array_add(priv->magic, g_steal_pointer(&patch));
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
	if (klass->get_checksum != NULL) {
		g_autoptr(GError) error_local = NULL;
		g_autofree gchar *checksum = klass->get_checksum(self, csum_kind, &error_local);
		if (checksum != NULL)
			return g_steal_pointer(&checksum);
		if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return NULL;
		}
	}

	/* write */
	if (klass->write != NULL) {
		blob = fu_firmware_write(self, error);
		if (blob == NULL)
			return NULL;
		return g_compute_checksum_for_bytes(csum_kind, blob);
	}

	/* internal data */
	if (priv->bytes != NULL)
		return g_compute_checksum_for_bytes(csum_kind, priv->bytes);
	if (priv->stream != NULL)
		return fu_input_stream_compute_checksum(priv->stream, csum_kind, error);

	/* nothing to do */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "no input data, as no FuFirmware->write, stream or bytes");
	return NULL;
}

/**
 * fu_firmware_tokenize:
 * @self: a #FuFirmware
 * @stream: a #FuInputStream
 * @flags: #FuFirmwareParseFlags, e.g. %FWUPD_INSTALL_FLAG_FORCE
 * @error: (nullable): optional return location for an error
 *
 * Tokenizes a firmware, typically breaking the firmware into records.
 *
 * Records can be enumerated using subclass-specific functionality, for example
 * using fu_srec_firmware_get_records().
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_firmware_tokenize(FuFirmware *self,
		     FuInputStream *stream,
		     FuFirmwareParseFlags flags,
		     GError **error)
{
	FuFirmwareClass *klass = FU_FIRMWARE_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_FIRMWARE(self), FALSE);
	g_return_val_if_fail(FWUPD_IS_INPUT_STREAM(stream), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* optionally subclassed */
	if (klass->tokenize != NULL)
		return klass->tokenize(self, stream, flags, error);
	return TRUE;
}

/**
 * fu_firmware_check_compatible:
 * @self: a #FuFirmware
 * @other: a #FuFirmware
 * @flags: #FuFirmwareParseFlags, e.g. %FWUPD_INSTALL_FLAG_FORCE
 * @error: (nullable): optional return location for an error
 *
 * Check a new firmware is compatible with the existing firmware.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_firmware_check_compatible(FuFirmware *self,
			     FuFirmware *other,
			     FuFirmwareParseFlags flags,
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
fu_firmware_validate_with_magic(FuFirmware *self,
				FuInputStream *stream,
				gsize offset,
				gsize *offset_found,
				GError **error)
{
	FuFirmwareClass *klass = FU_FIRMWARE_GET_CLASS(self);
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	gsize offset_lowest = G_MAXSIZE;

	for (guint i = 0; i < priv->magic->len; i++) {
		FuFirmwarePatch *patch = g_ptr_array_index(priv->magic, i);
		gsize offset_tmp = 0;
		g_autoptr(GError) error_local = NULL;

		g_debug("searching for 0x%zx bytes of magic", g_bytes_get_size(patch->blob));
		if (!fu_input_stream_find(stream,
					  g_bytes_get_data(patch->blob, NULL),
					  g_bytes_get_size(patch->blob),
					  offset,
					  &offset_tmp,
					  &error_local)) {
			g_debug("ignoring: %s", error_local->message);
			continue;
		}

		/* ensure magic found at or after expected offset */
		if (offset_tmp < patch->offset) {
			g_debug("magic at 0x%zx but expected >= 0x%zx", offset_tmp, patch->offset);
			continue;
		}
		offset_tmp -= patch->offset;
		g_debug("found magic at 0x%zx", offset_tmp);
		if (!klass->validate(self, stream, offset_tmp, &error_local)) {
			g_debug("ignoring: %s", error_local->message);
			continue;
		}

		/* any better? */
		if (offset_tmp < offset_lowest)
			offset_lowest = offset_tmp;
	}
	if (offset_lowest == G_MAXSIZE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "failed to find magic bytes");
		return FALSE;
	}

	/* success */
	if (offset_found != NULL)
		*offset_found = offset_lowest;
	return TRUE;
}

static gboolean
fu_firmware_validate_for_offset(FuFirmware *self,
				FuInputStream *stream,
				gsize offset,
				gsize *offset_found,
				FuFirmwareParseFlags flags,
				GError **error)
{
	FuFirmwareClass *klass = FU_FIRMWARE_GET_CLASS(self);
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	gsize streamsz = 0;

	/* not implemented */
	if (klass->validate == NULL)
		return TRUE;

	/* fuzzing */
	if (!fu_firmware_has_flag(self, FU_FIRMWARE_FLAG_ALWAYS_SEARCH) &&
	    (flags & FU_FIRMWARE_PARSE_FLAG_NO_SEARCH) > 0) {
		return klass->validate(self, stream, offset, error);
	}

	/* try all the magic values, if provided */
	if (priv->magic != NULL)
		return fu_firmware_validate_with_magic(self, stream, offset, offset_found, error);

	/* limit the size of firmware we search as brute force is expensive */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (streamsz < FU_FIRMWARE_SEARCH_MAGIC_BUFSZ_MAX) {
		for (gsize offset_tmp = offset; offset_tmp < streamsz; offset_tmp++) {
			if (klass->validate(self, stream, offset_tmp, NULL)) {
				*offset_found = offset_tmp;
				return TRUE;
			}
		}
	}

	/* check in more detail */
	return klass->validate(self, stream, offset, error);
}

/**
 * fu_firmware_parse_stream:
 * @self: a #FuFirmware
 * @stream: input stream
 * @offset: start offset
 * @flags: #FuFirmwareParseFlags, e.g. %FWUPD_INSTALL_FLAG_FORCE
 * @error: (nullable): optional return location for an error
 *
 * Parses a firmware from a stream, typically breaking the firmware into images.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_firmware_parse_stream(FuFirmware *self,
			 FuInputStream *stream,
			 gsize offset,
			 FuFirmwareParseFlags flags,
			 GError **error)
{
	FuFirmwareClass *klass = FU_FIRMWARE_GET_CLASS(self);
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	gsize streamsz = 0;
	g_autoptr(FuInputStream) partial_stream = NULL;
	g_autoptr(FuInputStream) seekable_stream = NULL;
	g_autoptr(GBytes) blob = NULL;

	g_return_val_if_fail(FU_IS_FIRMWARE(self), FALSE);
	g_return_val_if_fail(FWUPD_IS_INPUT_STREAM(stream), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* sanity check */
	if (fu_firmware_has_flag(self, FU_FIRMWARE_FLAG_DONE_PARSE)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "firmware object cannot be reused");
		return FALSE;
	}

	/* ensure the stream is seekable */
	if (!G_IS_SEEKABLE(stream) || !g_seekable_can_seek(G_SEEKABLE(stream))) {
		blob = fu_input_stream_read_bytes(stream,
						  offset,
						  fu_firmware_get_size_max(self) + 0x1,
						  NULL,
						  error);
		if (blob == NULL)
			return FALSE;
		seekable_stream =
		    FWUPD_INPUT_STREAM(fwupd_memory_input_stream_new_from_bytes(blob));
	} else {
		seekable_stream = g_object_ref(stream);
	}

	/* check size */
	if (!fu_input_stream_size(seekable_stream, &streamsz, error))
		return FALSE;
	if (streamsz <= offset) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "stream size 0x%x is smaller than offset 0x%x",
			    (guint)streamsz,
			    (guint)offset);
		return FALSE;
	}

	/* optional */
	if (!fu_firmware_validate_for_offset(self, seekable_stream, offset, &offset, flags, error))
		return FALSE;
	fu_firmware_set_offset(self, offset);

	/* validate offset hasn't been corrupted */
	if (offset >= streamsz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "offset 0x%x exceeds stream size 0x%x",
			    (guint)offset,
			    (guint)streamsz);
		return FALSE;
	}

	/* save stream size */
	priv->streamsz = streamsz - offset;
	if (priv->streamsz == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "invalid firmware as zero sized");
		return FALSE;
	}
	{
		FuFirmwareClassPrivate *cpriv =
		    fu_firmware_get_class_private(FU_FIRMWARE_GET_CLASS(self));
		if (cpriv->size_max > 0 && priv->streamsz > cpriv->size_max) {
			g_autofree gchar *sz_val = g_format_size(priv->streamsz);
			g_autofree gchar *sz_max = g_format_size(cpriv->size_max);
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "%s firmware is too large (%s, limit %s)",
				    G_OBJECT_TYPE_NAME(self),
				    sz_val,
				    sz_max);
			return FALSE;
		}

#ifndef SUPPORTED_BUILD
		/* we want all firmware subclasses to set this now */
		if (G_OBJECT_TYPE(self) != FU_TYPE_FIRMWARE &&
		    cpriv->size_max == FU_FIRMWARE_SIZE_MAX_DEFAULT) {
			g_autofree gchar *sz = g_format_size(priv->streamsz);
			g_critical("%s did not set firmware max size with "
				   "fu_firmware_set_size_max(); stream was %s",
				   G_OBJECT_TYPE_NAME(self),
				   sz);
		}
#endif
	}

	/* any FuFirmware subclass that gets past this point might have allocated memory in
	 * ->tokenize() or ->parse() and needs to be destroyed before parsing again */
	fu_firmware_add_flag(self, FU_FIRMWARE_FLAG_DONE_PARSE);

	/* this allows devices to skip reading the old firmware if the GType is unsuitable */
	if (klass->check_compatible != NULL)
		fu_firmware_add_flag(self, FU_FIRMWARE_FLAG_HAS_CHECK_COMPATIBLE);

	/* save stream */
	if (offset == 0) {
		partial_stream = g_object_ref(seekable_stream);
	} else {
		partial_stream =
		    fu_partial_input_stream_new(seekable_stream, offset, priv->streamsz, error);
		if (partial_stream == NULL) {
			g_prefix_error_literal(error, "failed to cut firmware: ");
			return FALSE;
		}
	}

	/* cache */
	if (flags & FU_FIRMWARE_PARSE_FLAG_CACHE_BLOB) {
		if (blob == NULL) {
			blob = fu_input_stream_read_bytes(partial_stream,
							  0x0,
							  priv->streamsz,
							  NULL,
							  error);
			if (blob == NULL)
				return FALSE;
		}
		fu_firmware_set_bytes(self, blob);
	}
	if (flags & FU_FIRMWARE_PARSE_FLAG_CACHE_STREAM)
		g_set_object(&priv->stream, partial_stream);

	/* optional */
	if (klass->tokenize != NULL) {
		if (!klass->tokenize(self, partial_stream, flags, error))
			return FALSE;
	}

	/* optional */
	if (klass->parse_full != NULL)
		return klass->parse_full(self, seekable_stream, offset, flags, error);
	if (klass->parse != NULL)
		return klass->parse(self, partial_stream, flags, error);

	/* verify alignment */
	if (streamsz % (1ull << priv->alignment) != 0) {
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
 * fu_firmware_parse_bytes:
 * @self: a #FuFirmware
 * @fw: firmware blob
 * @offset: start offset, useful for ignoring a bootloader
 * @flags: #FuFirmwareParseFlags, e.g. %FWUPD_INSTALL_FLAG_FORCE
 * @error: (nullable): optional return location for an error
 *
 * Parses a firmware, typically breaking the firmware into images.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_firmware_parse_bytes(FuFirmware *self,
			GBytes *fw,
			gsize offset,
			FuFirmwareParseFlags flags,
			GError **error)
{
	g_autoptr(FwupdMemoryInputStream) stream = NULL;

	g_return_val_if_fail(FU_IS_FIRMWARE(self), FALSE);
	g_return_val_if_fail(fw != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	stream = fwupd_memory_input_stream_new_from_bytes(fw);
	return fu_firmware_parse_stream(self, FWUPD_INPUT_STREAM(stream), offset, flags, error);
}

/**
 * fu_firmware_build:
 * @self: a #FuFirmware
 * @json_object: a #FwupdJsonObject
 * @error: (nullable): optional return location for an error
 *
 * Builds a firmware from a JSON manifest. The manifest would typically have the
 * following form:
 *
 * |[<!-- language="JSON" -->
 * {
 *   "gtype": "FuBcm57xxFirmware",
 *   "version": "1.2.3",
 *   "firmware": [
 *     {
 *       "gtype": "FuBcm57xxStage1Image",
 *       "version": "7.8.9",
 *       "id": "stage1",
 *       "idx": "0x01",
 *       "filename": "stage1.bin"
 *     },
 *     {
 *       "gtype": "FuBcm57xxStage2Image",
 *       "id": "stage2",
 *       "data": ""
 *     },
 *     {
 *       "gtype": "FuBcm57xxDictImage",
 *       "id": "ape",
 *       "addr": "0x7",
 *       "data": "aGVsbG8gd29ybGQ="
 *     }
 *   ]
 * }
 * ]|
 *
 * This would be used in a build-system to merge images from generated files:
 * `fwupdtool firmware-build fw.builder.json test.fw`
 *
 * Static binary content can be specified in the `data` member and is encoded as base64
 * text if not empty. The optional `data_size` member pads the data with `0xFF` bytes.
 *
 * Integer values may be specified as JSON numbers, or as hexadecimal strings with a `0x`
 * prefix, e.g. `"0x1F"`.
 *
 * Additionally, extra members can be included in nested `firmware` objects
 * which can be parsed by the subclassed objects. You should verify the
 * subclassed object `FuFirmware->build` vfunc for the specific additional
 * options supported.
 *
 * Plugins should manually g_type_ensure() subclassed image objects if not
 * constructed as part of the plugin fu_plugin_init() or fu_plugin_setup()
 * functions.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_firmware_build(FuFirmware *self, FwupdJsonObject *json_object, GError **error)
{
	FuFirmwareClass *klass = FU_FIRMWARE_GET_CLASS(self);
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	const gchar *tmp;
	gint64 tmpval = G_MAXINT64;

	g_return_val_if_fail(FU_IS_FIRMWARE(self), FALSE);
	g_return_val_if_fail(json_object != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* subclassed */
	if (klass->build != NULL) {
		if (!klass->build(self, json_object, error))
			return FALSE;
	}

	/* set attributes */
	if (!fwupd_json_object_get_string_optional(json_object, "version", &tmp, error))
		return FALSE;
	if (tmp != NULL)
		fu_firmware_set_version(self, tmp); /* nocheck:set-version */
	if (!fwupd_json_object_get_string_optional(json_object, "version_format", &tmp, error))
		return FALSE;
	if (tmp != NULL) {
		FwupdVersionFormat version_format = fwupd_version_format_from_string(tmp);
		if (version_format == FWUPD_VERSION_FORMAT_UNKNOWN) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "%s is not a valid version format",
				    tmp);
			return FALSE;
		}
		fu_firmware_set_version_format(self, version_format);
	}
	if (!fwupd_json_object_get_integer_with_default(json_object,
							"version_raw",
							&tmpval,
							G_MAXINT64,
							error))
		return FALSE;
	if (tmpval != G_MAXINT64)
		fu_firmware_set_version_raw(self, tmpval);
	if (!fwupd_json_object_get_string_optional(json_object, "id", &tmp, error))
		return FALSE;
	if (tmp != NULL)
		fu_firmware_set_id(self, tmp);
	if (!fwupd_json_object_get_integer_with_default(json_object,
							"idx",
							&tmpval,
							G_MAXINT64,
							error))
		return FALSE;
	if (tmpval != G_MAXINT64)
		fu_firmware_set_idx(self, tmpval);
	if (!fwupd_json_object_get_integer_with_default(json_object,
							"addr",
							&tmpval,
							G_MAXINT64,
							error))
		return FALSE;
	if (tmpval != G_MAXINT64)
		fu_firmware_set_addr(self, tmpval);
	if (!fwupd_json_object_get_integer_with_default(json_object,
							"offset",
							&tmpval,
							G_MAXINT64,
							error))
		return FALSE;
	if (tmpval != G_MAXINT64)
		fu_firmware_set_offset(self, tmpval);
	if (!fwupd_json_object_get_integer_with_default(json_object,
							"size",
							&tmpval,
							G_MAXINT64,
							error))
		return FALSE;
	if (tmpval != G_MAXINT64)
		fu_firmware_set_size(self, (gsize)tmpval);
	if (!fwupd_json_object_get_integer_with_default(json_object,
							"size_max",
							&tmpval,
							G_MAXINT64,
							error))
		return FALSE;
	if (tmpval != G_MAXINT64)
		fu_firmware_set_size_max(FU_FIRMWARE_GET_CLASS(self), (gsize)tmpval);
	if (!fwupd_json_object_get_integer_with_default(json_object,
							"alignment",
							&tmpval,
							G_MAXINT64,
							error))
		return FALSE;
	if (tmpval != G_MAXINT64) {
		if (tmpval < 0 || tmpval > FU_FIRMWARE_ALIGNMENT_2G) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "0x%x invalid, maximum is 0x%x",
				    (guint)tmpval,
				    (guint)FU_FIRMWARE_ALIGNMENT_2G);
			return FALSE;
		}
		fu_firmware_set_alignment(self, (guint8)tmpval);
	}

	/* data, which may be empty */
	if (fwupd_json_object_has_node(json_object, "data") ||
	    fwupd_json_object_has_node(json_object, "data_type")) {
		g_autoptr(GBytes) blob = NULL;

		/* base64 encoded data */
		if (!fwupd_json_object_get_string_optional(json_object, "data", &tmp, error))
			return FALSE;
		if (tmp != NULL) {
			gsize bufsz = 0;
			g_autofree guchar *buf = g_base64_decode(tmp, &bufsz);
			blob = g_bytes_new(buf, bufsz);
		} else {
			blob = g_bytes_new(NULL, 0);
		}

		/* padding is optional */
		if (!fwupd_json_object_get_integer_with_default(json_object,
								"data_size",
								&tmpval,
								G_MAXINT64,
								error))
			return FALSE;
		if (tmpval <= 0 || tmpval == G_MAXINT64) {
			fu_firmware_set_bytes(self, blob);
		} else {
			g_autoptr(GBytes) blob_padded = fu_bytes_pad(blob, (gsize)tmpval, 0xFF);
			fu_firmware_set_bytes(self, blob_padded);
		}
	}
	if (!fwupd_json_object_get_string_optional(json_object, "filename", &tmp, error))
		return FALSE;
	if (tmp != NULL) {
		if (priv->bytes == NULL) {
			g_autoptr(GBytes) blob = NULL;
			blob = fu_bytes_get_contents(tmp, error);
			if (blob == NULL)
				return FALSE;
			fu_firmware_set_bytes(self, blob);
		}
		fu_firmware_set_filename(self, tmp);
	}

	/* optional chunks */
	if (fwupd_json_object_has_node(json_object, "chunks")) {
		g_autoptr(FwupdJsonArray) json_chunks =
		    fwupd_json_object_get_array(json_object, "chunks", error);
		if (json_chunks == NULL)
			return FALSE;
		for (guint i = 0; i < fwupd_json_array_get_size(json_chunks); i++) {
			g_autoptr(FuChunk) chk = fu_chunk_bytes_new(NULL);
			g_autoptr(FwupdJsonObject) json_chunk =
			    fwupd_json_array_get_object(json_chunks, i, error);
			if (json_chunk == NULL)
				return FALSE;
			fu_chunk_set_idx(chk, i);
			if (!fu_chunk_build(chk, json_chunk, error))
				return FALSE;
			fu_firmware_add_chunk(self, chk);
		}
	}

	/* parse images */
	if (fwupd_json_object_has_node(json_object, "firmware")) {
		g_autoptr(FwupdJsonArray) json_images =
		    fwupd_json_object_get_array(json_object, "firmware", error);
		if (json_images == NULL)
			return FALSE;
		for (guint i = 0; i < fwupd_json_array_get_size(json_images); i++) {
			g_autoptr(FuFirmware) img = NULL;
			g_autoptr(FwupdJsonObject) json_image =
			    fwupd_json_array_get_object(json_images, i, error);
			if (json_image == NULL)
				return FALSE;
			if (!fwupd_json_object_get_string_optional(json_image,
								   "gtype",
								   &tmp,
								   error))
				return FALSE;
			if (tmp != NULL) {
				GType gtype = G_TYPE_INVALID;
				if (!fu_gtype_from_name(tmp, &gtype, FU_TYPE_FIRMWARE, error))
					return FALSE;
				img = g_object_new(gtype, NULL);
			} else {
				img = fu_firmware_new();
			}
			if (!fu_firmware_add_image(self, img, error))
				return FALSE;
			if (!fu_firmware_build(img, json_image, error))
				return FALSE;
		}
	}

	/* success */
	return TRUE;
}

/* parses a builder manifest into the root JSON object */
static FwupdJsonObject *
fu_firmware_builder_load_json(GBytes *blob, GError **error)
{
	g_autoptr(FwupdJsonParser) json_parser = fwupd_json_parser_new();
	g_autoptr(FwupdJsonNode) json_node = NULL;

	/* base64 data can be quite large */
	fwupd_json_parser_set_max_depth(json_parser, 50);
	fwupd_json_parser_set_max_items(json_parser, 100000);
	fwupd_json_parser_set_max_quoted(json_parser, 0x4000000);
	json_node =
	    fwupd_json_parser_load_from_bytes(json_parser, blob, FWUPD_JSON_LOAD_FLAG_NONE, error);
	if (json_node == NULL) {
		g_prefix_error_literal(error, "could not parse JSON: ");
		return NULL;
	}
	return fwupd_json_node_get_object(json_node, error);
}

/**
 * fu_firmware_new_from_json:
 * @json: JSON text
 * @error: (nullable): optional return location for an error
 *
 * Builds a firmware from a JSON manifest. The manifest would typically have the
 * following form:
 *
 * |[<!-- language="JSON" -->
 * {
 *   "gtype": "FuBcm57xxFirmware",
 *   "version": "1.2.3",
 *   "firmware": [
 *     {
 *       "gtype": "FuBcm57xxStage1Image",
 *       "version": "7.8.9",
 *       "id": "stage1",
 *       "idx": "0x01",
 *       "filename": "stage1.bin"
 *     },
 *     {
 *       "gtype": "FuBcm57xxStage2Image",
 *       "id": "stage2",
 *       "data": ""
 *     },
 *     {
 *       "gtype": "FuBcm57xxDictImage",
 *       "id": "ape",
 *       "addr": "0x7",
 *       "data": "aGVsbG8gd29ybGQ="
 *     }
 *   ]
 * }
 * ]|
 *
 * This would be used in a build-system to merge images from generated files:
 * `fwupdtool firmware-build fw.builder.json test.fw`
 *
 * Static binary content can be specified in the `data` member and is encoded as base64
 * text if not empty. The optional `data_size` member pads the data with `0xFF` bytes.
 *
 * Integer values may be specified as JSON numbers, or as hexadecimal strings with a `0x`
 * prefix, e.g. `"0x1F"`.
 *
 * Additionally, extra members can be included in nested `firmware` objects
 * which can be parsed by the subclassed objects. You should verify the
 * subclassed object `FuFirmware->build` vfunc for the specific additional
 * options supported.
 *
 * Firmware subclasses should use fu_firmware_add_image_gtype() for subclassed image objects.
 *
 * Returns: %TRUE for success
 **/
FuFirmware *
fu_firmware_new_from_json(const gchar *json, GError **error)
{
	GType gtype;
	const gchar *gtypestr = NULL;
	g_autoptr(FuFirmware) self = NULL;
	g_autoptr(FwupdJsonObject) json_object = NULL;
	g_autoptr(GBytes) blob = NULL;

	g_return_val_if_fail(json != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* parse JSON */
	blob = g_bytes_new_static(json, strlen(json));
	json_object = fu_firmware_builder_load_json(blob, error);
	if (json_object == NULL)
		return NULL;

	/* create FuFirmware of specific GType */
	gtypestr = fwupd_json_object_get_string(json_object, "gtype", NULL);
	if (gtypestr == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "expected gtype for firmware");
		return NULL;
	}
	if (!fu_gtype_from_name(gtypestr, &gtype, FU_TYPE_FIRMWARE, error))
		return NULL;
	self = g_object_new(gtype, NULL);
	if (!fu_firmware_build(self, json_object, error))
		return NULL;
	return g_steal_pointer(&self);
}

/**
 * fu_firmware_new_from_filename:
 * @filename: filename of JSON builder
 * @error: (nullable): optional return location for an error
 *
 * Builds a firmware from a JSON manifest.
 *
 * Returns: a #FuFirmware on success, else %NULL
 **/
FuFirmware *
fu_firmware_new_from_filename(const gchar *filename, GError **error)
{
	g_autofree gchar *json = NULL;

	g_return_val_if_fail(filename != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (!g_file_get_contents(filename, &json, NULL, error))
		return NULL;
	return fu_firmware_new_from_json(json, error);
}

/**
 * fu_firmware_parse_file:
 * @self: a #FuFirmware
 * @file: a file
 * @flags: #FuFirmwareParseFlags, e.g. %FWUPD_INSTALL_FLAG_FORCE
 * @error: (nullable): optional return location for an error
 *
 * Parses a firmware file, typically breaking the firmware into images.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_firmware_parse_file(FuFirmware *self, GFile *file, FuFirmwareParseFlags flags, GError **error)
{
	g_autoptr(FwupdFileInputStream) stream = NULL;

	g_return_val_if_fail(FU_IS_FIRMWARE(self), FALSE);
	g_return_val_if_fail(G_IS_FILE(file), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	stream = fwupd_file_input_stream_new_from_file(file, error);
	if (stream == NULL) {
		fwupd_error_convert(error);
		return FALSE;
	}
	return fu_firmware_parse_stream(self, FWUPD_INPUT_STREAM(stream), 0, flags, error);
}

/**
 * fu_firmware_write_array:
 * @self: a #FuFirmware
 * @error: (nullable): optional return location for an error
 *
 * Writes a firmware, typically packing the images into a binary blob.
 *
 * Returns: (transfer full): a data blob
 **/
GByteArray *
fu_firmware_write_array(FuFirmware *self, GError **error)
{
	FuFirmwareClass *klass = FU_FIRMWARE_GET_CLASS(self);
	g_autoptr(GBytes) blob = NULL;

	g_return_val_if_fail(FU_IS_FIRMWARE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* subclassed */
	if (klass->write != NULL)
		return klass->write(self, error);

	/* just add default blob */
	blob = fu_firmware_get_bytes_with_patches(self, error);
	if (blob == NULL)
		return NULL;
	return g_bytes_unref_to_array(g_steal_pointer(&blob));
}

/**
 * fu_firmware_write:
 * @self: a #FuFirmware
 * @error: (nullable): optional return location for an error
 *
 * Writes a firmware, typically packing the images into a binary blob.
 *
 * Returns: (transfer full): a data blob
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
 **/
GBytes *
fu_firmware_write_chunk(FuFirmware *self, guint64 address, guint64 chunk_sz_max, GError **error)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	gsize chunk_left;
	guint64 offset;

	g_return_val_if_fail(FU_IS_FIRMWARE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* check bytes are set */
	if (priv->bytes == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no payload set");
		return NULL;
	}

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
	if (chunk_sz_max > chunk_left)
		return fu_bytes_new_offset(priv->bytes, offset, chunk_left, error);

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

static void
fu_firmware_set_depth(FuFirmware *self, guint depth)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_FIRMWARE(self));
	priv->depth = depth;
}

/**
 * fu_firmware_get_depth:
 * @self: a #FuPlugin
 *
 * Gets the depth of this child image relative to the root.
 *
 * Returns: integer, or 0 for the root.
 **/
guint
fu_firmware_get_depth(FuFirmware *self)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_FIRMWARE(self), G_MAXUINT);
	return priv->depth;
}

/**
 * fu_firmware_add_image_gtype:
 * @klass: a #FuFirmwareClass
 * @type: a #GType, e.g. %FU_TYPE_ELF_FIRMWARE
 *
 * Adds a possible image GType.
 **/
void
fu_firmware_add_image_gtype(FuFirmwareClass *klass, GType type)
{
	FuFirmwareClassPrivate *cpriv = fu_firmware_get_class_private(klass);

	g_return_if_fail(FU_IS_FIRMWARE_CLASS(klass));
	g_return_if_fail(type != G_TYPE_INVALID);
	g_return_if_fail(cpriv->image_gtypes_cnt < FU_FIRMWARE_IMAGE_GTYPES_MAX);

	g_type_ensure(type);
	cpriv->image_gtypes[cpriv->image_gtypes_cnt++] = type;
}

/**
 * fu_firmware_get_image_gtypes:
 * @self: a #FuFirmware
 * @n_gtypes: (out): the number of GTypes
 *
 * Returns all the possible image GTypes.
 *
 * Returns: (array length=n_gtypes) (transfer none): array of #GType.
 **/
const GType *
fu_firmware_get_image_gtypes(FuFirmware *self, guint *n_gtypes)
{
	FuFirmwareClassPrivate *cpriv;

	g_return_val_if_fail(FU_IS_FIRMWARE(self), NULL);

	cpriv = fu_firmware_get_class_private(FU_FIRMWARE_GET_CLASS(self));
	if (n_gtypes != NULL)
		*n_gtypes = cpriv->image_gtypes_cnt;
	return cpriv->image_gtypes;
}

static gboolean
fu_firmware_check_image_gtype(FuFirmware *self, GType gtype, GError **error)
{
	FuFirmwareClassPrivate *cpriv = fu_firmware_get_class_private(FU_FIRMWARE_GET_CLASS(self));

	/* certain GTypes can opt-out of this check */
	if (fu_firmware_has_flag(self, FU_FIRMWARE_FLAG_NO_IMAGE_TYPE_CHECK))
		return TRUE;

	if (cpriv->image_gtypes_cnt == 0) {
#ifndef SUPPORTED_BUILD
		g_critical("%s did not add image GType %s with fu_firmware_add_image_gtype()",
			   G_OBJECT_TYPE_NAME(self),
			   g_type_name(gtype));
#endif
		return TRUE;
	}
	for (guint i = 0; i < cpriv->image_gtypes_cnt; i++) {
		if (cpriv->image_gtypes[i] == gtype)
			return TRUE;
	}
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "cannot add %s to %s",
		    g_type_name(gtype),
		    G_OBJECT_TYPE_NAME(self));
	return FALSE;
}

/**
 * fu_firmware_add_image:
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
 **/
gboolean
fu_firmware_add_image(FuFirmware *self, FuFirmware *img, GError **error)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_IS_FIRMWARE(self), FALSE);
	g_return_val_if_fail(FU_IS_FIRMWARE(img), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* check depth */
	if (priv->depth > FU_FIRMWARE_IMAGE_DEPTH_MAX) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "images are nested too deep, limit is %u",
			    (guint)FU_FIRMWARE_IMAGE_DEPTH_MAX);
		return FALSE;
	}

	/* check image is a permissible GType */
	if (!fu_firmware_check_image_gtype(self, G_OBJECT_TYPE(img), error))
		return FALSE;

	/* dedupe */
	if (priv->flags & FU_FIRMWARE_FLAG_DEDUPE_ID) {
		for (guint i = 0; i < priv->images->len; i++) {
			FuFirmware *img_tmp = g_ptr_array_index(priv->images, i);
			if (g_strcmp0(fu_firmware_get_id(img_tmp), fu_firmware_get_id(img)) == 0) {
				g_ptr_array_remove_index(priv->images, i);
				break;
			}
		}
	}
	if (priv->flags & FU_FIRMWARE_FLAG_DEDUPE_IDX) {
		for (guint i = 0; i < priv->images->len; i++) {
			FuFirmware *img_tmp = g_ptr_array_index(priv->images, i);
			if (fu_firmware_get_idx(img_tmp) == fu_firmware_get_idx(img)) {
				g_ptr_array_remove_index(priv->images, i);
				break;
			}
		}
	}

	/* sanity check */
	{
		FuFirmwareClassPrivate *cpriv =
		    fu_firmware_get_class_private(FU_FIRMWARE_GET_CLASS(self));
		if (cpriv->images_max > 0 && priv->images->len >= cpriv->images_max) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "too many images, limit is %u",
				    cpriv->images_max);
			return FALSE;
		}
	}

	g_ptr_array_add(priv->images, g_object_ref(img));

	/* set the other way around */
	fu_firmware_set_parent(img, self);
	fu_firmware_set_depth(img, priv->depth + 1);

	/* success */
	return TRUE;
}

/**
 * fu_firmware_set_images_max:
 * @klass: a #FuFirmwareClass
 * @images_max: integer, or 0 for unlimited
 *
 * Sets the maximum number of images this container can hold.
 **/
void
fu_firmware_set_images_max(FuFirmwareClass *klass, guint images_max)
{
	FuFirmwareClassPrivate *cpriv = fu_firmware_get_class_private(klass);
	cpriv->images_max = images_max;
}

/**
 * fu_firmware_get_images_max:
 * @self: a #FuFirmware
 *
 * Gets the maximum number of images this container can hold.
 *
 * Returns: integer, or 0 for unlimited.
 **/
guint
fu_firmware_get_images_max(FuFirmware *self)
{
	FuFirmwareClassPrivate *cpriv;
	g_return_val_if_fail(FU_IS_FIRMWARE(self), G_MAXUINT);
	cpriv = fu_firmware_get_class_private(FU_FIRMWARE_GET_CLASS(self));
	return cpriv->images_max;
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
 * @id: (nullable): image ID, e.g. `config` or `*.mfg|*.elf`
 * @error: (nullable): optional return location for an error
 *
 * Gets the firmware image using the image ID.
 *
 * Returns: (transfer full): a #FuFirmware, or %NULL if the image is not found
 **/
FuFirmware *
fu_firmware_get_image_by_id(FuFirmware *self, const gchar *id, GError **error)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *id_str = NULL;
	g_autoptr(GPtrArray) id_errmsg = g_ptr_array_new();

	g_return_val_if_fail(FU_IS_FIRMWARE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* sanity check */
	if (priv->images->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "no images in firmware");
		return NULL;
	}

	/* non-NULL */
	if (id != NULL) {
		g_auto(GStrv) split = g_strsplit(id, "|", 0);
		for (guint i = 0; i < priv->images->len; i++) {
			FuFirmware *img = g_ptr_array_index(priv->images, i);
			for (guint j = 0; split[j] != NULL; j++) {
				if (fu_firmware_get_id(img) == NULL)
					continue;
				if (g_pattern_match_simple(split[j], fu_firmware_get_id(img)))
					return g_object_ref(img);
			}
		}
	} else {
		for (guint i = 0; i < priv->images->len; i++) {
			FuFirmware *img = g_ptr_array_index(priv->images, i);
			if (fu_firmware_get_id(img) == NULL)
				return g_object_ref(img);
		}
	}

	/* build a useful error */
	for (guint i = 0; i < priv->images->len; i++) {
		FuFirmware *img = g_ptr_array_index(priv->images, i);
		g_ptr_array_add(id_errmsg, (gpointer)fu_firmware_get_id(img));
	}
	id_str = fu_strjoin(",", id_errmsg);
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_FOUND,
		    "no image id %s found in firmware, only have %s",
		    id,
		    id_str);
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
 **/
GBytes *
fu_firmware_get_image_by_id_bytes(FuFirmware *self, const gchar *id, GError **error)
{
	g_autoptr(FuFirmware) img = NULL;

	g_return_val_if_fail(FU_IS_FIRMWARE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	img = fu_firmware_get_image_by_id(self, id, error);
	if (img == NULL)
		return NULL;
	return fu_firmware_write(img, error);
}

/**
 * fu_firmware_get_image_by_id_stream:
 * @self: a #FuPlugin
 * @id: (nullable): image ID, e.g. `config`
 * @error: (nullable): optional return location for an error
 *
 * Gets the firmware image stream using the image ID.
 *
 * Returns: (transfer full): a #FuInputStream of a #FuFirmware, or %NULL if the image is not found
 **/
FuInputStream *
fu_firmware_get_image_by_id_stream(FuFirmware *self, const gchar *id, GError **error)
{
	g_autoptr(FuFirmware) img = NULL;

	g_return_val_if_fail(FU_IS_FIRMWARE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	img = fu_firmware_get_image_by_id(self, id, error);
	if (img == NULL)
		return NULL;
	return fu_firmware_get_stream(img, error);
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
 **/
GBytes *
fu_firmware_get_image_by_idx_bytes(FuFirmware *self, guint64 idx, GError **error)
{
	g_autoptr(FuFirmware) img = NULL;

	g_return_val_if_fail(FU_IS_FIRMWARE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	img = fu_firmware_get_image_by_idx(self, idx, error);
	if (img == NULL)
		return NULL;
	return fu_firmware_write(img, error);
}

/**
 * fu_firmware_get_image_by_idx_stream:
 * @self: a #FuPlugin
 * @idx: image index
 * @error: (nullable): optional return location for an error
 *
 * Gets the firmware image stream using the image index.
 *
 * Returns: (transfer full): a #FuInputStream of a #FuFirmware, or %NULL if the image is not found
 **/
FuInputStream *
fu_firmware_get_image_by_idx_stream(FuFirmware *self, guint64 idx, GError **error)
{
	g_autoptr(FuFirmware) img = NULL;

	g_return_val_if_fail(FU_IS_FIRMWARE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	img = fu_firmware_get_image_by_idx(self, idx, error);
	if (img == NULL)
		return NULL;
	return fu_firmware_get_stream(img, error);
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
 **/
GBytes *
fu_firmware_get_image_by_gtype_bytes(FuFirmware *self, GType gtype, GError **error)
{
	g_autoptr(FuFirmware) img = NULL;

	g_return_val_if_fail(FU_IS_FIRMWARE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	img = fu_firmware_get_image_by_gtype(self, gtype, error);
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

static gint
fu_firmware_sort_by_id_cb(gconstpointer a, gconstpointer b)
{
	FuFirmware *firmware1 = *((FuFirmware **)a);
	FuFirmware *firmware2 = *((FuFirmware **)b);
	return g_strcmp0(fu_firmware_get_id(firmware1), fu_firmware_get_id(firmware2));
}

static gint
fu_firmware_sort_by_idx_cb(gconstpointer a, gconstpointer b)
{
	FuFirmware *firmware1 = *((FuFirmware **)a);
	FuFirmware *firmware2 = *((FuFirmware **)b);
	guint64 idx1 = fu_firmware_get_idx(firmware1);
	guint64 idx2 = fu_firmware_get_idx(firmware2);
	if (idx1 < idx2)
		return -1;
	if (idx1 > idx2)
		return 1;
	return 0;
}

static GPtrArray *
fu_firmware_get_images_sorted(FuFirmware *self)
{
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GPtrArray) images = g_ptr_array_copy(priv->images, (GCopyFunc)g_object_ref, NULL);
	if (priv->flags & FU_FIRMWARE_FLAG_DEDUPE_IDX)
		g_ptr_array_sort(images, fu_firmware_sort_by_idx_cb);
	if (priv->flags & FU_FIRMWARE_FLAG_DEDUPE_ID)
		g_ptr_array_sort(images, fu_firmware_sort_by_id_cb);
	return g_steal_pointer(&images);
}

/**
 * fu_firmware_export:
 * @self: a #FuFirmware
 * @flags: firmware export flags, e.g. %FU_FIRMWARE_EXPORT_FLAG_INCLUDE_DEBUG
 * @json_object: a #FwupdJsonObject
 *
 * This allows us to build a JSON object for the nested firmware.
 *
 * The image children will have a predictable order if the firmware has
 * %FU_FIRMWARE_FLAG_DEDUPE_ID or %FU_FIRMWARE_FLAG_DEDUPE_IDX and
 * %FU_FIRMWARE_EXPORT_FLAG_SORTED is also used in @flags.
 **/
void
fu_firmware_export(FuFirmware *self, FuFirmwareExportFlags flags, FwupdJsonObject *json_object)
{
	FuFirmwareClass *klass = FU_FIRMWARE_GET_CLASS(self);
	FuFirmwarePrivate *priv = GET_PRIVATE(self);
	FuFirmwareFlags flags_for_display = priv->flags;
	const gchar *gtypestr = G_OBJECT_TYPE_NAME(self);

	/* object */
	if (g_strcmp0(gtypestr, "FuFirmware") != 0)
		fwupd_json_object_add_string(json_object, "gtype", gtypestr);

	/* subclassed type */
	flags_for_display &= ~FU_FIRMWARE_FLAG_DONE_PARSE;
	if (flags_for_display != FU_FIRMWARE_FLAG_NONE) {
		g_autofree gchar *str = fu_firmware_flags_to_string(flags_for_display);
		fwupd_json_object_add_string(json_object, "flags", str);
	}
	if (priv->id != NULL)
		fwupd_json_object_add_string(json_object, "id", priv->id);
	if (priv->idx != 0)
		fwupd_json_object_add_integer(json_object, "idx", (gint64)priv->idx);
	if (priv->version != NULL)
		fwupd_json_object_add_string(json_object, "version", priv->version);
	if (priv->version_raw != 0)
		fwupd_json_object_add_integer(json_object,
					      "version_raw",
					      (gint64)priv->version_raw);
	if (priv->version_format != FWUPD_VERSION_FORMAT_UNKNOWN) {
		const gchar *version_format_str =
		    fwupd_version_format_to_string(priv->version_format);
		if (version_format_str != NULL)
			fwupd_json_object_add_string(json_object,
						     "version_format",
						     version_format_str);
	}
	if (priv->addr != 0)
		fwupd_json_object_add_integer(json_object, "addr", (gint64)priv->addr);
	if (priv->offset != 0)
		fwupd_json_object_add_integer(json_object, "offset", (gint64)priv->offset);
	if (priv->alignment != 0)
		fwupd_json_object_add_integer(json_object, "alignment", priv->alignment);
	if (priv->size != 0)
		fwupd_json_object_add_integer(json_object, "size", (gint64)priv->size);
	{
		FuFirmwareClassPrivate *cpriv =
		    fu_firmware_get_class_private(FU_FIRMWARE_GET_CLASS(self));
		if (cpriv->size_max != FU_FIRMWARE_SIZE_MAX_DEFAULT && cpriv->size_max != 0)
			fwupd_json_object_add_integer(json_object,
						      "size_max",
						      (gint64)cpriv->size_max);
	}
	if (priv->filename != NULL)
		fwupd_json_object_add_string(json_object, "filename", priv->filename);
	if (priv->stream != NULL) {
		g_autofree gchar *datastr = NULL;
		if (priv->streamsz <= 0x100) {
			g_autoptr(GByteArray) buf = fu_input_stream_read_byte_array(priv->stream,
										    0x0,
										    priv->streamsz,
										    NULL,
										    NULL);
			if (buf != NULL) {
				if (flags & FU_FIRMWARE_EXPORT_FLAG_ASCII_DATA) {
					datastr = fu_memstrsafe(buf->data,
								buf->len,
								0x0,
								MIN(buf->len, 0x100),
								NULL);
				} else {
					datastr = fu_base64_encode(buf->data, buf->len);
				}
			}
		}
		if (datastr != NULL)
			fwupd_json_object_add_string(json_object, "data", datastr);
		fwupd_json_object_add_string(json_object, "data_type", "FuInputStream");
		fwupd_json_object_add_integer(json_object, "data_size", (gint64)priv->streamsz);
	} else if (priv->bytes != NULL && g_bytes_get_size(priv->bytes) == 0) {
		fwupd_json_object_add_string(json_object, "data_type", "GBytes");
	} else if (priv->bytes != NULL) {
		gsize bufsz = 0;
		const guint8 *buf = g_bytes_get_data(priv->bytes, &bufsz);
		g_autofree gchar *datastr = NULL;
		if (flags & FU_FIRMWARE_EXPORT_FLAG_ASCII_DATA) {
			datastr = fu_memstrsafe(buf, bufsz, 0x0, MIN(bufsz, 0x100), NULL);
		} else {
			datastr = fu_base64_encode(buf, bufsz);
		}
		if (datastr != NULL)
			fwupd_json_object_add_string(json_object, "data", datastr);
		fwupd_json_object_add_string(json_object, "data_type", "GBytes");
		fwupd_json_object_add_integer(json_object, "data_size", (gint64)bufsz);
	}

	/* chunks */
	if (priv->chunks != NULL && priv->chunks->len > 0) {
		g_autoptr(FwupdJsonArray) json_chunks = fwupd_json_array_new();
		for (guint i = 0; i < priv->chunks->len; i++) {
			FuChunk *chk = g_ptr_array_index(priv->chunks, i);
			g_autoptr(FwupdJsonObject) json_chunk = fwupd_json_object_new();
			fu_chunk_export(chk, flags, json_chunk);
			fwupd_json_array_add_object(json_chunks, json_chunk);
		}
		fwupd_json_object_add_array(json_object, "chunks", json_chunks);
	}

	/* magic */
	if (priv->magic != NULL && priv->magic->len > 0) {
		g_autoptr(FwupdJsonArray) json_magic = fwupd_json_array_new();
		for (guint i = 0; i < priv->magic->len; i++) {
			FuFirmwarePatch *patch = g_ptr_array_index(priv->magic, i);
			g_autoptr(FwupdJsonObject) json_patch = fwupd_json_object_new();
			g_autofree gchar *str = fu_bytes_to_string(patch->blob);
			fwupd_json_object_add_string(json_patch, "data", str);
			fwupd_json_object_add_integer(json_patch, "offset", (gint64)patch->offset);
			fwupd_json_array_add_object(json_magic, json_patch);
		}
		fwupd_json_object_add_array(json_object, "magic", json_magic);
	}

	/* image gtypes */
	if ((flags & FU_FIRMWARE_EXPORT_FLAG_INCLUDE_DEBUG) > 0) {
		FuFirmwareClassPrivate *cpriv =
		    fu_firmware_get_class_private(FU_FIRMWARE_GET_CLASS(self));
		if (cpriv->image_gtypes_cnt > 0) {
			g_autoptr(FwupdJsonArray) json_gtypes = fwupd_json_array_new();
			for (guint i = 0; i < cpriv->image_gtypes_cnt; i++) {
				fwupd_json_array_add_string(json_gtypes,
							    g_type_name(cpriv->image_gtypes[i]));
			}
			fwupd_json_object_add_array(json_object, "image_gtypes", json_gtypes);
		}
	}

	/* vfunc */
	if (klass->export != NULL)
		klass->export(self, flags, json_object);

	/* children */
	if (priv->images->len > 0) {
		g_autoptr(GPtrArray) images = flags & FU_FIRMWARE_EXPORT_FLAG_SORTED
						  ? fu_firmware_get_images_sorted(self)
						  : g_ptr_array_ref(priv->images);
		g_autoptr(FwupdJsonArray) json_images = fwupd_json_array_new();
		for (guint i = 0; i < images->len; i++) {
			FuFirmware *img = g_ptr_array_index(images, i);
			g_autoptr(FwupdJsonObject) json_image = fwupd_json_object_new();
			fu_firmware_export(img, flags, json_image);
			fwupd_json_array_add_object(json_images, json_image);
		}
		fwupd_json_object_add_array(json_object, "firmware", json_images);
	}
}

/**
 * fu_firmware_export_to_json:
 * @self: a #FuFirmware
 * @flags: firmware export flags, e.g. %FU_FIRMWARE_EXPORT_FLAG_INCLUDE_DEBUG
 * @error: (nullable): optional return location for an error
 *
 * Exports the nested firmware as JSON text.
 *
 * Returns: a string value, or %NULL for invalid.
 **/
gchar *
fu_firmware_export_to_json(FuFirmware *self, FuFirmwareExportFlags flags, GError **error)
{
	g_autoptr(FwupdJsonObject) json_object = fwupd_json_object_new();
	g_autoptr(GString) str = NULL;

	g_return_val_if_fail(FU_IS_FIRMWARE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	fu_firmware_export(self, flags, json_object);
	str = fwupd_json_object_to_string(json_object, FWUPD_JSON_EXPORT_FLAG_INDENT);
	return g_string_free(g_steal_pointer(&str), FALSE);
}

/**
 * fu_firmware_to_string:
 * @self: a #FuFirmware
 *
 * This allows us to easily print the object.
 *
 * Returns: a string value, or %NULL for invalid.
 **/
gchar *
fu_firmware_to_string(FuFirmware *self)
{
	g_autoptr(FwupdJsonObject) json_object = fwupd_json_object_new();
	g_autoptr(GString) str = NULL;
	fu_firmware_export(self,
			   FU_FIRMWARE_EXPORT_FLAG_INCLUDE_DEBUG |
			       FU_FIRMWARE_EXPORT_FLAG_ASCII_DATA,
			   json_object);
	str = fwupd_json_object_to_string(json_object, FWUPD_JSON_EXPORT_FLAG_INDENT);
	return g_string_free(g_steal_pointer(&str), FALSE);
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
fu_firmware_constructed(GObject *obj)
{
	FuFirmware *self = FU_FIRMWARE(obj);
	FuFirmwareClass *klass = FU_FIRMWARE_GET_CLASS(self);
	if (klass->add_magic != NULL)
		klass->add_magic(self);
	if (G_TYPE_FROM_CLASS(klass) != FU_TYPE_FIRMWARE && klass->parse_full == NULL &&
	    klass->parse == NULL)
		fu_firmware_add_flag(self, FU_FIRMWARE_FLAG_IS_ABSTRACT);

	/* chain up to parent */
	G_OBJECT_CLASS(fu_firmware_parent_class)->constructed(obj);
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
	if (priv->stream != NULL)
		g_object_unref(priv->stream);
	if (priv->chunks != NULL)
		g_ptr_array_unref(priv->chunks);
	if (priv->patches != NULL)
		g_ptr_array_unref(priv->patches);
	if (priv->magic != NULL)
		g_ptr_array_unref(priv->magic);
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
	object_class->constructed = fu_firmware_constructed;
	fu_firmware_set_size_max(klass, FU_FIRMWARE_SIZE_MAX_DEFAULT);

	/**
	 * FuFirmware:parent:
	 *
	 * The firmware parent.
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
 * @stream: a #FuInputStream
 * @offset: start offset, useful for ignoring a bootloader
 * @flags: install flags, e.g. %FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM
 * @error: (nullable): optional return location for an error
 * @...: an array of #GTypes, ending with %G_TYPE_INVALID
 *
 * Tries to parse the firmware with each #GType in order.
 *
 * Returns: (transfer full) (nullable): a #FuFirmware, or %NULL
 **/
FuFirmware *
fu_firmware_new_from_gtypes(FuInputStream *stream,
			    gsize offset,
			    FuFirmwareParseFlags flags,
			    GError **error,
			    ...)
{
	va_list args;
	g_autoptr(GArray) gtypes = g_array_new(FALSE, FALSE, sizeof(GType));
	g_autoptr(GError) error_all = NULL;

	g_return_val_if_fail(FWUPD_IS_INPUT_STREAM(stream), NULL);
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
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "no GTypes specified");
		return NULL;
	}

	/* try each GType in turn */
	for (guint i = 0; i < gtypes->len; i++) {
		GType gtype = g_array_index(gtypes, GType, i);
		g_autoptr(FuFirmware) firmware = g_object_new(gtype, NULL);
		g_autoptr(GError) error_local = NULL;
		if (!fu_firmware_parse_stream(firmware, stream, offset, flags, &error_local)) {
			g_debug("@0x%x %s", (guint)offset, error_local->message);
			if (error_all == NULL) {
				g_propagate_error(&error_all, g_steal_pointer(&error_local));
			} else {
				/* nocheck:error */
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

/**
 * fu_firmware_roundtrip_from_json:
 * @builder_json: (not nullable): JSON describing the firmware
 * @checksum_expected: (nullable): Expected SHA1 hash of the built binary blob
 * @flags: #FuFirmwareBuilderFlags, e.g. %FU_FIRMWARE_BUILDER_FLAG_NO_BINARY_COMPARE
 * @error: (nullable): optional return location for an error
 *
 * Roundtrip a firmware from builder JSON to binary and back to JSON.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_firmware_roundtrip_from_json(const gchar *builder_json,
				const gchar *checksum_expected,
				FuFirmwareBuilderFlags flags,
				GError **error)
{
	g_autofree gchar *csum1 = NULL;
	g_autofree gchar *csum2 = NULL;
	g_autofree gchar *json_out = NULL;
	g_autoptr(FuFirmware) firmware1 = NULL;
	g_autoptr(FuFirmware) firmware2 = NULL;

	g_return_val_if_fail(builder_json != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* build and write */
	firmware1 = fu_firmware_new_from_json(builder_json, error);
	if (firmware1 == NULL) {
		g_prefix_error(error, "failed to build %s: ", builder_json);
		return FALSE;
	}
	csum1 = fu_firmware_get_checksum(firmware1, G_CHECKSUM_SHA1, error);
	if (csum1 == NULL)
		return FALSE;
	if (checksum_expected != NULL && g_strcmp0(csum1, checksum_expected) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "got checksum %s and expected %s",
			    csum1,
			    checksum_expected);
		return FALSE;
	}

	/* ensure we can write and then parse what we just wrote */
	if ((flags & FU_FIRMWARE_BUILDER_FLAG_NO_WRITE) == 0) {
		g_autoptr(FuFirmware) firmware3 =
		    g_object_new(G_TYPE_FROM_INSTANCE(firmware1), NULL);
		g_autoptr(GBytes) fw = NULL;

		fw = fu_firmware_write(firmware1, error);
		if (fw == NULL) {
			g_prefix_error(error, "failed to write %s: ", builder_json);
			return FALSE;
		}
		if (!fu_firmware_parse_bytes(firmware3,
					     fw,
					     0x0,
					     FU_FIRMWARE_PARSE_FLAG_NO_SEARCH |
						 FU_FIRMWARE_PARSE_FLAG_CACHE_STREAM,
					     error)) {
			g_prefix_error(error, "failed to parse %s: ", builder_json);
			return FALSE;
		}
		if ((flags & FU_FIRMWARE_BUILDER_FLAG_NO_BINARY_COMPARE) == 0) {
			g_autoptr(GBytes) fw2 = NULL;
			fw2 = fu_firmware_write(firmware3, error);
			if (fw2 == NULL) {
				g_prefix_error(error, "failed to write %s: ", builder_json);
				return FALSE;
			}
			if (!fu_bytes_compare(fw2, fw, error)) {
				g_prefix_error(error, "failed to compare %s: ", builder_json);
				return FALSE;
			}
		}
	}

	/* ensure we can round-trip */
	json_out = fu_firmware_export_to_json(firmware1, FU_FIRMWARE_EXPORT_FLAG_NONE, error);
	if (json_out == NULL) {
		g_prefix_error(error, "cannot export %s: ", builder_json);
		return FALSE;
	}
	firmware2 = fu_firmware_new_from_json(json_out, error);
	if (firmware2 == NULL)
		return FALSE;
	csum2 = fu_firmware_get_checksum(firmware2, G_CHECKSUM_SHA1, error);
	if (csum2 == NULL)
		return FALSE;
	if (checksum_expected != NULL && g_strcmp0(csum2, checksum_expected) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "got round-trip checksum %s and expected %s",
			    csum2,
			    checksum_expected);
		return FALSE;
	}

	/* success */
	return TRUE;
}

/**
 * fu_firmware_roundtrip_from_filename:
 * @builder_fn: (not nullable): JSON filename describing the firmware
 * @checksum_expected: (nullable): Expected SHA1 hash of the built binary blob
 * @flags: #FuFirmwareBuilderFlags, e.g. %FU_FIRMWARE_BUILDER_FLAG_NO_BINARY_COMPARE
 * @error: (nullable): optional return location for an error
 *
 * Roundtrip a firmware from builder JSON filename to binary and back to JSON.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_firmware_roundtrip_from_filename(const gchar *builder_fn,
				    const gchar *checksum_expected,
				    FuFirmwareBuilderFlags flags,
				    GError **error)
{
	g_autofree gchar *json = NULL;

	g_return_val_if_fail(builder_fn != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!g_file_get_contents(builder_fn, &json, NULL, error)) {
		fwupd_error_convert(error);
		return FALSE;
	}
	return fu_firmware_roundtrip_from_json(json, checksum_expected, flags, error);
}

static gboolean
fu_firmware_fuzzer_test_input(FuFuzzer *fuzzer, GByteArray *buf, GError **error)
{
	FuFirmware *self = FU_FIRMWARE(fuzzer);
	g_autoptr(GByteArray) buf_out = NULL;
	g_autoptr(FwupdMemoryInputStream) stream = NULL;

	stream = fwupd_memory_input_stream_new_from_data(buf->data, buf->len, NULL);
	if (!fu_firmware_parse_stream(
		self,
		FWUPD_INPUT_STREAM(stream),
		0x0,
		FU_FIRMWARE_PARSE_FLAG_NO_SEARCH | FU_FIRMWARE_PARSE_FLAG_CACHE_BLOB |
		    FU_FIRMWARE_PARSE_FLAG_IGNORE_VID_PID | FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM,
		error))
		return FALSE;
	buf_out = fu_firmware_write_array(self, error);
	if (buf_out == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

static GByteArray *
fu_firmware_fuzzer_build_example(FuFuzzer *fuzzer, GBytes *blob, GError **error)
{
	FuFirmware *self = FU_FIRMWARE(fuzzer);
	g_autoptr(FwupdJsonObject) json_object = NULL;

	/* sanity check */
	if (blob == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "builder JSON required");
		return NULL;
	}

	/* parse JSON */
	json_object = fu_firmware_builder_load_json(blob, error);
	if (json_object == NULL)
		return NULL;
	if (!fu_firmware_build(self, json_object, error))
		return NULL;
	return fu_firmware_write_array(self, error);
}

static void
fu_firmware_fuzzer_iface_init(FuFuzzerInterface *iface)
{
	iface->test_input = fu_firmware_fuzzer_test_input;
	iface->build_example = fu_firmware_fuzzer_build_example;
}
