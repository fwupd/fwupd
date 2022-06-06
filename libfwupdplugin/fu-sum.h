/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-common.h"

guint8
fu_sum8(const guint8 *buf, gsize bufsz);
guint8
fu_sum8_bytes(GBytes *blob);
guint16
fu_sum16(const guint8 *buf, gsize bufsz);
guint16
fu_sum16_bytes(GBytes *blob);
guint16
fu_sum16w(const guint8 *buf, gsize bufsz, FuEndianType endian);
guint16
fu_sum16w_bytes(GBytes *blob, FuEndianType endian);
guint32
fu_sum32(const guint8 *buf, gsize bufsz);
guint32
fu_sum32_bytes(GBytes *blob);
guint32
fu_sum32w(const guint8 *buf, gsize bufsz, FuEndianType endian);
guint32
fu_sum32w_bytes(GBytes *blob, FuEndianType endian);
