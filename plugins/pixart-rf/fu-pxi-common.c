/*
 * Copyright (C) 2021 Jimmy Yu <Jimmy_yu@pixart.com>
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-pxi-common.h"

guint8
fu_pxi_common_sum8 (const guint8 *buf, gsize bufsz)
{
	guint8 checksum = 0;
	for (gsize idx = 0; idx < bufsz; idx++)
		checksum += (guint8) buf[idx];
	return checksum;
}

guint16
fu_pxi_common_sum16 (const guint8 *buf, gsize bufsz)
{
	guint16 checksum = 0;
	for (gsize idx = 0; idx < bufsz; idx++)
		checksum += (guint8) buf[idx];
	return checksum;
}
