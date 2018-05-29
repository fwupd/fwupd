/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
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
