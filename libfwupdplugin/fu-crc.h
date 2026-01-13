/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

#include "fu-crc-struct.h"

guint
fu_crc_size(FuCrcKind kind);

guint32
fu_crc32(FuCrcKind kind, const guint8 *buf, gsize bufsz);
guint32
fu_crc32_bytes(FuCrcKind kind, GBytes *blob);
guint16
fu_crc16(FuCrcKind kind, const guint8 *buf, gsize bufsz);
guint16
fu_crc16_bytes(FuCrcKind kind, GBytes *blob);
guint8
fu_crc8(FuCrcKind kind, const guint8 *buf, gsize bufsz);
guint8
fu_crc8_bytes(FuCrcKind kind, GBytes *blob);

FuCrcKind
fu_crc_find(const guint8 *buf, gsize bufsz, guint32 crc_target);

guint16
fu_crc_misr16(guint16 init, const guint8 *buf, gsize bufsz);
