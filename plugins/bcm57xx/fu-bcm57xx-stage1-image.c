/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-bcm57xx-common.h"
#include "fu-bcm57xx-stage1-image.h"

struct _FuBcm57xxStage1Image {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuBcm57xxStage1Image, fu_bcm57xx_stage1_image, FU_TYPE_FIRMWARE)

static gboolean
fu_bcm57xx_stage1_image_parse(FuFirmware *image,
			      GInputStream *stream,
			      gsize offset,
			      FwupdInstallFlags flags,
			      GError **error)
{
	gsize streamsz = 0;
	guint32 fwversion = 0;
	g_autoptr(GInputStream) stream_nocrc = NULL;

	/* verify CRC */
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0) {
		if (!fu_bcm57xx_verify_crc(stream, error))
			return FALSE;
	}

	/* get version number */
	if (!fu_input_stream_read_u32(stream,
				      BCM_NVRAM_STAGE1_VERSION,
				      &fwversion,
				      G_BIG_ENDIAN,
				      error))
		return FALSE;
	if (fwversion != 0x0) {
		fu_firmware_set_version_raw(image, fwversion);
	} else {
		guint32 veraddr = 0x0;

		/* fall back to the optional string, e.g. '5719-v1.43' */
		if (!fu_input_stream_read_u32(stream,
					      BCM_NVRAM_STAGE1_VERADDR,
					      &veraddr,
					      G_BIG_ENDIAN,
					      error))
			return FALSE;
		if (veraddr != 0x0) {
			guint32 bufver[4] = {'\0'};
			g_autoptr(Bcm57xxVeritem) veritem = NULL;
			if (veraddr < BCM_PHYS_ADDR_DEFAULT + sizeof(bufver)) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "version address 0x%x less than physical 0x%x",
					    veraddr,
					    (guint)BCM_PHYS_ADDR_DEFAULT);
				return FALSE;
			}
			if (!fu_input_stream_read_safe(stream,
						       (guint8 *)bufver,
						       sizeof(bufver),
						       0x0,				/* dst */
						       veraddr - BCM_PHYS_ADDR_DEFAULT, /* src */
						       sizeof(bufver),
						       error))
				return FALSE;
			veritem = fu_bcm57xx_veritem_new((guint8 *)bufver, sizeof(bufver));
			if (veritem != NULL) {
				fu_firmware_set_version(image, /* nocheck:set-version */
							veritem->version);
			}
		}
	}

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (streamsz < sizeof(guint32)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "stage1 image is too small");
		return FALSE;
	}
	stream_nocrc = fu_partial_input_stream_new(stream, 0x0, streamsz - sizeof(guint32), error);
	if (stream_nocrc == NULL)
		return FALSE;
	return fu_firmware_set_stream(image, stream_nocrc, error);
}

static GByteArray *
fu_bcm57xx_stage1_image_write(FuFirmware *firmware, GError **error)
{
	guint32 crc;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) fw_nocrc = NULL;

	/* sanity check */
	if (fu_firmware_get_alignment(firmware) > FU_FIRMWARE_ALIGNMENT_1M) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "alignment invalid, got 0x%02x",
			    fu_firmware_get_alignment(firmware));
		return NULL;
	}

	/* the CRC-less payload */
	fw_nocrc = fu_firmware_get_bytes(firmware, error);
	if (fw_nocrc == NULL)
		return NULL;

	/* fuzzing, so write a header with the version */
	if (g_bytes_get_size(fw_nocrc) < BCM_NVRAM_STAGE1_VERSION)
		fu_byte_array_set_size(buf, BCM_NVRAM_STAGE1_VERSION + sizeof(guint32), 0x00);

	/* payload */
	fu_byte_array_append_bytes(buf, fw_nocrc);

	/* update version */
	if (!fu_memwrite_uint32_safe(buf->data,
				     buf->len,
				     BCM_NVRAM_STAGE1_VERSION,
				     fu_firmware_get_version_raw(firmware),
				     G_BIG_ENDIAN,
				     error))
		return NULL;

	/* align */
	fu_byte_array_set_size(
	    buf,
	    fu_common_align_up(g_bytes_get_size(fw_nocrc), fu_firmware_get_alignment(firmware)),
	    0x00);

	/* add CRC */
	crc = fu_crc32(FU_CRC_KIND_B32_STANDARD, buf->data, buf->len);
	fu_byte_array_append_uint32(buf, crc, G_LITTLE_ENDIAN);
	return g_steal_pointer(&buf);
}

static gchar *
fu_bcm57xx_stage1_image_convert_version(FuFirmware *firmware, guint64 version_raw)
{
	return fu_version_from_uint32(version_raw, fu_firmware_get_version_format(firmware));
}

static void
fu_bcm57xx_stage1_image_init(FuBcm57xxStage1Image *self)
{
	fu_firmware_set_alignment(FU_FIRMWARE(self), FU_FIRMWARE_ALIGNMENT_4);
	fu_firmware_set_version_format(FU_FIRMWARE(self), FWUPD_VERSION_FORMAT_TRIPLET);
}

static void
fu_bcm57xx_stage1_image_class_init(FuBcm57xxStage1ImageClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->convert_version = fu_bcm57xx_stage1_image_convert_version;
	firmware_class->parse = fu_bcm57xx_stage1_image_parse;
	firmware_class->write = fu_bcm57xx_stage1_image_write;
}

FuFirmware *
fu_bcm57xx_stage1_image_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_BCM57XX_STAGE1_IMAGE, NULL));
}
