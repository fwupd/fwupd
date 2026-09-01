/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-test.h"

static void
fu_secure_bytes_func(void)
{
	g_autoptr(FuSecureBytes) sbytes =
	    fu_secure_bytes_new((guint8 *)g_strdup("secret"), 6, g_free);
	g_assert_cmpint(fu_secure_bytes_get_size(sbytes), ==, 6);
}

static void
fu_secure_bytes_take_func(void)
{
	guint8 buf[] = {'s', 'e', 'c', 'r', 'e', 't'};
	g_autoptr(FuSecureBytes) sbytes = fu_secure_bytes_new(buf, 6, NULL);
	g_assert_cmpint(fu_secure_bytes_get_size(sbytes), ==, 6);
	g_clear_pointer(&sbytes, fu_secure_bytes_free);
	for (guint i = 0; i < sizeof(buf); i++)
		g_assert_cmpint(buf[i], ==, 0x0);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/secure-bytes", fu_secure_bytes_func);
	g_test_add_func("/fwupd/secure-bytes/take", fu_secure_bytes_take_func);
	return g_test_run();
}
