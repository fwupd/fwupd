/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-common.h"
#include "fu-common-version.h"

#include "fu-bcm57xx-common.h"
#include "fu-bcm57xx-stage1-image.h"

struct _FuBcm57xxStage1Image {
	FuFirmwareImage		 parent_instance;
};

G_DEFINE_TYPE (FuBcm57xxStage1Image, fu_bcm57xx_stage1_image, FU_TYPE_FIRMWARE_IMAGE)

static gboolean
fu_bcm57xx_stage1_image_parse (FuFirmwareImage *image,
			       GBytes *fw,
			       FwupdInstallFlags flags,
			       GError **error)
{
	gsize bufsz = 0x0;
	guint32 fwversion = 0;
	const guint8 *buf = g_bytes_get_data (fw, &bufsz);
	g_autoptr(GBytes) fw_nocrc = NULL;

	/* verify CRC */
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0) {
		if (!fu_bcm57xx_verify_crc (fw, error))
			return FALSE;
	}

	/* get version number */
	if (!fu_common_read_uint32_safe (buf, bufsz, BCM_NVRAM_STAGE1_VERSION,
					 &fwversion, G_BIG_ENDIAN, error))
		return FALSE;
	if (fwversion != 0x0) {
		g_autofree gchar *tmp = NULL;
		tmp = fu_common_version_from_uint32 (fwversion, FWUPD_VERSION_FORMAT_TRIPLET);
		fu_firmware_image_set_version (image, tmp);
	} else {
		guint32 bufver[4] = { '\0' };
		guint32 veraddr = 0x0;
		g_autoptr(Bcm57xxVeritem) veritem = NULL;

		/* fall back to the string, e.g. '5719-v1.43' */
		if (!fu_common_read_uint32_safe (buf, bufsz, BCM_NVRAM_STAGE1_VERADDR,
						 &veraddr, G_BIG_ENDIAN, error))
			return FALSE;
		if (veraddr < BCM_PHYS_ADDR_DEFAULT + sizeof(bufver)) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "version address 0x%x less than physical 0x%x",
				     veraddr, (guint) BCM_PHYS_ADDR_DEFAULT);
			return FALSE;
		}
		if (!fu_memcpy_safe ((guint8 *) bufver, sizeof(bufver), 0x0,	/* dst */
				     buf, bufsz, veraddr - BCM_PHYS_ADDR_DEFAULT, /* src */
				     sizeof(bufver), error))
			return FALSE;
		veritem = fu_bcm57xx_veritem_new ((guint8 *) bufver, sizeof(bufver));
		if (veritem != NULL)
			fu_firmware_image_set_version (image, veritem->version);
	}

	fw_nocrc = fu_common_bytes_new_offset (fw, 0x0,
					       g_bytes_get_size (fw) - sizeof(guint32),
					       error);
	if (fw_nocrc == NULL)
		return FALSE;
	fu_firmware_image_set_bytes (image, fw_nocrc);
	return TRUE;
}

static GBytes *
fu_bcm57xx_stage1_image_write (FuFirmwareImage *image, GError **error)
{
	const guint8 *buf;
	gsize bufsz = 0;
	guint32 crc;
	g_autoptr(GByteArray) blob = NULL;
	g_autoptr(GBytes) fw_nocrc = NULL;
	g_autoptr(GBytes) fw_align = NULL;

	/* get the CRC-less data */
	fw_nocrc = fu_firmware_image_get_bytes (image);
	if (fw_nocrc == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "not supported");
		return NULL;
	}

	/* this has to be aligned by DWORDs */
	fw_align = fu_common_bytes_align (fw_nocrc, sizeof(guint32), 0xff);

	/* add to a mutable buffer */
	buf = g_bytes_get_data (fw_align, &bufsz);
	blob = g_byte_array_sized_new (bufsz + sizeof(guint32));
	g_byte_array_append (blob, buf, bufsz);

	/* add CRC */
	crc = fu_bcm57xx_nvram_crc (buf, bufsz);
	fu_byte_array_append_uint32 (blob, crc, G_LITTLE_ENDIAN);
	return g_byte_array_free_to_bytes (g_steal_pointer (&blob));
}

static void
fu_bcm57xx_stage1_image_init (FuBcm57xxStage1Image *self)
{
}

static void
fu_bcm57xx_stage1_image_class_init (FuBcm57xxStage1ImageClass *klass)
{
	FuFirmwareImageClass *klass_image = FU_FIRMWARE_IMAGE_CLASS (klass);
	klass_image->parse = fu_bcm57xx_stage1_image_parse;
	klass_image->write = fu_bcm57xx_stage1_image_write;
}

FuFirmwareImage *
fu_bcm57xx_stage1_image_new (void)
{
	return FU_FIRMWARE_IMAGE (g_object_new (FU_TYPE_BCM57XX_STAGE1_IMAGE, NULL));
}
