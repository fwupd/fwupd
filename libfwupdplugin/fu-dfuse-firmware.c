/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuFirmware"

#include "config.h"

#include <string.h>

#include "fu-chunk-private.h"
#include "fu-common.h"
#include "fu-dfu-firmware-private.h"
#include "fu-dfuse-firmware.h"

/**
 * SECTION:fu-dfuse-firmware
 * @short_description: DfuSe firmware image
 *
 * An object that represents a DfuSe firmware image.
 *
 * See also: #FuDfuFirmware
 */

G_DEFINE_TYPE (FuDfuseFirmware, fu_dfuse_firmware, FU_TYPE_DFU_FIRMWARE)

/* firmware: LE */
typedef struct __attribute__((packed)) {
	guint8		 sig[5];
	guint8		 ver;
	guint32		 image_size;
	guint8		 targets;
} DfuSeHdr;

/* image: LE */
typedef struct __attribute__((packed)) {
	guint8		 sig[6];
	guint8		 alt_setting;
	guint32		 target_named;
	gchar		 target_name[255];
	guint32		 target_size;
	guint32		 chunks;
} DfuSeImageHdr;

/* element: LE */
typedef struct __attribute__((packed)) {
	guint32		 address;
	guint32		 size;
} DfuSeElementHdr;

G_STATIC_ASSERT(sizeof(DfuSeHdr) == 11);
G_STATIC_ASSERT(sizeof(DfuSeImageHdr) == 274);
G_STATIC_ASSERT(sizeof(DfuSeElementHdr) == 8);

static FuChunk *
fu_firmware_image_chunk_parse (FuDfuseFirmware *self,
			       GBytes *bytes,
			       gsize *offset,
			       GError **error)
{
	DfuSeElementHdr hdr = { 0x0 };
	gsize bufsz = 0;
	gsize ftrlen = fu_dfu_firmware_get_footer_len (FU_DFU_FIRMWARE (self));
	const guint8 *buf = g_bytes_get_data (bytes, &bufsz);
	g_autoptr(FuChunk) chk = NULL;
	g_autoptr(GBytes) blob = NULL;

	/* check size */
	if (!fu_memcpy_safe ((guint8 *) &hdr, sizeof(hdr), 0x0,	/* dst */
			     buf, bufsz - ftrlen, *offset,	/* src */
			     sizeof(hdr), error))
		return NULL;

	/* create new chunk */
	*offset += sizeof(hdr);
	blob = fu_common_bytes_new_offset (bytes, *offset,
					   GUINT32_FROM_LE (hdr.size),
					   error);
	if (blob == NULL)
		return NULL;
	chk = fu_chunk_bytes_new (blob);
	fu_chunk_set_address (chk, GUINT32_FROM_LE (hdr.address));
	*offset += fu_chunk_get_data_sz (chk);

	/* success */
	return g_steal_pointer (&chk);
}

static FuFirmwareImage *
fu_dfuse_firmware_image_parse (FuDfuseFirmware *self,
			       GBytes *bytes,
			       gsize *offset,
			       GError **error)
{
	DfuSeImageHdr hdr = { 0x0 };
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data (bytes, &bufsz);
	g_autoptr(FuFirmwareImage) image = fu_firmware_image_new (NULL);

	/* verify image signature */
	if (!fu_memcpy_safe ((guint8 *) &hdr, sizeof(hdr), 0x0,	/* dst */
			     buf, bufsz, *offset,		/* src */
			     sizeof(hdr), error))
		return NULL;
	if (memcmp (hdr.sig, "Target", sizeof(hdr.sig)) != 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "invalid DfuSe target signature");
		return NULL;
	}

	/* set properties */
	fu_firmware_image_set_idx (image, hdr.alt_setting);
	if (GUINT32_FROM_LE (hdr.target_named) == 0x01) {
		g_autofree gchar *img_id = NULL;
		img_id = g_strndup (hdr.target_name, sizeof(hdr.target_name));
		fu_firmware_image_set_id (image, img_id);
	}

	/* no chunks */
	if (hdr.chunks == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "DfuSe image has no chunks");
		return NULL;
	}

	/* parse chunks */
	*offset += sizeof(hdr);
	for (guint j = 0; j < GUINT32_FROM_LE (hdr.chunks); j++) {
		g_autoptr(FuChunk) chk = NULL;
		chk = fu_firmware_image_chunk_parse (self,
						     bytes,
						     offset,
						     error);
		if (chk == NULL)
			return NULL;
		fu_firmware_image_add_chunk (image, chk);
	}

	/* success */
	return g_steal_pointer (&image);
}

static gboolean
fu_dfuse_firmware_parse (FuFirmware *firmware,
			 GBytes *fw,
			 guint64 addr_start,
			 guint64 addr_end,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuDfuFirmware *dfu_firmware = FU_DFU_FIRMWARE (firmware);
	DfuSeHdr hdr = { 0x0 };
	gsize bufsz = 0;
	gsize offset = 0;
	const guint8 *buf;

	/* DFU footer first */
	if (!fu_dfu_firmware_parse_footer (dfu_firmware, fw, flags, error))
		return FALSE;

	/* check the prefix */
	buf = (const guint8 *) g_bytes_get_data (fw, &bufsz);
	if (!fu_memcpy_safe ((guint8 *) &hdr, sizeof(hdr), 0x0,	/* dst */
			     buf, bufsz, offset,		/* src */
			     sizeof(hdr), error))
		return FALSE;
	if (memcmp (hdr.sig, "DfuSe", sizeof(hdr.sig)) != 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "invalid DfuSe prefix");
		return FALSE;
	}

	/* check the version */
	if (hdr.ver != 0x01) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "invalid DfuSe version, got %02x",
			     hdr.ver);
		return FALSE;
	}

	/* check image size */
	if (GUINT32_FROM_LE (hdr.image_size) !=
	    bufsz - fu_dfu_firmware_get_footer_len (dfu_firmware)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "invalid DfuSe image size, "
			     "got %" G_GUINT32_FORMAT ", "
			     "expected %" G_GSIZE_FORMAT,
			     GUINT32_FROM_LE (hdr.image_size),
			     bufsz - fu_dfu_firmware_get_footer_len (dfu_firmware));
		return FALSE;
	}

	/* parse the image targets */
	offset += sizeof(hdr);
	for (guint i = 0; i < hdr.targets; i++) {
		g_autoptr(FuFirmwareImage) image = NULL;
		image = fu_dfuse_firmware_image_parse (FU_DFUSE_FIRMWARE (firmware),
						       fw, &offset,
						       error);
		if (image == NULL)
			return FALSE;
		fu_firmware_add_image (firmware, image);
	}
	return TRUE;
}

static GBytes *
fu_firmware_image_chunk_write (FuChunk *chk)
{
	DfuSeElementHdr hdr = { 0x0 };
	const guint8 *data = fu_chunk_get_data (chk);
	gsize length = fu_chunk_get_data_sz (chk);
	g_autoptr(GByteArray) buf = NULL;

	buf = g_byte_array_sized_new (sizeof(DfuSeElementHdr) + length);
	hdr.address = GUINT32_TO_LE (fu_chunk_get_address (chk));
	hdr.size = GUINT32_TO_LE (length);
	g_byte_array_append (buf, (const guint8 *) &hdr, sizeof(hdr));
	g_byte_array_append (buf, data, length);
	return g_byte_array_free_to_bytes (g_steal_pointer (&buf));
}

static GBytes *
fu_dfuse_firmware_image_write (FuFirmwareImage *image, GError **error)
{
	DfuSeImageHdr hdr = { 0x0 };
	gsize totalsz = 0;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GPtrArray) blobs = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* get total size */
	blobs = g_ptr_array_new_with_free_func ((GDestroyNotify) g_bytes_unref);
	chunks = fu_firmware_image_get_chunks (image, error);
	if (chunks == NULL)
		return NULL;
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		GBytes *bytes = fu_firmware_image_chunk_write (chk);
		g_ptr_array_add (blobs, bytes);
		totalsz += g_bytes_get_size (bytes);
	}

	/* mutable output buffer */
	buf = g_byte_array_sized_new (sizeof(DfuSeImageHdr) + totalsz);

	/* add prefix */
	memcpy (hdr.sig, "Target", 6);
	hdr.alt_setting = fu_firmware_image_get_idx (image);
	if (fu_firmware_image_get_id (image) != NULL) {
		hdr.target_named = GUINT32_TO_LE (0x01);
		g_strlcpy ((gchar *) &hdr.target_name,
			   fu_firmware_image_get_id (image),
			   sizeof(hdr.target_name));
	}
	hdr.target_size = GUINT32_TO_LE (totalsz);
	hdr.chunks = GUINT32_TO_LE (chunks->len);
	g_byte_array_append (buf, (const guint8 *) &hdr, sizeof(hdr));

	/* copy data */
	for (guint i = 0; i < blobs->len; i++) {
		GBytes *blob = g_ptr_array_index (blobs, i);
		g_byte_array_append (buf,
				     g_bytes_get_data (blob, NULL),
				     g_bytes_get_size (blob));
	}
	return g_byte_array_free_to_bytes (g_steal_pointer (&buf));
}

static GBytes *
fu_dfuse_firmware_write (FuFirmware *firmware, GError **error)
{
	DfuSeHdr hdr = { 0x0 };
	gsize totalsz = 0;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GBytes) blob_noftr = NULL;
	g_autoptr(GPtrArray) blobs = NULL;
	g_autoptr(GPtrArray) images = NULL;

	/* create mutable output buffer */
	blobs = g_ptr_array_new_with_free_func ((GDestroyNotify) g_bytes_unref);
	images = fu_firmware_get_images (FU_FIRMWARE (firmware));
	for (guint i = 0; i < images->len; i++) {
		FuFirmwareImage *img = g_ptr_array_index (images, i);
		g_autoptr(GBytes) blob = NULL;
		blob = fu_dfuse_firmware_image_write (img, error);
		if (blob == NULL)
			return NULL;
		totalsz += g_bytes_get_size (blob);
		g_ptr_array_add (blobs, g_steal_pointer (&blob));
	}
	buf = g_byte_array_sized_new (sizeof(DfuSeHdr) + totalsz);

	/* DfuSe header */
	memcpy (hdr.sig, "DfuSe", 5);
	hdr.ver = 0x01;
	hdr.image_size = GUINT32_TO_LE (sizeof(hdr) + totalsz);
	if (images->len > G_MAXUINT8) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "too many (%u) images to write DfuSe file",
			     images->len);
		return NULL;
	}
	hdr.targets = (guint8) images->len;
	g_byte_array_append (buf, (const guint8 *) &hdr, sizeof(hdr));

	/* copy images */
	for (guint i = 0; i < blobs->len; i++) {
		GBytes *blob = g_ptr_array_index (blobs, i);
		g_byte_array_append (buf,
				     g_bytes_get_data (blob, NULL),
				     g_bytes_get_size (blob));
	}

	/* return blob */
	blob_noftr = g_byte_array_free_to_bytes (g_steal_pointer (&buf));
	return fu_dfu_firmware_append_footer (FU_DFU_FIRMWARE (firmware),
					      blob_noftr, error);
}

static void
fu_dfuse_firmware_init (FuDfuseFirmware *self)
{
	fu_dfu_firmware_set_version (FU_DFU_FIRMWARE (self), DFU_VERSION_DFUSE);
}

static void
fu_dfuse_firmware_class_init (FuDfuseFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	klass_firmware->parse = fu_dfuse_firmware_parse;
	klass_firmware->write = fu_dfuse_firmware_write;
}

/**
 * fu_dfuse_firmware_new:
 *
 * Creates a new #FuFirmware of sub type Dfuse
 *
 * Since: 1.5.6
 **/
FuFirmware *
fu_dfuse_firmware_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_DFUSE_FIRMWARE, NULL));
}
