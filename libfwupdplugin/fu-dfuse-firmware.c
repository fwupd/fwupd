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
#include "fu-dfuse-firmware.h"
#include "fu-mem.h"
#include "fu-struct.h"

/**
 * FuDfuseFirmware:
 *
 * A DfuSe firmware image.
 *
 * See also: [class@FuDfuFirmware]
 */

G_DEFINE_TYPE(FuDfuseFirmware, fu_dfuse_firmware, FU_TYPE_DFU_FIRMWARE)

static FuChunk *
fu_dfuse_firmware_chunk_parse(FuDfuseFirmware *self, GBytes *bytes, gsize *offset, GError **error)
{
	guint32 address = 0x0;
	guint32 size = 0x0;
	gsize bufsz = 0;
	gsize ftrlen = fu_dfu_firmware_get_footer_len(FU_DFU_FIRMWARE(self));
	const guint8 *buf = g_bytes_get_data(bytes, &bufsz);
	g_autoptr(FuChunk) chk = NULL;
	g_autoptr(GBytes) blob = NULL;

	/* check size */
	if (!fu_struct_unpack_from("<[LL]", error, buf, bufsz - ftrlen, offset, &address, &size))
		return NULL;

	/* create new chunk */
	blob = fu_bytes_new_offset(bytes, *offset, size, error);
	if (blob == NULL)
		return NULL;
	chk = fu_chunk_bytes_new(blob);
	fu_chunk_set_address(chk, address);
	*offset += fu_chunk_get_data_sz(chk);

	/* success */
	return g_steal_pointer(&chk);
}

static FuFirmware *
fu_dfuse_firmware_image_parse(FuDfuseFirmware *self, GBytes *bytes, gsize *offset, GError **error)
{
	gsize bufsz = 0;
	guint8 sig[6] = {0x0};
	guint8 alt_setting = 0x0;
	guint32 target_named = 0x0;
	gchar target_name[255] = {0x0};
	guint32 target_size = 0x0;
	guint32 chunks = 0x0;
	const guint8 *buf = g_bytes_get_data(bytes, &bufsz);
	g_autoptr(FuFirmware) image = fu_firmware_new();

	/* verify image signature */
	if (!fu_struct_unpack_from("<[6sBL255sLL",
				   error,
				   buf,
				   bufsz,
				   offset,
				   &sig,
				   &alt_setting,
				   &target_named,
				   &target_name,
				   &target_size,
				   &chunks))
		return NULL;
	if (memcmp(sig, "Target", sizeof(sig)) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid DfuSe target signature");
		return NULL;
	}

	/* set properties */
	fu_firmware_set_idx(image, alt_setting);
	if (target_named == 0x01) {
		g_autofree gchar *img_id = g_strndup(target_name, sizeof(target_name));
		fu_firmware_set_id(image, img_id);
	}

	/* no chunks */
	if (chunks == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "DfuSe image has no chunks");
		return NULL;
	}

	/* parse chunks */
	for (guint j = 0; j < chunks; j++) {
		g_autoptr(FuChunk) chk = NULL;
		chk = fu_dfuse_firmware_chunk_parse(self, bytes, offset, error);
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
	guint8 magic[5] = {0x0};

	if (!fu_memcpy_safe(magic,
			    sizeof(magic),
			    0x0, /* dst */
			    g_bytes_get_data(fw, NULL),
			    g_bytes_get_size(fw),
			    offset, /* src */
			    sizeof(magic),
			    error)) {
		g_prefix_error(error, "failed to read magic: ");
		return FALSE;
	}
	if (memcmp(magic, "DfuSe", sizeof(magic)) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid DfuSe prefix");
		return FALSE;
	}

	/* success */
	return TRUE;
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
	guint32 image_size = 0;
	guint8 targets = 0;
	guint8 ver = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	/* DFU footer first */
	if (!fu_dfu_firmware_parse_footer(dfu_firmware, fw, flags, error))
		return FALSE;

	/* parse */
	if (!fu_struct_unpack_from("<[5xBLB]",
				   error,
				   buf,
				   bufsz,
				   &offset,
				   &ver,
				   &image_size,
				   &targets))
		return FALSE;

	/* check the version */
	if (ver != 0x01) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "invalid DfuSe version, got %02x",
			    ver);
		return FALSE;
	}

	/* check image size */
	if (image_size != bufsz - fu_dfu_firmware_get_footer_len(dfu_firmware)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "invalid DfuSe image size, "
			    "got %" G_GUINT32_FORMAT ", "
			    "expected %" G_GSIZE_FORMAT,
			    image_size,
			    bufsz - fu_dfu_firmware_get_footer_len(dfu_firmware));
		return FALSE;
	}

	/* parse the image targets */
	for (guint i = 0; i < targets; i++) {
		g_autoptr(FuFirmware) image = NULL;
		image =
		    fu_dfuse_firmware_image_parse(FU_DFUSE_FIRMWARE(firmware), fw, &offset, error);
		if (image == NULL)
			return FALSE;
		fu_firmware_add_image(firmware, image);
	}
	return TRUE;
}

static GBytes *
fu_firmware_chunk_write(FuChunk *chk)
{
	g_autoptr(GByteArray) buf = NULL;
	buf = fu_struct_pack("<LL", NULL, fu_chunk_get_address(chk), fu_chunk_get_data_sz(chk));
	if (buf == NULL)
		return NULL;
	g_byte_array_append(buf, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static GBytes *
fu_dfuse_firmware_write_image(FuFirmware *image, GError **error)
{
	gsize totalsz = 0;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GPtrArray) blobs = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* get total size */
	blobs = g_ptr_array_new_with_free_func((GDestroyNotify)g_bytes_unref);
	chunks = fu_firmware_get_chunks(image, error);
	if (chunks == NULL)
		return NULL;
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		GBytes *bytes = fu_firmware_chunk_write(chk);
		g_ptr_array_add(blobs, bytes);
		totalsz += g_bytes_get_size(bytes);
	}

	/* add prefix */
	buf = fu_struct_pack("<6sBL255sLL",
			     error,
			     "Target",					    /* sig */
			     (guint)fu_firmware_get_idx(image),		    /* alt_setting */
			     fu_firmware_get_id(image) != NULL ? 0x1 : 0x0, /* target_named */
			     fu_firmware_get_id(image),			    /* target_name */
			     totalsz,					    /* target_size */
			     chunks->len);
	if (buf == NULL)
		return NULL;

	/* copy data */
	for (guint i = 0; i < blobs->len; i++) {
		GBytes *blob = g_ptr_array_index(blobs, i);
		fu_byte_array_append_bytes(buf, blob);
	}
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static GBytes *
fu_dfuse_firmware_write(FuFirmware *firmware, GError **error)
{
	gsize totalsz = 0;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GBytes) blob_noftr = NULL;
	g_autoptr(GPtrArray) blobs = NULL;
	g_autoptr(GPtrArray) images = NULL;

	/* create mutable output buffer */
	blobs = g_ptr_array_new_with_free_func((GDestroyNotify)g_bytes_unref);
	images = fu_firmware_get_images(FU_FIRMWARE(firmware));
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		g_autoptr(GBytes) blob = NULL;
		blob = fu_dfuse_firmware_write_image(img, error);
		if (blob == NULL)
			return NULL;
		totalsz += g_bytes_get_size(blob);
		g_ptr_array_add(blobs, g_steal_pointer(&blob));
	}

	/* DfuSe header */
	buf = fu_struct_pack("<5sBLB", error, "DfuSe", 0x01, 0xB + totalsz, images->len);
	if (buf == NULL)
		return NULL;

	/* copy images */
	for (guint i = 0; i < blobs->len; i++) {
		GBytes *blob = g_ptr_array_index(blobs, i);
		fu_byte_array_append_bytes(buf, blob);
	}

	/* return blob */
	blob_noftr = g_byte_array_free_to_bytes(g_steal_pointer(&buf));
	return fu_dfu_firmware_append_footer(FU_DFU_FIRMWARE(firmware), blob_noftr, error);
}

static void
fu_dfuse_firmware_init(FuDfuseFirmware *self)
{
	fu_dfu_firmware_set_version(FU_DFU_FIRMWARE(self), FU_DFU_FIRMARE_VERSION_DFUSE);
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
