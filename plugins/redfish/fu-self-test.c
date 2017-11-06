/* -*- mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
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

#include "fu-plugin-private.h"
#include "fu-test.h"

#include "redfish-common.h"

static void
fu_test_redfish_common_func (void)
{
	const guint8 buf[16] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
				 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f  };
	g_autofree gchar *ipv4 = NULL;
	g_autofree gchar *ipv6 = NULL;

	ipv4 = redfish_common_buffer_to_ipv4 (buf);
	g_assert_cmpstr (ipv4, ==, "0.1.2.3");
	ipv6 = redfish_common_buffer_to_ipv6 (buf);
	g_assert_cmpstr (ipv6, ==, "00010203:04050607:08090a0b:0c0d0e0f");
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	g_test_add_func ("/redfish/common", fu_test_redfish_common_func);
	return g_test_run ();
}
