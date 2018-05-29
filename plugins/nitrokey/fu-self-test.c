/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>

#include "fu-nitrokey-common.h"

static void
fu_nitrokey_func (void)
{
	const guint8 buf[] =  { 0x00, 0x01, 0x02, 0x03,
				0x04, 0x05, 0x06, 0x07,
				0x08, 0x09, 0x0a, 0x0b,
				0x0c, 0x0d, 0x0e, 0x0f };
	g_assert_cmpint (fu_nitrokey_perform_crc32 (buf, 16), ==, 0x081B46CA);
	g_assert_cmpint (fu_nitrokey_perform_crc32 (buf, 15), ==, 0xED7320AB);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* tests go here */
	g_test_add_func ("/fwupd/nitrokey", fu_nitrokey_func);
	return g_test_run ();
}
