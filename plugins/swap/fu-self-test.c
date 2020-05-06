/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>

#include "fu-swap.h"

#include "fwupd-error.h"

static void
fu_swap_none_func (void)
{
	g_autoptr(FuSwap) swap = NULL;
	g_autoptr(GError) error = NULL;

	swap = fu_swap_new ("Filename\t\t\t\tType\t\tSize\tUsed\tPriority\n", 0, &error);
	g_assert_no_error (error);
	g_assert_nonnull (swap);
	g_assert_false (fu_swap_get_enabled (swap));
	g_assert_false (fu_swap_get_encrypted (swap));
}

static void
fu_swap_plain_func (void)
{
	g_autoptr(FuSwap) swap = NULL;
	g_autoptr(GError) error = NULL;

	swap = fu_swap_new ("Filename\t\t\t\tType\t\tSize\tUsed\tPriority\n"
			    "/dev/nvme0n1p4  partition\t5962748\t0\t-2\n",
			    0, &error);
	g_assert_no_error (error);
	g_assert_nonnull (swap);
	g_assert_true (fu_swap_get_enabled (swap));
	g_assert_false (fu_swap_get_encrypted (swap));
}

static void
fu_swap_encrypted_func (void)
{
	g_autoptr(FuSwap) swap = NULL;
	g_autoptr(GError) error = NULL;

	swap = fu_swap_new ("Filename\t\t\t\tType\t\tSize\tUsed\tPriority\n"
			    "/dev/dm-1  partition\t5962748\t0\t-2\n",
			    0, &error);
	g_assert_no_error (error);
	g_assert_nonnull (swap);
	g_assert_true (fu_swap_get_enabled (swap));
	g_assert_true (fu_swap_get_encrypted (swap));
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

	/* tests go here */
	g_test_add_func ("/swap/none", fu_swap_none_func);
	g_test_add_func ("/swap/plain", fu_swap_plain_func);
	g_test_add_func ("/swap/encrypted", fu_swap_encrypted_func);
	return g_test_run ();
}
