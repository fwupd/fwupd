/*
 * Copyright 2025 Shihwei Huang <shihwei.huang@focaltech-electronics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-focaltouch-firmware.h"

struct _FuFocaltouchFirmware {
	FuFirmware parent_instance;
	guint32 checksum;
};

G_DEFINE_TYPE(FuFocaltouchFirmware, fu_focaltouch_firmware, FU_TYPE_FIRMWARE)

/* firmware block update */
#define FOCAL_NAME_START_ADDR_WRDS 0x011E

const guint8 focaltouch_signature[] = {0xFF};

guint32
fu_focaltouch_firmware_get_checksum(FuFocaltouchFirmware *self)
{
	g_return_val_if_fail(FU_IS_FOCALTOUCH_FIRMWARE(self), 0);
	return self->checksum;
}

static void
fu_focaltouch_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuFocaltouchFirmware *self = FU_FOCALTOUCH_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "checksum", self->checksum);
}

static gboolean
fu_focaltouch_firmware_compute_checksum_cb(const guint8 *buf,
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
fu_focaltouch_firmware_parse(FuFirmware *firmware,
			     GInputStream *stream,
			     FuFirmwareParseFlags flags,
			     GError **error)
{
	FuFocaltouchFirmware *self = FU_FOCALTOUCH_FIRMWARE(firmware);
	/* calculate checksum */
	if (!fu_input_stream_chunkify(stream,
				      fu_focaltouch_firmware_compute_checksum_cb,
				      &self->checksum,
				      error))
		return FALSE;
	self->checksum += 1;

	/* success */
	return TRUE;
}

static void
fu_focaltouch_firmware_init(FuFocaltouchFirmware *self)
{
}

static void
fu_focaltouch_firmware_class_init(FuFocaltouchFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_focaltouch_firmware_parse;
	firmware_class->export = fu_focaltouch_firmware_export;
}
