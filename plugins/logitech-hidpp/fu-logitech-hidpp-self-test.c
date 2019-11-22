/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>
#include <glib-object.h>

#include "fu-logitech-hidpp-common.h"

static void
fu_logitech_hidpp_common (void)
{
	guint8 u8;
	guint16 u16;
	g_autofree gchar *ver1 = NULL;

	u8 = fu_logitech_hidpp_buffer_read_uint8 ("12");
	g_assert_cmpint (u8, ==, 0x12);
	u16 = fu_logitech_hidpp_buffer_read_uint16 ("1234");
	g_assert_cmpint (u16, ==, 0x1234);

	ver1 = fu_logitech_hidpp_format_version (" A ", 0x87, 0x65, 0x4321);
	g_assert_cmpstr (ver1, ==, "A87.65_B4321");
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* tests go here */
	g_test_add_func ("/unifying/common", fu_logitech_hidpp_common);
	return g_test_run ();
}
