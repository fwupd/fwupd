/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-ccgx-common.h"

gchar *
fu_ccgx_version_to_string(guint32 val)
{
	/* 16 bits: application type [LSB]
	 *  8 bits: build number
	 *  4 bits: minor version
	 *  4 bits: major version [MSB] */
	return g_strdup_printf("%u.%u.%u",
			       (val >> 28) & 0x0f,
			       (val >> 24) & 0x0f,
			       (val >> 16) & 0xff);
}

gchar *
fu_ccgx_detailed_version_to_string(guint32 val)
{
	/* 16 bits: application type [LSB]
	 *  8 bits: build number
	 *  4 bits: minor version
	 *  4 bits: major version [MSB] */
	return g_strdup_printf("%u.%u.%u Build %u",
			       (val >> 28) & 0x0f,
			       (val >> 24) & 0x0f,
			       (val >> 16) & 0xff,
			       val & 0xff);
}

FuCcgxFwMode
fu_ccgx_fw_mode_get_alternate(FuCcgxFwMode val)
{
	if (val == FU_CCGX_FW_MODE_FW1)
		return FU_CCGX_FW_MODE_FW2;
	if (val == FU_CCGX_FW_MODE_FW2)
		return FU_CCGX_FW_MODE_FW1;
	return FU_CCGX_FW_MODE_BOOT;
}
