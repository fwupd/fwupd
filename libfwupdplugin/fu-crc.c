/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuCommon"

#include "config.h"

#include "fu-crc.h"

/**
 * fu_crc8_full:
 * @buf: memory buffer
 * @bufsz: size of @buf
 * @crc_init: initial CRC value, typically 0x00
 * @polynomial: CRC polynomial, e.g. 0x07 for CCITT
 *
 * Returns the cyclic redundancy check value for the given memory buffer.
 *
 * Returns: CRC value
 *
 * Since: 1.8.2
 **/
guint8
fu_crc8_full(const guint8 *buf, gsize bufsz, guint8 crc_init, guint8 polynomial)
{
	guint32 crc = crc_init;
	for (gsize j = bufsz; j > 0; j--) {
		crc ^= (*(buf++) << 8);
		for (guint32 i = 8; i; i--) {
			if (crc & 0x8000)
				crc ^= ((polynomial | 0x100) << 7);
			crc <<= 1;
		}
	}
	return ~((guint8)(crc >> 8));
}

/**
 * fu_crc8:
 * @buf: memory buffer
 * @bufsz: size of @buf
 *
 * Returns the cyclic redundancy check value for the given memory buffer.
 *
 * Returns: CRC value
 *
 * Since: 1.8.2
 **/
guint8
fu_crc8(const guint8 *buf, gsize bufsz)
{
	return fu_crc8_full(buf, bufsz, 0x00, 0x07);
}

/**
 * fu_crc16_full:
 * @buf: memory buffer
 * @bufsz: size of @buf
 * @crc: initial CRC value, typically 0xFFFF
 * @polynomial: CRC polynomial, typically 0xA001 for IBM or 0x1021 for CCITT
 *
 * Returns the cyclic redundancy check value for the given memory buffer.
 *
 * Returns: CRC value
 *
 * Since: 1.8.2
 **/
guint16
fu_crc16_full(const guint8 *buf, gsize bufsz, guint16 crc, guint16 polynomial)
{
	for (gsize len = bufsz; len > 0; len--) {
		crc = (guint16)(crc ^ (*buf++));
		for (guint8 i = 0; i < 8; i++) {
			if (crc & 0x1) {
				crc = (crc >> 1) ^ polynomial;
			} else {
				crc >>= 1;
			}
		}
	}
	return ~crc;
}

/**
 * fu_crc16:
 * @buf: memory buffer
 * @bufsz: size of @buf
 *
 * Returns the CRC-16-IBM cyclic redundancy value for the given memory buffer.
 *
 * Returns: CRC value
 *
 * Since: 1.8.2
 **/
guint16
fu_crc16(const guint8 *buf, gsize bufsz)
{
	return fu_crc16_full(buf, bufsz, 0xFFFF, 0xA001);
}

/**
 * fu_crc32_full:
 * @buf: memory buffer
 * @bufsz: size of @buf
 * @crc: initial CRC value, typically 0xFFFFFFFF
 * @polynomial: CRC polynomial, typically 0xEDB88320
 *
 * Returns the cyclic redundancy check value for the given memory buffer.
 *
 * Returns: CRC value
 *
 * Since: 1.8.2
 **/
guint32
fu_crc32_full(const guint8 *buf, gsize bufsz, guint32 crc, guint32 polynomial)
{
	for (guint32 idx = 0; idx < bufsz; idx++) {
		guint8 data = *buf++;
		crc = crc ^ data;
		for (guint32 bit = 0; bit < 8; bit++) {
			guint32 mask = -(crc & 1);
			crc = (crc >> 1) ^ (polynomial & mask);
		}
	}
	return ~crc;
}

/**
 * fu_crc32:
 * @buf: memory buffer
 * @bufsz: size of @buf
 *
 * Returns the cyclic redundancy check value for the given memory buffer.
 *
 * Returns: CRC value
 *
 * Since: 1.8.2
 **/
guint32
fu_crc32(const guint8 *buf, gsize bufsz)
{
	return fu_crc32_full(buf, bufsz, 0xFFFFFFFF, 0xEDB88320);
}
