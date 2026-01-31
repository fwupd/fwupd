/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-remote-private.h"
#include "fwupd-test.h"

static void
fwupd_remote_func(void)
{
	g_autofree gchar *uri1 = NULL;
	g_autofree gchar *uri2 = NULL;
	g_autofree gchar *uri3 = NULL;
	g_autoptr(FwupdRemote) remote = fwupd_remote_new();
	g_autoptr(GError) error = NULL;

	uri1 = fwupd_remote_build_firmware_uri(remote,
					       "https://example.org/downloads/foo.cab",
					       &error);
	g_assert_no_error(error);
	g_assert_cmpstr(uri1, ==, "https://example.org/downloads/foo.cab");

	fwupd_remote_set_firmware_base_uri(remote, "https://example.org/mirror");
	uri2 = fwupd_remote_build_firmware_uri(remote,
					       "https://example.org/downloads/foo.cab",
					       &error);
	g_assert_no_error(error);
	g_assert_cmpstr(uri2, ==, "https://example.org/mirror/foo.cab");

	fwupd_remote_set_username(remote, "admin");
	uri3 = fwupd_remote_build_firmware_uri(remote,
					       "https://example.org/downloads/foo.cab",
					       &error);
	g_assert_no_error(error);
	g_assert_cmpstr(uri3, ==, "https://admin@example.org/mirror/foo.cab/auth");
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/remote", fwupd_remote_func);
	return g_test_run();
}
