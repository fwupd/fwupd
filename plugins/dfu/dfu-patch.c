/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
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

/**
 * SECTION:dfu-patch
 * @short_description: Object representing a binary patch
 *
 * This object represents an binary patch that can be applied on a firmware
 * image. The patch itself is made up of chunks of data that have an offset
 * and that can replace the data to upgrade the firmware.
 *
 * Note: this is one way operation -- the patch can only be used to go forwards
 * and also cannot be used to truncate the existing image.
 *
 * See also: #DfuImage, #DfuFirmware
 */

#include "config.h"

#include <string.h>
#include <stdio.h>

#include "dfu-common.h"
#include "dfu-patch.h"

#include "fwupd-error.h"

static void dfu_patch_finalize			 (GObject *object);

typedef struct __attribute__((packed)) {
	guint32			 off;
	guint32			 sz;
	guint32			 flags;
} DfuPatchChunkHeader;

typedef struct __attribute__((packed)) {
	guint8			 signature[4];		/* 'DfuP' */
	guint8			 reserved[4];
	guint8			 checksum_old[20];	/* SHA1 */
	guint8			 checksum_new[20];	/* SHA1 */
} DfuPatchFileHeader;

typedef struct {
	GBytes			*checksum_old;
	GBytes			*checksum_new;
	GPtrArray		*chunks;		/* of DfuPatchChunk */
} DfuPatchPrivate;

typedef struct {
	guint32			 off;
	GBytes			*blob;
} DfuPatchChunk;

G_DEFINE_TYPE_WITH_PRIVATE (DfuPatch, dfu_patch, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (dfu_patch_get_instance_private (o))

static void
dfu_patch_class_init (DfuPatchClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = dfu_patch_finalize;
}

static void
dfu_patch_chunk_free (DfuPatchChunk *chunk)
{
	g_bytes_unref (chunk->blob);
	g_free (chunk);
}

static void
dfu_patch_init (DfuPatch *self)
{
	DfuPatchPrivate *priv = GET_PRIVATE (self);
	priv->chunks = g_ptr_array_new_with_free_func ((GDestroyNotify) dfu_patch_chunk_free);
}

static void
dfu_patch_finalize (GObject *object)
{
	DfuPatch *self = DFU_PATCH (object);
	DfuPatchPrivate *priv = GET_PRIVATE (self);

	if (priv->checksum_old != NULL)
		g_bytes_unref (priv->checksum_old);
	if (priv->checksum_new != NULL)
		g_bytes_unref (priv->checksum_new);
	g_ptr_array_unref (priv->chunks);

	G_OBJECT_CLASS (dfu_patch_parent_class)->finalize (object);
}

/**
 * dfu_patch_export:
 * @self: a #DfuPatch
 * @error: a #GError, or %NULL
 *
 * Converts the patch to a binary blob that can be stored as a file.
 *
 * Return value: (transfer full): blob
 **/
GBytes *
dfu_patch_export (DfuPatch *self, GError **error)
{
	DfuPatchPrivate *priv = GET_PRIVATE (self);
	gsize addr;
	gsize sz;
	guint8 *data;

	g_return_val_if_fail (DFU_IS_PATCH (self), NULL);

	/* check we have something to write */
	if (priv->chunks->len == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no chunks to process");
		return NULL;
	}

	/* calculate the size of the new blob */
	sz = sizeof(DfuPatchFileHeader);
	for (guint i = 0; i < priv->chunks->len; i++) {
		DfuPatchChunk *chunk = g_ptr_array_index (priv->chunks, i);
		sz += sizeof(DfuPatchChunkHeader) + g_bytes_get_size (chunk->blob);
	}
	g_debug ("blob size is %" G_GSIZE_FORMAT, sz);

	/* actually allocate and fill in the blob */
	data = g_malloc0 (sz);
	memcpy (data, "DfuP", 4);

	/* add checksums */
	if (priv->checksum_old != NULL) {
		gsize csum_sz = 0;
		const guint8 *csum_data = g_bytes_get_data (priv->checksum_old, &csum_sz);
		memcpy (data + G_STRUCT_OFFSET(DfuPatchFileHeader,checksum_old),
			csum_data, csum_sz);
	}
	if (priv->checksum_new != NULL) {
		gsize csum_sz = 0;
		const guint8 *csum_data = g_bytes_get_data (priv->checksum_new, &csum_sz);
		memcpy (data + G_STRUCT_OFFSET(DfuPatchFileHeader,checksum_new),
			csum_data, csum_sz);
	}

	addr = sizeof(DfuPatchFileHeader);
	for (guint i = 0; i < priv->chunks->len; i++) {
		DfuPatchChunk *chunk = g_ptr_array_index (priv->chunks, i);
		DfuPatchChunkHeader chunkhdr;
		gsize sz_tmp = 0;
		const guint8 *data_new = g_bytes_get_data (chunk->blob, &sz_tmp);

		/* build chunk header and append data */
		chunkhdr.off = GUINT32_TO_LE (chunk->off);
		chunkhdr.sz = GUINT32_TO_LE (sz_tmp);
		chunkhdr.flags = 0;
		memcpy (data + addr, &chunkhdr, sizeof(DfuPatchChunkHeader));
		memcpy (data + addr + sizeof(DfuPatchChunkHeader), data_new, sz_tmp);

		/* move up after the copied data */
		addr += sizeof(DfuPatchChunkHeader) + sz_tmp;
	}
	return g_bytes_new_take (data, sz);

}

/**
 * dfu_patch_import:
 * @self: a #DfuPatch
 * @blob: patch data
 * @error: a #GError, or %NULL
 *
 * Creates a patch from a serialized patch, possibly from a file.
 *
 * Return value: %TRUE on success
 **/
gboolean
dfu_patch_import (DfuPatch *self, GBytes *blob, GError **error)
{
	DfuPatchPrivate *priv = GET_PRIVATE (self);
	const guint8 *data;
	gsize sz = 0;
	guint32 off;

	g_return_val_if_fail (DFU_IS_PATCH (self), FALSE);
	g_return_val_if_fail (blob != NULL, FALSE);

	/* cannot reuse object */
	if (priv->chunks->len > 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "patch has already been loaded");
		return FALSE;
	}

	/* check minimum size */
	data = g_bytes_get_data (blob, &sz);
	if (sz < sizeof(DfuPatchFileHeader) + sizeof(DfuPatchChunkHeader) + 1) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "file is too small");
		return FALSE;
	}

	/* check header */
	if (memcmp (data, "DfuP", 4) != 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "header signature is not correct");
		return FALSE;
	}

	/* get checksums */
	priv->checksum_old = g_bytes_new (data + G_STRUCT_OFFSET(DfuPatchFileHeader,checksum_old), 20);
	priv->checksum_new = g_bytes_new (data + G_STRUCT_OFFSET(DfuPatchFileHeader,checksum_new), 20);

	/* look for each chunk */
	off = sizeof(DfuPatchFileHeader);
	while (off < (guint32) sz) {
		DfuPatchChunkHeader *chunkhdr = (DfuPatchChunkHeader *) (data + off);
		DfuPatchChunk *chunk;
		guint32 chunk_sz = GUINT32_FROM_LE (chunkhdr->sz);
		guint32 chunk_off = GUINT32_FROM_LE (chunkhdr->off);

		/* check chunk size, assuming it can overflow */
		if (chunk_sz > sz || off + chunk_sz > sz) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "chunk offset 0x%04x outsize file size 0x%04x",
				     (guint) (off + chunk_sz), (guint) sz);
			return FALSE;
		}
		chunk = g_new0 (DfuPatchChunk, 1);
		chunk->off = chunk_off;
		chunk->blob = g_bytes_new_from_bytes (blob, off + sizeof(DfuPatchChunkHeader), chunk_sz);
		g_ptr_array_add (priv->chunks, chunk);
		off += sizeof(DfuPatchChunkHeader) + chunk_sz;
	}

	/* check we finished properly */
	if (off != sz) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "blob chunk sizes did not sum to total");
		return FALSE;
	}

	/* success */
	return TRUE;
}


static GBytes *
dfu_patch_calculate_checksum (GBytes *blob)
{
	const guchar *data;
	gsize digest_len = 20;
	gsize sz = 0;
	guint8 *buf = g_malloc0 (digest_len);
	g_autoptr(GChecksum) csum = NULL;
	csum = g_checksum_new (G_CHECKSUM_SHA1);
	data = g_bytes_get_data (blob, &sz);
	g_checksum_update (csum, data, (gssize) sz);
	g_checksum_get_digest (csum, buf, &digest_len);
	return g_bytes_new_take (buf, digest_len);
}

typedef struct {
	guint32			 diff_start;
	guint32			 diff_end;
	GBytes			*blob; /* no ref */
} DfuPatchCreateHelper;

static void
dfu_patch_flush (DfuPatch *self, DfuPatchCreateHelper *helper)
{
	DfuPatchChunk *chunk;
	DfuPatchPrivate *priv = GET_PRIVATE (self);

	if (helper->diff_end == 0xffff)
		return;
	g_debug ("add chunk @0x%04x (len %" G_GUINT32_FORMAT ")",
		 (guint) helper->diff_start, helper->diff_end - helper->diff_start + 1);

	chunk = g_new0 (DfuPatchChunk, 1);
	chunk->off = helper->diff_start;
	chunk->blob = g_bytes_new_from_bytes (helper->blob, chunk->off,
					      helper->diff_end - helper->diff_start + 1);
	g_ptr_array_add (priv->chunks, chunk);
	helper->diff_end = 0xffff;
}

/**
 * dfu_patch_create:
 * @self: a #DfuPatch
 * @blob1: a #GBytes, typically the old firmware image
 * @blob2: a #GBytes, typically the new firmware image
 * @error: a #GError, or %NULL
 *
 * Creates a patch from two blobs of memory.
 *
 * The blobs should ideally be the same size. If @blob2 is has grown in size
 * the binary diff will still work but the algorithm will probably not perform
 * well unless the majority of data has just been appended.
 *
 * As an additional constrainst, @blob2 cannot be smaller than @blob1, i.e.
 * the firmware cannot be truncated by this format.
 *
 * Return value: %TRUE on success
 **/
gboolean
dfu_patch_create (DfuPatch *self, GBytes *blob1, GBytes *blob2, GError **error)
{
	DfuPatchPrivate *priv = GET_PRIVATE (self);
	DfuPatchCreateHelper helper;
	const guint8 *data1;
	const guint8 *data2;
	gsize sz1 = 0;
	gsize sz2 = 0;
	guint32 same_sz = 0;

	g_return_val_if_fail (DFU_IS_PATCH (self), FALSE);
	g_return_val_if_fail (blob1 != NULL, FALSE);
	g_return_val_if_fail (blob2 != NULL, FALSE);

	/* are the blobs the same */
	if (g_bytes_equal (blob1, blob2)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "old and new binaries are the same");
		return FALSE;
	}

	/* cannot reuse object */
	if (priv->chunks->len > 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "patch has already been loaded");
		return FALSE;
	}

	/* get the hash of the old firmware file */
	priv->checksum_old = dfu_patch_calculate_checksum (blob1);
	priv->checksum_new = dfu_patch_calculate_checksum (blob2);

	/* get the raw data, and ensure they are the same size */
	data1 = g_bytes_get_data (blob1, &sz1);
	data2 = g_bytes_get_data (blob2, &sz2);
	if (sz1 > sz2) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "firmware binary cannot go down, got "
			     "%" G_GSIZE_FORMAT " and %" G_GSIZE_FORMAT,
			     sz1, sz2);
		return FALSE;
	}
	if (sz1 == sz2) {
		g_debug ("binary staying same size: %" G_GSIZE_FORMAT, sz1);
	} else {
		g_debug ("binary growing from: %" G_GSIZE_FORMAT
			 " to %" G_GSIZE_FORMAT, sz1, sz2);
	}

	/* start the dumb comparison algorithm */
	helper.diff_start = 0;
	helper.diff_end = 0xffff;
	helper.blob = blob2;
	for (gsize i = 0; i < sz1 || i < sz2; i++) {
		if (i < sz1 && i < sz2 &&
		    data1[i] == data2[i]) {
			/* if we got enough the same, dump what is pending */
			if (++same_sz > sizeof(DfuPatchChunkHeader) * 2)
				dfu_patch_flush (self, &helper);
			continue;
		}
		if (helper.diff_end == 0xffff)
			helper.diff_start = (guint32) i;
		helper.diff_end = (guint32) i;
		same_sz = 0;
	}
	dfu_patch_flush (self, &helper);
	return TRUE;
}

static gchar *
_g_bytes_to_string (GBytes *blob)
{
	gsize sz = 0;
	const guint8 *data = g_bytes_get_data (blob, &sz);
	GString *str = g_string_new (NULL);
	for (gsize i = 0; i < sz; i++)
		g_string_append_printf (str, "%02x", (guint) data[i]);
	return g_string_free (str, FALSE);
}

/**
 * dfu_patch_get_checksum_old:
 * @self: a #DfuPatch
 *
 * Get the checksum for the old firmware image.
 *
 * Return value: A #GBytes, or %NULL if nothing has been loaded.
 **/
GBytes *
dfu_patch_get_checksum_old (DfuPatch *self)
{
	DfuPatchPrivate *priv = GET_PRIVATE (self);
	return priv->checksum_old;
}

/**
 * dfu_patch_get_checksum_new:
 * @self: a #DfuPatch
 *
 * Get the checksum for the new firmware image.
 *
 * Return value: A #GBytes, or %NULL if nothing has been loaded.
 **/
GBytes *
dfu_patch_get_checksum_new (DfuPatch *self)
{
	DfuPatchPrivate *priv = GET_PRIVATE (self);
	return priv->checksum_new;
}

/**
 * dfu_patch_apply:
 * @self: a #DfuPatch
 * @blob: a #GBytes, typically the old firmware image
 * @flags: a #DfuPatchApplyFlags, e.g. %DFU_PATCH_APPLY_FLAG_IGNORE_CHECKSUM
 * @error: a #GError, or %NULL
 *
 * Apply the currently loaded patch to a new firmware image.
 *
 * Return value: A #GBytes, typically saved as the new firmware file
 **/
GBytes *
dfu_patch_apply (DfuPatch *self, GBytes *blob, DfuPatchApplyFlags flags, GError **error)
{
	DfuPatchPrivate *priv = GET_PRIVATE (self);
	const guint8 *data_old;
	gsize sz;
	gsize sz_max = 0;
	g_autofree guint8 *data_new = NULL;
	g_autoptr(GBytes) blob_checksum_new = NULL;
	g_autoptr(GBytes) blob_checksum = NULL;
	g_autoptr(GBytes) blob_new = NULL;

	/* not loaded yet */
	if (priv->chunks->len == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no patches loaded");
		return NULL;
	}

	/* get the hash of the old firmware file */
	blob_checksum = dfu_patch_calculate_checksum (blob);
	if ((flags & DFU_PATCH_APPLY_FLAG_IGNORE_CHECKSUM) == 0 &&
	    !g_bytes_equal (blob_checksum, priv->checksum_old)) {
		g_autofree gchar *actual = _g_bytes_to_string (blob_checksum);
		g_autofree gchar *expect = _g_bytes_to_string (priv->checksum_old);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "checksum for source did not match, expected %s, got %s",
			     expect, actual);
		return NULL;
	}

	/* get the size of the new image size */
	for (guint i = 0; i < priv->chunks->len; i++) {
		DfuPatchChunk *chunk = g_ptr_array_index (priv->chunks, i);
		gsize chunk_sz = g_bytes_get_size (chunk->blob);
		if (chunk->off + chunk_sz > sz_max)
			sz_max = chunk->off + chunk_sz;
	}

	/* first, copy the data buffer */
	data_old = g_bytes_get_data (blob, &sz);
	if (sz_max < sz) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "binary patch cannot truncate binary");
		return NULL;
	}
	if (sz == sz_max) {
		g_debug ("binary staying same size: %" G_GSIZE_FORMAT, sz);
	} else {
		g_debug ("binary growing from: %" G_GSIZE_FORMAT
			 " to %" G_GSIZE_FORMAT, sz, sz_max);
	}

	data_new = g_malloc0 (sz_max);
	memcpy (data_new, data_old, sz_max);
	for (guint i = 0; i < priv->chunks->len; i++) {
		DfuPatchChunk *chunk = g_ptr_array_index (priv->chunks, i);
		const guint8 *chunk_data;
		gsize chunk_sz;

		/* bigger than the total size */
		chunk_data = g_bytes_get_data (chunk->blob, &chunk_sz);
		if (chunk->off + chunk_sz > sz_max) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "cannot apply chunk as larger than max size");
			return NULL;
		}

		/* apply one chunk */
		g_debug ("applying chunk %u/%u @0x%04x (length %" G_GSIZE_FORMAT ")",
			 i + 1, priv->chunks->len, chunk->off, chunk_sz);
		memcpy (data_new + chunk->off, chunk_data, chunk_sz);
	}

	/* check we got the desired hash */
	blob_new = g_bytes_new (data_new, sz_max);
	blob_checksum_new = dfu_patch_calculate_checksum (blob_new);
	if ((flags & DFU_PATCH_APPLY_FLAG_IGNORE_CHECKSUM) == 0 &&
	    !g_bytes_equal (blob_checksum_new, priv->checksum_new)) {
		g_autofree gchar *actual = _g_bytes_to_string (blob_checksum_new);
		g_autofree gchar *expect = _g_bytes_to_string (priv->checksum_new);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "checksum for result did not match, expected %s, got %s",
			     expect, actual);
		return NULL;
	}

	/* success */
	return g_steal_pointer (&blob_new);
}

/**
 * dfu_patch_to_string:
 * @self: a #DfuPatch
 *
 * Returns a string representaiton of the object.
 *
 * Return value: NULL terminated string, or %NULL for invalid
 **/
gchar *
dfu_patch_to_string (DfuPatch *self)
{
	DfuPatchPrivate *priv = GET_PRIVATE (self);
	GString *str = g_string_new (NULL);
	g_autofree gchar *checksum_old = NULL;
	g_autofree gchar *checksum_new = NULL;

	g_return_val_if_fail (DFU_IS_PATCH (self), NULL);

	/* add checksums */
	checksum_old = _g_bytes_to_string (priv->checksum_old);
	g_string_append_printf (str, "checksum-old: %s\n", checksum_old);
	checksum_new = _g_bytes_to_string (priv->checksum_new);
	g_string_append_printf (str, "checksum-new: %s\n", checksum_new);

	/* add chunks */
	for (guint i = 0; i < priv->chunks->len; i++) {
		DfuPatchChunk *chunk = g_ptr_array_index (priv->chunks, i);
		g_string_append_printf (str, "chunk #%02u     0x%04x, length %" G_GSIZE_FORMAT "\n",
					i, chunk->off, g_bytes_get_size (chunk->blob));
	}
	g_string_truncate (str, str->len - 1);
	return g_string_free (str, FALSE);
}

/**
 * dfu_patch_new:
 *
 * Creates a new DFU patch object.
 *
 * Return value: a new #DfuPatch
 **/
DfuPatch *
dfu_patch_new (void)
{
	DfuPatch *self;
	self = g_object_new (DFU_TYPE_PATCH, NULL);
	return self;
}
