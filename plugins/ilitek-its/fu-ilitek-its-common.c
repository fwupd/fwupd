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

gchar *
fu_ilitek_its_convert_version(guint64 version_raw)
{
	/* convert 8 byte version in to human readable format. e.g. convert 0x0700000101020304 into
	 * 0700.0001.0102.0304*/
	return g_strdup_printf("%02x%02x.%02x%02x.%02x%02x.%02x%02x",
			       (guint)((version_raw >> 56) & 0xFF),
			       (guint)((version_raw >> 48) & 0xFF),
			       (guint)((version_raw >> 40) & 0xFF),
			       (guint)((version_raw >> 32) & 0xFF),
			       (guint)((version_raw >> 24) & 0xFF),
			       (guint)((version_raw >> 16) & 0xFF),
			       (guint)((version_raw >> 8) & 0xFF),
			       (guint)((version_raw >> 0) & 0xFF));
}
