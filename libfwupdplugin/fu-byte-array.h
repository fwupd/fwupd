/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-common.h"

void
fu_byte_array_set_size(GByteArray *array, guint length, guint8 data);
void
fu_byte_array_align_up(GByteArray *array, guint8 alignment, guint8 data);
void
fu_byte_array_append_uint8(GByteArray *array, guint8 data);
void
fu_byte_array_append_uint16(GByteArray *array, guint16 data, FuEndianType endian);
void
fu_byte_array_append_uint32(GByteArray *array, guint32 data, FuEndianType endian);
void
fu_byte_array_append_uint64(GByteArray *array, guint64 data, FuEndianType endian);
void
fu_byte_array_append_bytes(GByteArray *array, GBytes *bytes);
gboolean
fu_byte_array_compare(GByteArray *buf1, GByteArray *buf2, GError **error);
