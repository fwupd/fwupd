/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-bcm57xx-common.h"
#include "fu-bcm57xx-stage2-image.h"

struct _FuBcm57xxStage2Image {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuBcm57xxStage2Image, fu_bcm57xx_stage2_image, FU_TYPE_FIRMWARE)

static gboolean
fu_bcm57xx_stage2_image_parse(FuFirmware *image,
			      GBytes *fw,
			      gsize offset,
			      FwupdInstallFlags flags,
			      GError **error)
{
	g_autoptr(GBytes) fw_nocrc = NULL;
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0) {
		if (!fu_bcm57xx_verify_crc(fw, error))
			return FALSE;
	}
	fw_nocrc = fu_bytes_new_offset(fw, 0x0, g_bytes_get_size(fw) - sizeof(guint32), error);
	if (fw_nocrc == NULL)
		return FALSE;
	fu_firmware_set_bytes(image, fw_nocrc);
	return TRUE;
}

static GBytes *
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
	fu_byte_array_append_uint32(blob, fu_bcm57xx_nvram_crc(buf, bufsz), G_LITTLE_ENDIAN);
	return g_byte_array_free_to_bytes(g_steal_pointer(&blob));
}

static void
fu_bcm57xx_stage2_image_init(FuBcm57xxStage2Image *self)
{
}

static void
fu_bcm57xx_stage2_image_class_init(FuBcm57xxStage2ImageClass *klass)
{
	FuFirmwareClass *klass_image = FU_FIRMWARE_CLASS(klass);
	klass_image->parse = fu_bcm57xx_stage2_image_parse;
	klass_image->write = fu_bcm57xx_stage2_image_write;
}

FuFirmware *
fu_bcm57xx_stage2_image_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_BCM57XX_STAGE2_IMAGE, NULL));
}
