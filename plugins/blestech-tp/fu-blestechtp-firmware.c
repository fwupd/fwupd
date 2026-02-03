/*
 * Copyright 2026 SHENZHEN Betterlife Electronic CO.LTD <xiaomaoting@blestech.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-blestechtp-firmware.h"
#include "fu-blestechtp-common.h"

struct _FuBlestechtpFirmware {
	FuFirmware parent_instance;
	guint16 checksum;
};

G_DEFINE_TYPE(FuBlestechtpFirmware, fu_blestechtp_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_blestechtp_firmware_compute_checksum_cb(const guint8 *buf,
					gsize bufsz,
					gpointer user_data,
					GError **error)
{
	guint16 *checksum = (guint16 *)user_data;
	for (guint32 i = 0; i < bufsz; ++i) {
        *checksum += buf[i];
	}
	
	return TRUE;
}


guint16
fu_blestechtp_firmware_get_checksum(FuBlestechtpFirmware *self) {
	g_return_val_if_fail(FU_IS_BLESTECHTP_FIRMWARE(self), 0);
	return self->checksum;
}

static gboolean
fu_blestechtp_firmware_parse(FuFirmware *firmware,
			  GInputStream *stream,
			  FuFirmwareParseFlags flags,
			  GError **error)
{
	FuBlestechtpFirmware *self = FU_BLESTECHTP_FIRMWARE(firmware);
	guint16 bin_ver;
	self->checksum = 0;
	if (!fu_input_stream_read_u16(stream,
						BIN_VER_ADDR,
						&bin_ver,
						G_BIG_ENDIAN,
						error)) {
		return FALSE;
	}

	fu_firmware_set_version_raw(FU_FIRMWARE(self), bin_ver);
    /* calc checksum of fw */
	if (!fu_input_stream_chunkify(stream,
				      fu_blestechtp_firmware_compute_checksum_cb,
				      &self->checksum,
				      error))
		return FALSE;

    /* success */
	return TRUE;
}

static void
fu_blestechtp_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuBlestechtpFirmware *self = FU_BLESTECHTP_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "checksum", self->checksum);
}

static void
fu_blestechtp_firmware_init(FuBlestechtpFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_NO_AUTO_DETECTION);
}

static void
fu_blestechtp_firmware_class_init(FuBlestechtpFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_blestechtp_firmware_parse;
	firmware_class->export = fu_blestechtp_firmware_export;
}
