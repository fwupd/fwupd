/*
 * Copyright (C) 2022 Andrii Dushko <andrii.dushko@developex.net>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-corsair-common.h"

guint32
fu_corsair_calculate_crc(const guint8 *data, guint32 data_len)
{
	gboolean bit;
	guint8 c;
	guint32 crc = 0xffffffff;

	while (data_len--) {
		c = *data++;
		for (guint i = 0x80; i > 0; i >>= 1) {
			bit = crc & 0x80000000;
			if (c & i) {
				bit = !bit;
			}
			crc <<= 1;
			if (bit) {
				crc ^= 0x04c11db7;
			}
		}
	}
	return crc;
}

/**
 * fu_corsair_version_from_uint32:
 * @val: version in corsair device format
 *
 * fu_version_from_uint32(... %FWUPD_VERSION_FORMAT_TRIPLET)
 * cannot be used because bytes in the version are in non-standard
 * order: 0xCCDD.BB.AA.
 *
 * Returns: a version number, e.g. `1.0.3`.
 **/
gchar *
fu_corsair_version_from_uint32(guint32 value)
{
	return g_strdup_printf("%u.%u.%u",
			       value & 0xff,
			       (value >> 8) & 0xff,
			       (value >> 16) & 0xffff);
}
