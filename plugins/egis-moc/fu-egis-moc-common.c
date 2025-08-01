/*
 * Copyright 2025 Jason Huang <jason.huang@egistec.com>
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-egis-moc-common.h"

guint32
fu_egis_moc_checksum_add(guint32 csum, const guint8 *buf, gsize bufsz)
{
	g_autofree guint16 *buf16 = g_new0(guint16, (bufsz / 2) + 1);
	memcpy(buf16, buf, bufsz); /* nocheck:blocked */
	for (gsize i = 0; i < bufsz / 2; i++)
		csum += buf16[i];
	if (bufsz % 2)
		csum += buf[bufsz - 1];
	return csum;
}

static guint16
fu_egis_moc_checksum_fold(guint32 csum)
{
	while (csum > 0xFFFF)
		csum = (csum >> 16) + (csum & 0xFFFF);
	return (guint16)csum;
}

guint16
fu_egis_moc_checksum_finish(guint32 csum)
{
	return ~fu_egis_moc_checksum_fold(csum);
}
