/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-crc.h"

G_BEGIN_DECLS

guint32
fu_crc32_step(FuCrcKind kind, const guint8 *buf, gsize bufsz, guint32 crc) G_GNUC_PURE;
guint32
fu_crc32_done(FuCrcKind kind, guint32 crc) G_GNUC_PURE;
guint32
fu_crc32_fast(const guint8 *buf, gsize bufsz, guint32 crc) G_GNUC_PURE;

guint16
fu_crc16_step(FuCrcKind kind, const guint8 *buf, gsize bufsz, guint16 crc) G_GNUC_PURE;
guint16
fu_crc16_done(FuCrcKind kind, guint16 crc) G_GNUC_PURE;

guint8
fu_crc8_step(FuCrcKind kind, const guint8 *buf, gsize bufsz, guint8 crc) G_GNUC_PURE;
guint8
fu_crc8_done(FuCrcKind kind, guint8 crc) G_GNUC_PURE;

G_END_DECLS
