/*
 * Copyright 2024 Randy Lai <randy.lai@weidahitech.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-weida-raw-common.h"

const gchar *
fu_weida_raw_strerror(guint8 code)
{
	if (code == 0)
		return "success";
	return NULL;
}

gboolean
fu_weida_raw_block_is_empty(const guint8 *data, gsize datasz)
{
	for (gsize i = 0; i < datasz; i++) {
		if (data[i] != 0xff)
			return FALSE;
	}
	return TRUE;
}

guint16
fu_weida_raw_misr_16b(guint16 current_value, guint16 new_value)
{
	guint16 a, b;
	guint16 bit0;
	guint16 y;

	a = current_value;
	b = new_value;
	bit0 = a ^ (b & 1);
	bit0 ^= a >> 1;
	bit0 ^= a >> 2;
	bit0 ^= a >> 4;
	bit0 ^= a >> 5;
	bit0 ^= a >> 7;
	bit0 ^= a >> 11;
	bit0 ^= a >> 15;
	y = (a << 1) ^ b;
	y = (y & ~1) | (bit0 & 1);

	return y;
}

guint16
fu_weida_raw_misr_for_halfwords(guint16 current_value, guint8 *buf, guint start, guint hword_count)
{
	guint16 checksum = current_value;
	for (guint i = 0; i < hword_count; i++) {
		guint16 x = (buf[start + 2 * i + 0] | (buf[start + 2 * i + 1] << 8));
		checksum = fu_weida_raw_misr_16b(checksum, x);
	}
	return checksum;
}

guint16
fu_weida_raw_misr_for_bytes(guint16 current_value, guint8 *bytes, guint start, guint size)
{
	guint16 checksum = current_value;

	if (size / 2 > 0)
		checksum = fu_weida_raw_misr_for_halfwords(checksum, bytes, start, size / 2);

	if ((size % 2) != 0)
		checksum = fu_weida_raw_misr_16b(checksum, bytes[start + size - 1]);

	return checksum;
}
