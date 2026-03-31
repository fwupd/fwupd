/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-crc.h"

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
fu_crc32_step(FuCrcKind kind, const guint8 *buf, gsize bufsz, guint32 crc);
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
fu_crc32_done(FuCrcKind kind, guint32 crc);
/**
 * fu_crc32_fast:
 * @buf: memory buffer
 * @bufsz: size of @buf
 * @crc: initial CRC value
 *
 * Returns the cyclic redundancy check value using the zlib hand-crafted assembly with pre-computed
 * tables for raw speed. It must always be %FU_CRC_KIND_B32_STANDARD with no inverted start value.
 *
 * Returns: CRC value
 *
 * Since: 2.1.1
 **/
guint32
fu_crc32_fast(const guint8 *buf, gsize bufsz, guint32 crc);

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
fu_crc16_step(FuCrcKind kind, const guint8 *buf, gsize bufsz, guint16 crc);
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
fu_crc16_done(FuCrcKind kind, guint16 crc);

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
fu_crc8_step(FuCrcKind kind, const guint8 *buf, gsize bufsz, guint8 crc);
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
fu_crc8_done(FuCrcKind kind, guint8 crc);
