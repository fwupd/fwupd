/*
 * Copyright (C) 2021 Texas Instruments Incorporated
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+ OR MIT
 */

#include "config.h"

#include "fu-ti-tps6598x-common.h"

gboolean
fu_ti_tps6598x_byte_array_is_nonzero(GByteArray *buf)
{
	if (buf->len == 0)
		return FALSE;
	for (guint j = 1; j < buf->len; j++) {
		if (buf->data[j] != 0x0)
			return TRUE;
	}
	return FALSE;
}
