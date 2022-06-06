/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-common.h"

guint8
fu_crc8(const guint8 *buf, gsize bufsz);
guint8
fu_crc8_full(const guint8 *buf, gsize bufsz, guint8 crc_init, guint8 polynomial);
guint16
fu_crc16(const guint8 *buf, gsize bufsz);
guint16
fu_crc16_full(const guint8 *buf, gsize bufsz, guint16 crc, guint16 polynomial);
guint32
fu_crc32(const guint8 *buf, gsize bufsz);
guint32
fu_crc32_full(const guint8 *buf, gsize bufsz, guint32 crc, guint32 polynomial);
