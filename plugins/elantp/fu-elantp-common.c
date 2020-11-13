/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-elantp-common.h"

guint16
fu_elantp_calc_checksum (const guint8 *data, gsize length)
{
	guint16 checksum = 0;
	for (gsize i = 0; i < length; i += 2)
		checksum += ((guint16) (data[i+1]) << 8) | (data[i]);
	return checksum;
}
