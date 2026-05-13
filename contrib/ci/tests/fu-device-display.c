/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: use fu_device_get_id_display
 */

static void
fu_device_display_func(void)
{
	g_debug("removing %s (%s)", fu_device_get_name(parent), fu_device_get_id(parent));
}
