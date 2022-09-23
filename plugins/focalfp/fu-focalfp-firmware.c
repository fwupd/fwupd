/*
 * Copyright (C) 2022 Shihwei Huang <shihwei.huang@focaltech-electronics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

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
fu_focalfp_firmware_parse(FuFirmware *firmware,
			  GBytes *fw,
			  gsize offset,
			  FwupdInstallFlags flags,
			  GError **error)
{
	FuFocalfpFirmware *self = FU_FOCALFP_FIRMWARE(firmware);
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	/* start address */
	if (!fu_memread_uint16_safe(buf,
				    bufsz,
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
	for (guint32 i = 0; i < bufsz; i += 4) {
		guint32 value = 0;
		if (!fu_memread_uint32_safe(buf, bufsz, i, &value, G_LITTLE_ENDIAN, error))
			return FALSE;
		self->checksum ^= value;
	}
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
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_focalfp_firmware_parse;
	klass_firmware->export = fu_focalfp_firmware_export;
}

FuFirmware *
fu_focalfp_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_FOCALFP_FIRMWARE, NULL));
}
