/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-ccgx-common.h"

gchar *
fu_ccgx_version_to_string (guint32 val)
{
	/* 16 bits: application type [LSB]
	 *  8 bits: build number
	 *  4 bits: minor version
	 *  4 bits: major version [MSB] */
	return g_strdup_printf ("%u.%u.%u",
				(val >> 28) & 0x0f,
				(val >> 24) & 0x0f,
				(val >> 16) & 0xff);
}

const gchar *
fu_ccgx_fw_mode_to_string (FWMode val)
{
	if (val == FW_MODE_BOOT)
		return "BOOT";
	if (val == FW_MODE_FW1)
		return "FW1";
	if (val == FW_MODE_FW2)
		return "FW2";
	return NULL;
}

