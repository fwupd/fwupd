/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

guint8
fu_crc8(const guint8 *buf, gsize bufsz);
guint8
fu_crc8_full(const guint8 *buf, gsize bufsz, guint8 crc_init, guint8 polynomial);

guint16
fu_crc16(const guint8 *buf, gsize bufsz);
guint16
fu_crc16_full(const guint8 *buf, gsize bufsz, guint16 crc, guint16 polynomial);

/**
 * FuCrc32Kind:
 * @FU_CRC32_KIND_UNKNOWN:	Unknown
 * @FU_CRC32_KIND_STANDARD:	CRC-32
 * @FU_CRC32_KIND_BZIP2:	CRC-32/BZIP2
 * @FU_CRC32_KIND_JAMCRC:	CRC-32/JAMCRC
 * @FU_CRC32_KIND_MPEG2:	CRC-32/MPEG-2
 * @FU_CRC32_KIND_POSIX:	CRC-32/POSIX
 * @FU_CRC32_KIND_SATA:		CRC-32/SATA
 * @FU_CRC32_KIND_XFER:		CRC-32/XFER
 * @FU_CRC32_KIND_C:		CRC-32C
 * @FU_CRC32_KIND_D:		CRC-32D
 * @FU_CRC32_KIND_Q:		CRC-32Q
 *
 * The type of CRC-32.
 **/
typedef enum {
	FU_CRC32_KIND_UNKNOWN,
	FU_CRC32_KIND_STANDARD,
	FU_CRC32_KIND_BZIP2,
	FU_CRC32_KIND_JAMCRC,
	FU_CRC32_KIND_MPEG2,
	FU_CRC32_KIND_POSIX,
	FU_CRC32_KIND_SATA,
	FU_CRC32_KIND_XFER,
	FU_CRC32_KIND_C,
	FU_CRC32_KIND_D,
	FU_CRC32_KIND_Q,
	/*< private >*/
	FU_CRC32_KIND_LAST,
} FuCrc32Kind;

guint32
fu_crc32(FuCrc32Kind kind, const guint8 *buf, gsize bufsz);
FuCrc32Kind
fu_crc32_find(const guint8 *buf, gsize bufsz, guint32 crc_target);

guint16
fu_crc_misr16(guint16 init, const guint8 *buf, gsize bufsz);
