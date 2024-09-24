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

const struct {
	FuCrcKind kind;
	guint bitwidth;
	guint32 poly;
	guint32 init;
	gboolean reflected;
	guint32 xorout;
} crc_map[] = {
    {FU_CRC_KIND_UNKNOWN, 32, 0x00000000, 0x00000000, TRUE, 0xFFFFFFFF},
    {FU_CRC_KIND_B32_STANDARD, 32, 0x04C11DB7, 0xFFFFFFFF, TRUE, 0xFFFFFFFF},
    {FU_CRC_KIND_B32_BZIP2, 32, 0x04C11DB7, 0xFFFFFFFF, FALSE, 0xFFFFFFFF},
    {FU_CRC_KIND_B32_JAMCRC, 32, 0x04C11DB7, 0xFFFFFFFF, TRUE, 0x00000000},
    {FU_CRC_KIND_B32_MPEG2, 32, 0x04C11DB7, 0xFFFFFFFF, FALSE, 0x00000000},
    {FU_CRC_KIND_B32_POSIX, 32, 0x04C11DB7, 0x00000000, FALSE, 0xFFFFFFFF},
    {FU_CRC_KIND_B32_SATA, 32, 0x04C11DB7, 0x52325032, FALSE, 0x00000000},
    {FU_CRC_KIND_B32_XFER, 32, 0x000000AF, 0x00000000, FALSE, 0x00000000},
    {FU_CRC_KIND_B32_C, 32, 0x1EDC6F41, 0xFFFFFFFF, TRUE, 0xFFFFFFFF},
    {FU_CRC_KIND_B32_D, 32, 0xA833982B, 0xFFFFFFFF, TRUE, 0xFFFFFFFF},
    {FU_CRC_KIND_B32_Q, 32, 0x814141AB, 0x00000000, FALSE, 0x00000000},
    {FU_CRC_KIND_B16_XMODEM, 16, 0x1021, 0x0000, FALSE, 0x0000},
    {FU_CRC_KIND_B16_USB, 16, 0x8005, 0xFFFF, TRUE, 0xFFFF},
    {FU_CRC_KIND_B16_UMTS, 16, 0x8005, 0x0000, FALSE, 0x0000},
    {FU_CRC_KIND_B16_TMS37157, 16, 0x1021, 0x89ec, TRUE, 0x0000},
    {FU_CRC_KIND_B8_WCDMA, 8, 0x9B, 0x00, TRUE, 0x00},
    {FU_CRC_KIND_B8_TECH_3250, 8, 0x1D, 0xFF, TRUE, 0x00},
    {FU_CRC_KIND_B8_STANDARD, 8, 0x07, 0x00, FALSE, 0x00},
    {FU_CRC_KIND_B8_SAE_J1850, 8, 0x1D, 0xFF, FALSE, 0xFF},
    {FU_CRC_KIND_B8_ROHC, 8, 0x07, 0xFF, TRUE, 0x00},
    {FU_CRC_KIND_B8_OPENSAFETY, 8, 0x2F, 0x00, FALSE, 0x00},
    {FU_CRC_KIND_B8_NRSC_5, 8, 0x31, 0xFF, FALSE, 0x00},
    {FU_CRC_KIND_B8_MIFARE_MAD, 8, 0x1D, 0xC7, FALSE, 0x00},
    {FU_CRC_KIND_B8_MAXIM_DOW, 8, 0x31, 0x00, TRUE, 0x00},
    {FU_CRC_KIND_B8_LTE, 8, 0x9B, 0x00, FALSE, 0x00},
    {FU_CRC_KIND_B8_I_CODE, 8, 0x1D, 0xFD, FALSE, 0x00},
    {FU_CRC_KIND_B8_ITU, 8, 0x07, 0x00, FALSE, 0x55},
    {FU_CRC_KIND_B8_HITAG, 8, 0x1D, 0xFF, FALSE, 0x00},
    {FU_CRC_KIND_B8_GSM_B, 8, 0x49, 0x00, FALSE, 0xFF},
    {FU_CRC_KIND_B8_GSM_A, 8, 0x1D, 0x00, FALSE, 0x00},
    {FU_CRC_KIND_B8_DVB_S2, 8, 0xD5, 0x00, FALSE, 0x00},
    {FU_CRC_KIND_B8_DARC, 8, 0x39, 0x00, TRUE, 0x00},
    {FU_CRC_KIND_B8_CDMA2000, 8, 0x9B, 0xFF, FALSE, 0x00},
    {FU_CRC_KIND_B8_BLUETOOTH, 8, 0xA7, 0x00, TRUE, 0x00},
    {FU_CRC_KIND_B8_AUTOSAR, 8, 0x2F, 0xFF, FALSE, 0xFF},
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
fu_crc_reflect(guint32 data, guint bitwidth)
{
	guint32 val = 0;
	for (guint bit = 0; bit < bitwidth; bit++) {
		if (data & 0x01)
			val |= 1ul << ((bitwidth - 1) - bit);
		data = (data >> 1);
	}
	return val;
}

/**
 * fu_crc8_step:
 * @kind: a #FuCrcKind, typically %FU_CRC_KIND_B8_MAXIM_DOW
 * @buf: memory buffer
 * @bufsz: size of @buf
 * @crc: initial CRC value
 *
 * Computes the cyclic redundancy check section value for the given memory buffer.
 *
 * NOTE: When all data has been added, you should call fu_crc8_done() to return the final value.
 *
 * Returns: CRC value
 *
 * Since: 2.0.0
 **/
guint8
fu_crc8_step(FuCrcKind kind, const guint8 *buf, gsize bufsz, guint8 crc)
{
	const guint bitwidth = sizeof(crc) * 8;

	g_return_val_if_fail(kind < FU_CRC_KIND_LAST, 0x0);
	g_return_val_if_fail(crc_map[kind].bitwidth == 8, 0x0);

	for (gsize i = 0; i < bufsz; ++i) {
		crc ^= crc_map[kind].reflected ? fu_crc_reflect8(buf[i]) : buf[i];
		for (guint8 bit = 0; bit < 8; bit++) {
			if (crc & (1ul << (bitwidth - 1))) {
				crc = (crc << 1) ^ crc_map[kind].poly;
			} else {
				crc = (crc << 1);
			}
		}
	}
	return crc;
}

/**
 * fu_crc8_done:
 * @kind: a #FuCrcKind, typically %FU_CRC_KIND_B8_MAXIM_DOW
 * @crc: initial CRC value
 *
 * Returns the finished cyclic redundancy check value.
 *
 * Returns: CRC value
 *
 * Since: 2.0.0
 **/
guint8
fu_crc8_done(FuCrcKind kind, guint8 crc)
{
	g_return_val_if_fail(kind < FU_CRC_KIND_LAST, 0x0);
	g_return_val_if_fail(crc_map[kind].bitwidth == 8, 0x0);
	crc = crc_map[kind].reflected ? fu_crc_reflect(crc, crc_map[kind].bitwidth) : crc;
	return crc ^ crc_map[kind].xorout;
}

/**
 * fu_crc8:
 * @kind: a #FuCrcKind, typically %FU_CRC_KIND_B8_MAXIM_DOW
 * @buf: memory buffer
 * @bufsz: size of @buf
 *
 * Returns the cyclic redundancy check value for the given memory buffer.
 *
 * Returns: CRC value
 *
 * Since: 2.0.0
 **/
guint8
fu_crc8(FuCrcKind kind, const guint8 *buf, gsize bufsz)
{
	g_return_val_if_fail(kind < FU_CRC_KIND_LAST, 0x0);
	g_return_val_if_fail(crc_map[kind].bitwidth == 8, 0x0);
	return fu_crc8_done(kind, fu_crc8_step(kind, buf, bufsz, crc_map[kind].init));
}

/**
 * fu_crc16_step:
 * @kind: a #FuCrcKind, typically %FU_CRC_KIND_B16_XMODEM
 * @buf: memory buffer
 * @bufsz: size of @buf
 * @crc: initial CRC value
 *
 * Computes the cyclic redundancy check section value for the given memory buffer.
 *
 * NOTE: When all data has been added, you should call fu_crc16_done() to return the final value.
 *
 * Returns: CRC value
 *
 * Since: 2.0.0
 **/
guint16
fu_crc16_step(FuCrcKind kind, const guint8 *buf, gsize bufsz, guint16 crc)
{
	const guint bitwidth = sizeof(crc) * 8;

	g_return_val_if_fail(kind < FU_CRC_KIND_LAST, 0x0);
	g_return_val_if_fail(crc_map[kind].bitwidth == 16, 0x0);

	for (gsize i = 0; i < bufsz; ++i) {
		guint16 tmp = crc_map[kind].reflected ? fu_crc_reflect8(buf[i]) : buf[i];
		crc ^= tmp << (bitwidth - 8);
		for (guint8 bit = 0; bit < 8; bit++) {
			if (crc & (1ul << (bitwidth - 1))) {
				crc = (crc << 1) ^ crc_map[kind].poly;
			} else {
				crc = (crc << 1);
			}
		}
	}
	return crc;
}

/**
 * fu_crc16_done:
 * @kind: a #FuCrcKind, typically %FU_CRC_KIND_B16_XMODEM
 * @crc: initial CRC value
 *
 * Returns the finished cyclic redundancy check value.
 *
 * Returns: CRC value
 *
 * Since: 2.0.0
 **/
guint16
fu_crc16_done(FuCrcKind kind, guint16 crc)
{
	g_return_val_if_fail(kind < FU_CRC_KIND_LAST, 0x0);
	g_return_val_if_fail(crc_map[kind].bitwidth == 16, 0x0);
	crc = crc_map[kind].reflected ? fu_crc_reflect(crc, crc_map[kind].bitwidth) : crc;
	return crc ^ crc_map[kind].xorout;
}

/**
 * fu_crc16:
 * @kind: a #FuCrcKind, typically %FU_CRC_KIND_B16_XMODEM
 * @buf: memory buffer
 * @bufsz: size of @buf
 *
 * Returns the cyclic redundancy check value for the given memory buffer.
 *
 * Returns: CRC value
 *
 * Since: 2.0.0
 **/
guint16
fu_crc16(FuCrcKind kind, const guint8 *buf, gsize bufsz)
{
	g_return_val_if_fail(kind < FU_CRC_KIND_LAST, 0x0);
	g_return_val_if_fail(crc_map[kind].bitwidth == 16, 0x0);
	return fu_crc16_done(kind, fu_crc16_step(kind, buf, bufsz, crc_map[kind].init));
}

/**
 * fu_crc32_step:
 * @kind: a #FuCrcKind, typically %FU_CRC_KIND_B32_STANDARD
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
fu_crc32_step(FuCrcKind kind, const guint8 *buf, gsize bufsz, guint32 crc)
{
	const guint bitwidth = sizeof(crc) * 8;

	g_return_val_if_fail(kind < FU_CRC_KIND_LAST, 0x0);
	g_return_val_if_fail(crc_map[kind].bitwidth == 32, 0x0);

	for (gsize i = 0; i < bufsz; ++i) {
		guint32 tmp = crc_map[kind].reflected ? fu_crc_reflect8(buf[i]) : buf[i];
		crc ^= tmp << (bitwidth - 8);
		for (guint8 bit = 0; bit < 8; bit++) {
			if (crc & (1ul << (bitwidth - 1))) {
				crc = (crc << 1) ^ crc_map[kind].poly;
			} else {
				crc = (crc << 1);
			}
		}
	}
	return crc;
}

/**
 * fu_crc32_done:
 * @kind: a #FuCrcKind, typically %FU_CRC_KIND_B32_STANDARD
 * @crc: initial CRC value
 *
 * Returns the finished cyclic redundancy check value.
 *
 * Returns: CRC value
 *
 * Since: 2.0.0
 **/
guint32
fu_crc32_done(FuCrcKind kind, guint32 crc)
{
	g_return_val_if_fail(kind < FU_CRC_KIND_LAST, 0x0);
	g_return_val_if_fail(crc_map[kind].bitwidth == 32, 0x0);
	crc = crc_map[kind].reflected ? fu_crc_reflect(crc, crc_map[kind].bitwidth) : crc;
	return crc ^ crc_map[kind].xorout;
}

/**
 * fu_crc32:
 * @kind: a #FuCrcKind, typically %FU_CRC_KIND_B32_STANDARD
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
fu_crc32(FuCrcKind kind, const guint8 *buf, gsize bufsz)
{
	g_return_val_if_fail(kind < FU_CRC_KIND_LAST, 0x0);
	g_return_val_if_fail(crc_map[kind].bitwidth == 32, 0x0);
	return fu_crc32_done(kind, fu_crc32_step(kind, buf, bufsz, crc_map[kind].init));
}

/**
 * fu_crc_find:
 * @buf: memory buffer
 * @bufsz: size of @buf
 * @crc_target: "correct" CRC value
 *
 * Returns the cyclic redundancy kind for the given memory buffer and target CRC.
 *
 * You can use a very simple buffer to discover most types of standard CRC-32:
 *
 *    guint8 buf[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
 *    g_info("CRC:%u", fu_crc_find(buf, sizeof(buf), _custom_crc(buf, sizeof(buf))));
 *
 * Returns: a #FuCrcKind, or %FU_CRC_KIND_UNKNOWN on error
 *
 * Since: 2.0.0
 **/
FuCrcKind
fu_crc_find(const guint8 *buf, gsize bufsz, guint32 crc_target)
{
	for (guint i = 0; i < G_N_ELEMENTS(crc_map); i++) {
		if (crc_map[i].bitwidth == 32) {
			if (crc_target == fu_crc32(crc_map[i].kind, buf, bufsz))
				return crc_map[i].kind;
		}
		if (crc_map[i].bitwidth == 16) {
			if ((guint16)crc_target == fu_crc16(crc_map[i].kind, buf, bufsz))
				return crc_map[i].kind;
		}
		if (crc_map[i].bitwidth == 8) {
			if ((guint8)crc_target == fu_crc8(crc_map[i].kind, buf, bufsz))
				return crc_map[i].kind;
		}
	}
	return FU_CRC_KIND_UNKNOWN;
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
