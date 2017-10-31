/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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
