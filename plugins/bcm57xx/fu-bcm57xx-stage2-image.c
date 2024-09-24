/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-bcm57xx-common.h"
#include "fu-bcm57xx-stage2-image.h"

struct _FuBcm57xxStage2Image {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuBcm57xxStage2Image, fu_bcm57xx_stage2_image, FU_TYPE_FIRMWARE)

static gboolean
fu_bcm57xx_stage2_image_parse(FuFirmware *image,
			      GInputStream *stream,
			      gsize offset,
			      FwupdInstallFlags flags,
			      GError **error)
{
	gsize streamsz = 0;
	g_autoptr(GInputStream) stream_nocrc = NULL;
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0) {
		if (!fu_bcm57xx_verify_crc(stream, error))
			return FALSE;
	}
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (streamsz < sizeof(guint32)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "stage2 image is too small");
		return FALSE;
	}
	stream_nocrc = fu_partial_input_stream_new(stream, 0x0, streamsz - sizeof(guint32), error);
	if (stream_nocrc == NULL)
		return FALSE;
	return fu_firmware_set_stream(image, stream_nocrc, error);
}

static GByteArray *
fu_bcm57xx_stage2_image_write(FuFirmware *image, GError **error)
{
	const guint8 *buf;
	gsize bufsz = 0;
	g_autoptr(GByteArray) blob = NULL;
	g_autoptr(GBytes) fw_nocrc = NULL;

	/* get the CRC-less data */
	fw_nocrc = fu_firmware_get_bytes(image, error);
	if (fw_nocrc == NULL)
		return NULL;

	/* add to a mutable buffer */
	buf = g_bytes_get_data(fw_nocrc, &bufsz);
	blob = g_byte_array_sized_new(bufsz + (sizeof(guint32) * 3));
	fu_byte_array_append_uint32(blob, BCM_NVRAM_MAGIC, G_BIG_ENDIAN);
	fu_byte_array_append_uint32(blob,
				    g_bytes_get_size(fw_nocrc) + sizeof(guint32),
				    G_BIG_ENDIAN);
	fu_byte_array_append_bytes(blob, fw_nocrc);

	/* add CRC */
	fu_byte_array_append_uint32(blob,
				    fu_crc32(FU_CRC_KIND_B32_STANDARD, buf, bufsz),
				    G_LITTLE_ENDIAN);
	return g_steal_pointer(&blob);
}

static void
fu_bcm57xx_stage2_image_init(FuBcm57xxStage2Image *self)
{
}

static void
fu_bcm57xx_stage2_image_class_init(FuBcm57xxStage2ImageClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_bcm57xx_stage2_image_parse;
	firmware_class->write = fu_bcm57xx_stage2_image_write;
}

FuFirmware *
fu_bcm57xx_stage2_image_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_BCM57XX_STAGE2_IMAGE, NULL));
}
