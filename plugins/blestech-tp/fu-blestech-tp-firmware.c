/*
 * Copyright 2026 SHENZHEN Betterlife Electronic CO.LTD <xiaomaoting@blestech.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-blestech-tp-firmware.h"

struct _FuBlestechTpFirmware {
	FuFirmware parent_instance;
	guint16 checksum;
};

G_DEFINE_TYPE(FuBlestechTpFirmware, fu_blestech_tp_firmware, FU_TYPE_FIRMWARE)

#define FU_BLESTECH_TP_FIRMWARE_ADDR_BIN_VER 0xC02A

static gboolean
fu_blestech_tp_firmware_compute_checksum_cb(const guint8 *buf,
					    gsize bufsz,
					    gpointer user_data,
					    GError **error)
{
	guint16 *checksum = (guint16 *)user_data;

	*checksum += fu_sum16(buf, bufsz);

	return TRUE;
}

guint16
fu_blestech_tp_firmware_get_checksum(FuBlestechTpFirmware *self)
{
	g_return_val_if_fail(FU_IS_BLESTECH_TP_FIRMWARE(self), 0);
	return self->checksum;
}

static gboolean
fu_blestech_tp_firmware_parse(FuFirmware *firmware,
			      GInputStream *stream,
			      FuFirmwareParseFlags flags,
			      GError **error)
{
	FuBlestechTpFirmware *self = FU_BLESTECH_TP_FIRMWARE(firmware);
	guint16 bin_ver = 0;

	if (!fu_input_stream_read_u16(stream,
				      FU_BLESTECH_TP_FIRMWARE_ADDR_BIN_VER,
				      &bin_ver,
				      G_BIG_ENDIAN,
				      error))
		return FALSE;
	fu_firmware_set_version_raw(FU_FIRMWARE(self), bin_ver);

	/* calc checksum of fw */
	if (!fu_input_stream_chunkify(stream,
				      fu_blestech_tp_firmware_compute_checksum_cb,
				      &self->checksum,
				      error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_blestech_tp_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuBlestechTpFirmware *self = FU_BLESTECH_TP_FIRMWARE(firmware);

	fu_xmlb_builder_insert_kx(bn, "checksum", self->checksum);
}

static void
fu_blestech_tp_firmware_init(FuBlestechTpFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_NO_AUTO_DETECTION);
}

static void
fu_blestech_tp_firmware_class_init(FuBlestechTpFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);

	firmware_class->parse = fu_blestech_tp_firmware_parse;
	firmware_class->export = fu_blestech_tp_firmware_export;
}
