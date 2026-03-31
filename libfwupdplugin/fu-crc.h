/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

#include "fu-crc-struct.h"

/**
 * fu_crc_size:
 * @kind: a #FuCrcKind
 *
 * Returns the size of the CRC in bits.
 *
 * Returns: integer, or 0 on error
 *
 * Since: 2.0.19
 **/
guint
fu_crc_size(FuCrcKind kind);

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
fu_crc32(FuCrcKind kind, const guint8 *buf, gsize bufsz);
/**
 * fu_crc32_bytes:
 * @kind: a #FuCrcKind, typically %FU_CRC_KIND_B32_STANDARD
 * @blob: a #GBytes
 *
 * Returns the cyclic redundancy check value for the given memory buffer.
 *
 * Returns: CRC value
 *
 * Since: 2.0.2
 **/
guint32
fu_crc32_bytes(FuCrcKind kind, GBytes *blob);
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
fu_crc16(FuCrcKind kind, const guint8 *buf, gsize bufsz);
/**
 * fu_crc16_bytes:
 * @kind: a #FuCrcKind, typically %FU_CRC_KIND_B16_XMODEM
 * @blob: a #GBytes
 *
 * Returns the cyclic redundancy check value for the given memory buffer.
 *
 * Returns: CRC value
 *
 * Since: 2.0.2
 **/
guint16
fu_crc16_bytes(FuCrcKind kind, GBytes *blob);
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
fu_crc8(FuCrcKind kind, const guint8 *buf, gsize bufsz);
/**
 * fu_crc8_bytes:
 * @kind: a #FuCrcKind, typically %FU_CRC_KIND_B8_MAXIM_DOW
 * @blob: a #GBytes
 *
 * Returns the cyclic redundancy check value for the given memory buffer.
 *
 * Returns: CRC value
 *
 * Since: 2.0.2
 **/
guint8
fu_crc8_bytes(FuCrcKind kind, GBytes *blob);

/**
 * fu_crc_find:
 * @buf: memory buffer
 * @bufsz: size of @buf
 * @crc_target: "correct" CRC value
 * @kind: (out) (nullable): a #FuCrcKind, or %FU_CRC_KIND_UNKNOWN on error
 * @error: (nullable): optional return location for an error
 *
 * Returns the cyclic redundancy kind for the given memory buffer and target CRC.
 *
 * You can use a very simple buffer to discover most types of standard CRC-32:
 *
 *    guint8 buf[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
 *    g_info("CRC:%u", fu_crc_find(buf, sizeof(buf), _custom_crc(buf, sizeof(buf))));
 *
 * Returns: %TRUE if one well-known CRC kind was found.
 *
 * Since: 2.1.1
 **/
gboolean
fu_crc_find(const guint8 *buf, gsize bufsz, guint32 crc_target, FuCrcKind *kind, GError **error);

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
fu_crc_misr16(guint16 init, const guint8 *buf, gsize bufsz);
