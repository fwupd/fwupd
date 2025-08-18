/*
 * Copyright 2025 Joe Hong <JoeHung@ilitek.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-ilitek-its-common.h"

guint16
fu_ilitek_its_get_crc(GBytes *blob, gsize count)
{
	guint16 crc = 0;
	const guint16 polynomial = 0x8408;
	gsize sz = 0;
	const guint8 *data = g_bytes_get_data(blob, &sz);

	if (sz < count)
		return 0;

	for (gsize i = 0; i < count; i++) {
		crc ^= data[i];
		for (guint8 idx = 0; idx < 8; idx++) {
			if (crc & 0x01)
				crc = (crc >> 1) ^ polynomial;
			else
				crc = crc >> 1;
		}
	}

	return crc;
}
