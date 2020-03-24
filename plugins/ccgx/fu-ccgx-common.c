/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-ccgx-common.h"

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

