/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <fwupd.h>
#include <glib-object.h>

#include "lu-common.h"

static void
lu_common_func (void)
{
	guint8 u8;
	guint16 u16;
	g_autofree gchar *ver1 = NULL;

	u8 = lu_buffer_read_uint8 ("12");
	g_assert_cmpint (u8, ==, 0x12);
	u16 = lu_buffer_read_uint16 ("1234");
	g_assert_cmpint (u16, ==, 0x1234);

	ver1 = lu_format_version (" A ", 0x87, 0x65, 0x4321);
	g_assert_cmpstr (ver1, ==, "A87.65_B4321");
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* tests go here */
	g_test_add_func ("/unifying/common", lu_common_func);
	return g_test_run ();
}
