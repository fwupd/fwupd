/*
 * Copyright 2024 Maciej Borzecki <maciej.borzecki@canonical.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "fu-snapd-snap.h"

/**
 * fu_snapd_is_in_snap:
 *
 * Check whether the current process is running inside a snap.
 *
 * Returns: TRUE if current process is running inside a snap.
 **/
gboolean
fu_snapd_is_in_snap(void)
{
	if (getenv("SNAP") != NULL) {
		return TRUE;
	}
	return FALSE;
}
