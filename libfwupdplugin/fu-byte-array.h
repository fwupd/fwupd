/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

#include "fu-endian.h"

gchar *
fu_byte_array_to_string(GByteArray *array) G_GNUC_NON_NULL(1);
GByteArray *
fu_byte_array_from_string(const gchar *str, GError **error) G_GNUC_NON_NULL(1);
void
fu_byte_array_set_size(GByteArray *array, gsize length, guint8 data) G_GNUC_NON_NULL(1);
void
fu_byte_array_align_up(GByteArray *array, guint8 alignment, guint8 data) G_GNUC_NON_NULL(1);
void
fu_byte_array_append_uint8(GByteArray *array, guint8 data) G_GNUC_NON_NULL(1);
void
fu_byte_array_append_uint16(GByteArray *array, guint16 data, FuEndianType endian)
    G_GNUC_NON_NULL(1);
void
fu_byte_array_append_uint24(GByteArray *array, guint32 data, FuEndianType endian)
    G_GNUC_NON_NULL(1);
void
fu_byte_array_append_uint32(GByteArray *array, guint32 data, FuEndianType endian)
    G_GNUC_NON_NULL(1);
void
fu_byte_array_append_uint64(GByteArray *array, guint64 data, FuEndianType endian)
    G_GNUC_NON_NULL(1);
void
fu_byte_array_append_bytes(GByteArray *array, GBytes *bytes) G_GNUC_NON_NULL(1, 2);
gboolean
fu_byte_array_compare(GByteArray *buf1, GByteArray *buf2, GError **error) G_GNUC_NON_NULL(1, 2);
