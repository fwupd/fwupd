/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuCommon"

#include "config.h"

#include "fu-common.h"
#include "fu-crc-private.h"
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

const struct {
	FuCrc32Kind kind;
	guint32 poly;
	guint32 init;
	gboolean reflected;
	gboolean inverted;
} crc32_map[] = {
    {FU_CRC32_KIND_UNKNOWN, 0x00000000, 0x00000000, TRUE, TRUE},
    {FU_CRC32_KIND_STANDARD, 0x04C11DB7, 0xFFFFFFFF, TRUE, TRUE},
    {FU_CRC32_KIND_BZIP2, 0x04C11DB7, 0xFFFFFFFF, FALSE, TRUE},
    {FU_CRC32_KIND_JAMCRC, 0x04C11DB7, 0xFFFFFFFF, TRUE, FALSE},
    {FU_CRC32_KIND_MPEG2, 0x04C11DB7, 0xFFFFFFFF, FALSE, FALSE},
    {FU_CRC32_KIND_POSIX, 0x04C11DB7, 0x00000000, FALSE, TRUE},
    {FU_CRC32_KIND_SATA, 0x04C11DB7, 0x52325032, FALSE, FALSE},
    {FU_CRC32_KIND_XFER, 0x000000AF, 0x00000000, FALSE, FALSE},
    {FU_CRC32_KIND_C, 0x1EDC6F41, 0xFFFFFFFF, TRUE, TRUE},
    {FU_CRC32_KIND_D, 0xA833982B, 0xFFFFFFFF, TRUE, TRUE},
    {FU_CRC32_KIND_Q, 0x814141AB, 0x00000000, FALSE, FALSE},
};

static guint8
fu_crc_reflect8(guint8 data)
{
	guint8 val = 0;
	for (guint8 bit = 0; bit < 8; bit++) {
		if (data & 0x01)
			FU_BIT_SET(val, 7 - bit);
		data = (data >> 1);
	}
	return val;
}

static guint32
fu_crc_reflect32(guint32 data)
{
	guint32 val = 0;
	for (guint8 bit = 0; bit < 32; bit++) {
		if (data & 0x01)
			val |= 1ul << (31 - bit);
		data = (data >> 1);
	}
	return val;
}

/**
 * fu_crc32_step:
 * @kind: a #FuCrc32Kind, typically %FU_CRC32_KIND_STANDARD
 * @buf: memory buffer
 * @bufsz: size of @buf
 * @crc: initial CRC value
 *
 * Computes the cyclic redundancy check section value for the given memory buffer.
 *
 * NOTE: When all data has been added, you should call fu_crc32_done() to return the final value.
 *
 * Returns: CRC value
 *
 * Since: 2.0.0
 **/
guint32
fu_crc32_step(FuCrc32Kind kind, const guint8 *buf, gsize bufsz, guint32 crc)
{
	g_return_val_if_fail(kind < FU_CRC32_KIND_LAST, 0x0);

	for (gsize i = 0; i < bufsz; ++i) {
		guint32 tmp = crc32_map[kind].reflected ? fu_crc_reflect8(buf[i]) : buf[i];
		crc ^= tmp << 24;
		for (guint8 bit = 0; bit < 8; bit++) {
			if (crc & (1ul << 31)) {
				crc = (crc << 1) ^ crc32_map[kind].poly;
			} else {
				crc = (crc << 1);
			}
		}
	}
	return crc;
}

/**
 * fu_crc32_done:
 * @kind: a #FuCrc32Kind, typically %FU_CRC32_KIND_STANDARD
 * @crc: initial CRC value
 *
 * Returns the finished cyclic redundancy check value.
 *
 * Returns: CRC value
 *
 * Since: 2.0.0
 **/
guint32
fu_crc32_done(FuCrc32Kind kind, guint32 crc)
{
	g_return_val_if_fail(kind < FU_CRC32_KIND_LAST, 0x0);
	crc = crc32_map[kind].reflected ? fu_crc_reflect32(crc) : crc;
	if (crc32_map[kind].inverted)
		crc ^= 0xFFFFFFFF;
	return crc;
}

/**
 * fu_crc32:
 * @kind: a #FuCrc32Kind, typically %FU_CRC32_KIND_STANDARD
 * @buf: memory buffer
 * @bufsz: size of @buf
 *
 * Returns the cyclic redundancy check value for the given memory buffer.
 *
 * Returns: CRC value
 *
 * Since: 2.0.0
 **/
guint32
fu_crc32(FuCrc32Kind kind, const guint8 *buf, gsize bufsz)
{
	g_return_val_if_fail(kind < FU_CRC32_KIND_LAST, 0x0);
	return fu_crc32_done(kind, fu_crc32_step(kind, buf, bufsz, crc32_map[kind].init));
}

/**
 * fu_crc32_find:
 * @buf: memory buffer
 * @bufsz: size of @buf
 * @crc_target: "correct" CRC32 value
 *
 * Returns the cyclic redundancy kind for the given memory buffer and target CRC.
 *
 * You can use a very simple buffer to discover most types of standard CRC-32:
 *
 *    guint8 buf[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
 *    g_info("CRC:%u", fu_crc32_find(buf, sizeof(buf), _custom_crc(buf, sizeof(buf))));
 *
 * Returns: a #FuCrc32Kind, or %FU_CRC32_KIND_UNKNOWN on error
 *
 * Since: 2.0.0
 **/
FuCrc32Kind
fu_crc32_find(const guint8 *buf, gsize bufsz, guint32 crc_target)
{
	for (guint i = 0; i < G_N_ELEMENTS(crc32_map); i++) {
		guint32 crc_tmp = fu_crc32(crc32_map[i].kind, buf, bufsz);
		if (crc_tmp == crc_target)
			return crc32_map[i].kind;
	}
	return FU_CRC32_KIND_UNKNOWN;
}

static guint16
fu_crc_misr16_step(guint16 cur, guint16 new)
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
 * fu_crc_misr16:
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
fu_crc_misr16(guint16 init, const guint8 *buf, gsize bufsz)
{
	g_return_val_if_fail(buf != NULL, G_MAXUINT16);
	g_return_val_if_fail(bufsz % 2 == 0, G_MAXUINT16);

	for (gsize i = 0; i < bufsz; i += 2)
		init = fu_crc_misr16_step(init, fu_memread_uint16(buf + i, G_LITTLE_ENDIAN));
	return init;
}
