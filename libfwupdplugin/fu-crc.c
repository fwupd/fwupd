/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuCommon"

#include "config.h"

#include "fu-crc.h"
#include "fu-mem.h"

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

static guint16
fu_misr16_step(guint16 cur, guint16 new)
{
	guint16 bit0;
	guint16 res;

	bit0 = cur ^ (new & 1);
	bit0 ^= cur >> 1;
	bit0 ^= cur >> 2;
	bit0 ^= cur >> 4;
	bit0 ^= cur >> 5;
	bit0 ^= cur >> 7;
	bit0 ^= cur >> 11;
	bit0 ^= cur >> 15;
	res = (cur << 1) ^ new;
	res = (res & ~1) | (bit0 & 1);
	return res;
}

/**
 * fu_misr16:
 * @buf: memory buffer
 * @bufsz: size of @buf
 * @init: initial value, typically 0x0
 *
 * Returns the MISR check value for the given memory buffer.
 *
 * Returns: value
 *
 * Since: 1.9.17
 **/
guint16
fu_misr16(guint16 init, const guint8 *buf, gsize bufsz)
{
	g_return_val_if_fail(buf != NULL, G_MAXUINT16);
	g_return_val_if_fail(bufsz % 2 == 0, G_MAXUINT16);

	for (gsize i = 0; i < bufsz; i += 2)
		init = fu_misr16_step(init, fu_memread_uint16(buf + i, G_LITTLE_ENDIAN));
	return init;
}
