/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-nitrokey-common.h"

static guint32
fu_nitrokey_perform_crc32_mutate (guint32 crc, guint32 data)
{
	crc = crc ^ data;
	for (guint i = 0; i < 32; i++) {
		if (crc & 0x80000000) {
			/* polynomial used in STM32 */
			crc = (crc << 1) ^ 0x04C11DB7;
		} else {
			crc = (crc << 1);
		}
	}
	return crc;
}

guint32
fu_nitrokey_perform_crc32 (const guint8 *data, gsize size)
{
	guint32 crc = 0xffffffff;
	g_autofree guint32 *data_aligned = NULL;
	data_aligned = g_new0 (guint32, (size / 4) + 1);
	memcpy (data_aligned, data, size);
	for (gsize idx = 0; idx * 4 < size; idx++) {
		guint32 data_aligned_le = GUINT32_FROM_LE (data_aligned[idx]);
		crc = fu_nitrokey_perform_crc32_mutate (crc, data_aligned_le);
	}
	return crc;
}
