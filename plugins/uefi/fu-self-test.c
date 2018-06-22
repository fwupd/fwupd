/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>
#include <glib-object.h>

#include "fu-test.h"
#include "fu-ucs2.h"

static void
fu_uefi_ucs2_func (void)
{
	g_autofree guint16 *str1 = NULL;
	g_autofree gchar *str2 = NULL;
	str1 = fu_uft8_to_ucs2 ("hw!", -1);
	g_assert_cmpint (fu_ucs2_strlen (str1, -1), ==, 3);
	str2 = fu_ucs2_to_uft8 (str1, -1);
	g_assert_cmpstr ("hw!", ==, str2);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* tests go here */
	g_test_add_func ("/uefi/ucs2", fu_uefi_ucs2_func);
	return g_test_run ();
}
