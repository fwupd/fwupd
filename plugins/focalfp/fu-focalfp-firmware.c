/*
 * Copyright 2022 Shihwei Huang <shihwei.huang@focaltech-electronics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-focalfp-firmware.h"

struct _FuFocalfpFirmware {
	FuFirmwareClass parent_instance;
	guint16 start_address;
	guint32 checksum;
};

G_DEFINE_TYPE(FuFocalfpFirmware, fu_focalfp_firmware, FU_TYPE_FIRMWARE)

/* firmware block update */
#define FOCAL_NAME_START_ADDR_WRDS 0x011E

const guint8 focalfp_signature[] = {0xFF};

guint32
fu_focalfp_firmware_get_checksum(FuFocalfpFirmware *self)
{
	g_return_val_if_fail(FU_IS_FOCALFP_FIRMWARE(self), 0);
	return self->checksum;
}

static void
fu_focalfp_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuFocalfpFirmware *self = FU_FOCALFP_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "start_address", self->start_address);
	fu_xmlb_builder_insert_kx(bn, "checksum", self->checksum);
}

static gboolean
fu_focalfp_firmware_compute_checksum_cb(const guint8 *buf,
					gsize bufsz,
					gpointer user_data,
					GError **error)
{
	guint32 *value = (guint32 *)user_data;
	for (guint32 i = 0; i < bufsz; i += 4) {
		guint32 tmp = 0;
		if (!fu_memread_uint32_safe(buf, bufsz, i, &tmp, G_LITTLE_ENDIAN, error))
			return FALSE;
		*value ^= tmp;
	}
	return TRUE;
}

static gboolean
fu_focalfp_firmware_parse(FuFirmware *firmware,
			  GInputStream *stream,
			  FwupdInstallFlags flags,
			  GError **error)
{
	FuFocalfpFirmware *self = FU_FOCALFP_FIRMWARE(firmware);

	/* start address */
	if (!fu_input_stream_read_u16(stream,
				      FOCAL_NAME_START_ADDR_WRDS,
				      &self->start_address,
				      G_BIG_ENDIAN,
				      error)) {
		return FALSE;
	}
	if (self->start_address != 0x582e) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "force pad address invalid: 0x%x",
			    self->start_address);
		return FALSE;
	}

	/* calculate checksum */
	if (!fu_input_stream_chunkify(stream,
				      fu_focalfp_firmware_compute_checksum_cb,
				      &self->checksum,
				      error))
		return FALSE;
	self->checksum += 1;

	/* success */
	return TRUE;
}

static void
fu_focalfp_firmware_init(FuFocalfpFirmware *self)
{
}

static void
fu_focalfp_firmware_class_init(FuFocalfpFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_focalfp_firmware_parse;
	firmware_class->export = fu_focalfp_firmware_export;
}
