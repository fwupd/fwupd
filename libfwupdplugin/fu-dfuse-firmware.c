/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include <string.h>

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-chunk-private.h"
#include "fu-common.h"
#include "fu-dfu-firmware-private.h"
#include "fu-dfu-struct.h"
#include "fu-dfuse-firmware.h"

/**
 * FuDfuseFirmware:
 *
 * A DfuSe firmware image.
 *
 * See also: [class@FuDfuFirmware]
 */

G_DEFINE_TYPE(FuDfuseFirmware, fu_dfuse_firmware, FU_TYPE_DFU_FIRMWARE)

static FuChunk *
fu_firmware_image_chunk_parse(FuDfuseFirmware *self, GBytes *bytes, gsize *offset, GError **error)
{
	gsize bufsz = 0;
	gsize ftrlen = fu_dfu_firmware_get_footer_len(FU_DFU_FIRMWARE(self));
	const guint8 *buf = g_bytes_get_data(bytes, &bufsz);
	g_autoptr(FuChunk) chk = NULL;
	g_autoptr(GByteArray) st_ele = NULL;
	g_autoptr(GBytes) blob = NULL;

	/* create new chunk */
	st_ele = fu_struct_dfuse_element_parse(buf, bufsz - ftrlen, *offset, error);
	if (st_ele == NULL)
		return NULL;
	*offset += st_ele->len;
	blob = fu_bytes_new_offset(bytes, *offset, fu_struct_dfuse_element_get_size(st_ele), error);
	if (blob == NULL)
		return NULL;
	chk = fu_chunk_bytes_new(blob);
	fu_chunk_set_address(chk, fu_struct_dfuse_element_get_address(st_ele));
	*offset += fu_chunk_get_data_sz(chk);

	/* success */
	return g_steal_pointer(&chk);
}

static FuFirmware *
fu_dfuse_firmware_image_parse(FuDfuseFirmware *self, GBytes *bytes, gsize *offset, GError **error)
{
	gsize bufsz = 0;
	guint chunks;
	const guint8 *buf = g_bytes_get_data(bytes, &bufsz);
	g_autoptr(FuFirmware) image = fu_firmware_new();
	g_autoptr(GByteArray) st_img = NULL;

	/* verify image signature */
	st_img = fu_struct_dfuse_image_parse(buf, bufsz, *offset, error);
	if (st_img == NULL)
		return NULL;

	/* set properties */
	fu_firmware_set_idx(image, fu_struct_dfuse_image_get_alt_setting(st_img));
	if (fu_struct_dfuse_image_get_target_named(st_img) == 0x01) {
		g_autofree gchar *target_name = fu_struct_dfuse_image_get_target_name(st_img);
		fu_firmware_set_id(image, target_name);
	}

	/* no chunks */
	chunks = fu_struct_dfuse_image_get_chunks(st_img);
	if (chunks == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "DfuSe image has no chunks");
		return NULL;
	}

	/* parse chunks */
	*offset += st_img->len;
	for (guint j = 0; j < chunks; j++) {
		g_autoptr(FuChunk) chk = NULL;
		chk = fu_firmware_image_chunk_parse(self, bytes, offset, error);
		if (chk == NULL)
			return NULL;
		fu_firmware_add_chunk(image, chk);
	}

	/* success */
	return g_steal_pointer(&image);
}

static gboolean
fu_dfuse_firmware_check_magic(FuFirmware *firmware, GBytes *fw, gsize offset, GError **error)
{
	return fu_struct_dfuse_hdr_validate(g_bytes_get_data(fw, NULL),
					    g_bytes_get_size(fw),
					    offset,
					    error);
}

static gboolean
fu_dfuse_firmware_parse(FuFirmware *firmware,
			GBytes *fw,
			gsize offset,
			FwupdInstallFlags flags,
			GError **error)
{
	FuDfuFirmware *dfu_firmware = FU_DFU_FIRMWARE(firmware);
	gsize bufsz = 0;
	guint8 targets = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autoptr(GByteArray) st_hdr = NULL;

	/* DFU footer first */
	if (!fu_dfu_firmware_parse_footer(dfu_firmware, fw, flags, error))
		return FALSE;

	/* parse */
	st_hdr = fu_struct_dfuse_hdr_parse(buf, bufsz, offset, error);
	if (st_hdr == NULL)
		return FALSE;

	/* check image size */
	if (fu_struct_dfuse_hdr_get_image_size(st_hdr) !=
	    bufsz - fu_dfu_firmware_get_footer_len(dfu_firmware)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "invalid DfuSe image size, "
			    "got %" G_GUINT32_FORMAT ", "
			    "expected %" G_GSIZE_FORMAT,
			    fu_struct_dfuse_hdr_get_image_size(st_hdr),
			    bufsz - fu_dfu_firmware_get_footer_len(dfu_firmware));
		return FALSE;
	}

	/* parse the image targets */
	targets = fu_struct_dfuse_hdr_get_targets(st_hdr);
	offset += st_hdr->len;
	for (guint i = 0; i < targets; i++) {
		g_autoptr(FuFirmware) image = NULL;
		image =
		    fu_dfuse_firmware_image_parse(FU_DFUSE_FIRMWARE(firmware), fw, &offset, error);
		if (image == NULL)
			return FALSE;
		if (!fu_firmware_add_image_full(firmware, image, error))
			return FALSE;
	}
	return TRUE;
}

static GBytes *
fu_firmware_chunk_write(FuDfuseFirmware *self, FuChunk *chk)
{
	g_autoptr(GByteArray) st_ele = fu_struct_dfuse_element_new();
	fu_struct_dfuse_element_set_address(st_ele, fu_chunk_get_address(chk));
	fu_struct_dfuse_element_set_size(st_ele, fu_chunk_get_data_sz(chk));
	g_byte_array_append(st_ele, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));
	return g_bytes_new(st_ele->data, st_ele->len);
}

static GBytes *
fu_dfuse_firmware_write_image(FuDfuseFirmware *self, FuFirmware *image, GError **error)
{
	gsize totalsz = 0;
	g_autoptr(GByteArray) st_img = fu_struct_dfuse_image_new();
	g_autoptr(GPtrArray) blobs = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* get total size */
	blobs = g_ptr_array_new_with_free_func((GDestroyNotify)g_bytes_unref);
	chunks = fu_firmware_get_chunks(image, error);
	if (chunks == NULL)
		return NULL;
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		GBytes *bytes = fu_firmware_chunk_write(self, chk);
		g_ptr_array_add(blobs, bytes);
		totalsz += g_bytes_get_size(bytes);
	}

	/* add prefix */
	fu_struct_dfuse_image_set_alt_setting(st_img, fu_firmware_get_idx(image));
	if (fu_firmware_get_id(image) != NULL) {
		fu_struct_dfuse_image_set_target_named(st_img, 0x01);
		if (!fu_struct_dfuse_image_set_target_name(st_img,
							   fu_firmware_get_id(image),
							   error))
			return NULL;
	}
	fu_struct_dfuse_image_set_target_size(st_img, totalsz);
	fu_struct_dfuse_image_set_chunks(st_img, chunks->len);

	/* copy data */
	for (guint i = 0; i < blobs->len; i++) {
		GBytes *blob = g_ptr_array_index(blobs, i);
		fu_byte_array_append_bytes(st_img, blob);
	}
	return g_bytes_new(st_img->data, st_img->len);
}

static GByteArray *
fu_dfuse_firmware_write(FuFirmware *firmware, GError **error)
{
	FuDfuseFirmware *self = FU_DFUSE_FIRMWARE(firmware);
	gsize totalsz = 0;
	g_autoptr(GByteArray) st_hdr = fu_struct_dfuse_hdr_new();
	g_autoptr(GBytes) blob_noftr = NULL;
	g_autoptr(GPtrArray) blobs = NULL;
	g_autoptr(GPtrArray) images = NULL;

	/* create mutable output buffer */
	blobs = g_ptr_array_new_with_free_func((GDestroyNotify)g_bytes_unref);
	images = fu_firmware_get_images(FU_FIRMWARE(firmware));
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		g_autoptr(GBytes) blob = NULL;
		blob = fu_dfuse_firmware_write_image(self, img, error);
		if (blob == NULL)
			return NULL;
		totalsz += g_bytes_get_size(blob);
		g_ptr_array_add(blobs, g_steal_pointer(&blob));
	}

	/* DfuSe header */
	fu_struct_dfuse_hdr_set_image_size(st_hdr, st_hdr->len + totalsz);
	if (images->len > G_MAXUINT8) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "too many (%u) images to write DfuSe file",
			    images->len);
		return NULL;
	}
	fu_struct_dfuse_hdr_set_targets(st_hdr, (guint8)images->len);

	/* copy images */
	for (guint i = 0; i < blobs->len; i++) {
		GBytes *blob = g_ptr_array_index(blobs, i);
		fu_byte_array_append_bytes(st_hdr, blob);
	}

	/* return blob */
	blob_noftr = g_bytes_new(st_hdr->data, st_hdr->len);
	return fu_dfu_firmware_append_footer(FU_DFU_FIRMWARE(firmware), blob_noftr, error);
}

static void
fu_dfuse_firmware_init(FuDfuseFirmware *self)
{
	fu_dfu_firmware_set_version(FU_DFU_FIRMWARE(self), FU_DFU_FIRMARE_VERSION_DFUSE);
	fu_firmware_set_images_max(FU_FIRMWARE(self), 255);
}

static void
fu_dfuse_firmware_class_init(FuDfuseFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->check_magic = fu_dfuse_firmware_check_magic;
	klass_firmware->parse = fu_dfuse_firmware_parse;
	klass_firmware->write = fu_dfuse_firmware_write;
}

/**
 * fu_dfuse_firmware_new:
 *
 * Creates a new #FuFirmware of sub type DfuSe
 *
 * Since: 1.5.6
 **/
FuFirmware *
fu_dfuse_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_DFUSE_FIRMWARE, NULL));
}
