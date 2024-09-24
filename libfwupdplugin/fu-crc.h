/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

/**
 * FuCrcKind:
 *
 * The type of CRC.
 **/
typedef enum {
	FU_CRC_KIND_UNKNOWN,
	FU_CRC_KIND_B32_STANDARD,
	FU_CRC_KIND_B32_BZIP2,
	FU_CRC_KIND_B32_JAMCRC,
	FU_CRC_KIND_B32_MPEG2,
	FU_CRC_KIND_B32_POSIX,
	FU_CRC_KIND_B32_SATA,
	FU_CRC_KIND_B32_XFER,
	FU_CRC_KIND_B32_C,
	FU_CRC_KIND_B32_D,
	FU_CRC_KIND_B32_Q,
	FU_CRC_KIND_B16_XMODEM,
	FU_CRC_KIND_B16_USB,
	FU_CRC_KIND_B16_UMTS,
	FU_CRC_KIND_B16_TMS37157,
	FU_CRC_KIND_B8_WCDMA,
	FU_CRC_KIND_B8_TECH_3250,
	FU_CRC_KIND_B8_STANDARD,
	FU_CRC_KIND_B8_SAE_J1850,
	FU_CRC_KIND_B8_ROHC,
	FU_CRC_KIND_B8_OPENSAFETY,
	FU_CRC_KIND_B8_NRSC_5,
	FU_CRC_KIND_B8_MIFARE_MAD,
	FU_CRC_KIND_B8_MAXIM_DOW,
	FU_CRC_KIND_B8_LTE,
	FU_CRC_KIND_B8_I_CODE,
	FU_CRC_KIND_B8_ITU,
	FU_CRC_KIND_B8_HITAG,
	FU_CRC_KIND_B8_GSM_B,
	FU_CRC_KIND_B8_GSM_A,
	FU_CRC_KIND_B8_DVB_S2,
	FU_CRC_KIND_B8_DARC,
	FU_CRC_KIND_B8_CDMA2000,
	FU_CRC_KIND_B8_BLUETOOTH,
	FU_CRC_KIND_B8_AUTOSAR,
	/*< private >*/
	FU_CRC_KIND_LAST,
} FuCrcKind;

guint32
fu_crc32(FuCrcKind kind, const guint8 *buf, gsize bufsz);
guint16
fu_crc16(FuCrcKind kind, const guint8 *buf, gsize bufsz);
guint8
fu_crc8(FuCrcKind kind, const guint8 *buf, gsize bufsz);

FuCrcKind
fu_crc_find(const guint8 *buf, gsize bufsz, guint32 crc_target);

guint16
fu_crc_misr16(guint16 init, const guint8 *buf, gsize bufsz);
