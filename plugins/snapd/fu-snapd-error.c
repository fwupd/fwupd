/*
 * Copyright 2024 Maciej Borzecki <maciej.borzecki@canonical.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "fu-snapd-error.h"

/**
 * fu_snapd_error_quark:
 *
 * Returns: an error quark
 **/
GQuark
fu_snapd_error_quark(void)
{
	static GQuark quark = 0;
	if (!quark) {
		quark = g_quark_from_static_string("FuSnapdError");
	}
	return quark;
}
