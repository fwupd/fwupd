/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>

#include "fu-linux-swap.h"

static void
fu_linux_swap_none_func(void)
{
	g_autoptr(FuLinuxSwap) swap = NULL;
	g_autoptr(GError) error = NULL;

	swap = fu_linux_swap_new("Filename\t\t\t\tType\t\tSize\tUsed\tPriority\n", 0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(swap);
	g_assert_false(fu_linux_swap_get_enabled(swap));
	g_assert_false(fu_linux_swap_get_encrypted(swap));
}

static void
fu_linux_swap_plain_func(void)
{
	g_autoptr(FuLinuxSwap) swap = NULL;
	g_autoptr(GError) error = NULL;

	swap =
	    fu_linux_swap_new("Filename\t\t\t\tType\t\tSize\tUsed\tPriority\n"
			      "/dev/nvme0n1p4                          partition\t5962748\t0\t-2\n",
			      0,
			      &error);
	if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ||
	    g_error_matches(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT)) {
		g_test_skip(error->message);
		return;
	}
	if (g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN)) {
		g_test_skip(error->message);
		return;
	}
	g_assert_no_error(error);
	g_assert_nonnull(swap);
}

static void
fu_linux_swap_encrypted_func(void)
{
	g_autoptr(FuLinuxSwap) swap = NULL;
	g_autoptr(GError) error = NULL;

	swap =
	    fu_linux_swap_new("Filename\t\t\t\tType\t\tSize\tUsed\tPriority\n"
			      "/dev/dm-1                               partition\t5962748\t0\t-2\n",
			      0,
			      &error);
	if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ||
	    g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN) ||
	    g_error_matches(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT)) {
		g_test_skip(error->message);
		return;
	}
	g_assert_no_error(error);
	g_assert_nonnull(swap);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);

	/* tests go here */
	g_test_add_func("/linux-swap/none", fu_linux_swap_none_func);
	g_test_add_func("/linux-swap/plain", fu_linux_swap_plain_func);
	g_test_add_func("/linux-swap/encrypted", fu_linux_swap_encrypted_func);
	return g_test_run();
}
