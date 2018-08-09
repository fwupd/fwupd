/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-ucs2.h"

#define ev_bits(val, mask, shift)	(((val) & ((mask) << (shift))) >> (shift))

gchar *
fu_ucs2_to_uft8 (const guint16 *str, gssize max)
{
	gssize i, j;
	gchar *ret;

	if (max < 0)
		max = fu_ucs2_strlen (str, max);
	ret = g_malloc0 (max * 3 + 1); /* would be s/3/6 if this were UCS-4 */
	for (i = 0, j = 0; i < max && str[i]; i++, j++) {
		if (str[i] <= 0x7f) {
			ret[j] = str[i];
		} else if (str[i] > 0x7f && str[i] <= 0x7ff) {
			ret[j++] = 0xc0 | ev_bits(str[i], 0x1f, 6);
			ret[j]   = 0x80 | ev_bits(str[i], 0x3f, 0);
		} else if (str[i] > 0x7ff /* && str[i] < 0x10000 */ ) {
			ret[j++] = 0xe0 | ev_bits(str[i], 0xf, 12);
			ret[j++] = 0x80 | ev_bits(str[i], 0x3f, 6);
			ret[j]   = 0x80 | ev_bits(str[i], 0x3f, 0);
		}
	}
	return ret;
}

guint16 *
fu_uft8_to_ucs2 (const gchar *str, gssize max)
{
	gssize i, j;
	guint16 *ret = g_new0 (guint16, g_utf8_strlen (str, max) + 1);
	for (i = 0, j = 0; i < (max >= 0 ? max : i + 1) && str[i] != '\0'; j++) {
		guint32 val = 0;
		if ((str[i] & 0xe0) == 0xe0 && !(str[i] & 0x10)) {
			val = ((str[i+0] & 0x0f) << 10)
			     |((str[i+1] & 0x3f) << 6)
			     |((str[i+2] & 0x3f) << 0);
			i += 3;
		} else if ((str[i] & 0xc0) == 0xc0 && !(str[i] & 0x20)) {
			val = ((str[i+0] & 0x1f) << 6)
			     |((str[i+1] & 0x3f) << 0);
			i += 2;
		} else {
			val = str[i] & 0x7f;
			i += 1;
		}
		ret[j] = val;
	}
	ret[j] = L'\0';
	return ret;
}

gsize
fu_ucs2_strlen (const guint16 *str, gssize limit)
{
	gssize i;
	for (i = 0; i < (limit >= 0 ? limit : i + 1) && str[i] != L'\0'; i++);
	return i;
}
