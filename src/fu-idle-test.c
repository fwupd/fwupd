/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-idle.h"

static void
fu_idle_func(void)
{
	guint token;
	g_autoptr(FuIdle) idle = fu_idle_new();

	fu_idle_reset(idle);
	g_assert_false(fu_idle_has_inhibit(idle, FU_IDLE_INHIBIT_TIMEOUT));
	g_assert_false(fu_idle_has_inhibit(idle, FU_IDLE_INHIBIT_SIGNALS));

	token = fu_idle_inhibit(idle, FU_IDLE_INHIBIT_TIMEOUT | FU_IDLE_INHIBIT_SIGNALS, NULL);
	g_assert_true(fu_idle_has_inhibit(idle, FU_IDLE_INHIBIT_TIMEOUT));
	g_assert_true(fu_idle_has_inhibit(idle, FU_IDLE_INHIBIT_SIGNALS));

	/* wrong token */
	fu_idle_uninhibit(idle, token + 1);
	g_assert_true(fu_idle_has_inhibit(idle, FU_IDLE_INHIBIT_SIGNALS));

	/* correct token */
	fu_idle_uninhibit(idle, token);
	g_assert_false(fu_idle_has_inhibit(idle, FU_IDLE_INHIBIT_TIMEOUT));
	g_assert_false(fu_idle_has_inhibit(idle, FU_IDLE_INHIBIT_SIGNALS));

	/* locker section */
	{
		g_autoptr(FuIdleLocker) idle_locker1 =
		    fu_idle_locker_new(idle, FU_IDLE_INHIBIT_TIMEOUT, NULL);
		g_autoptr(FuIdleLocker) idle_locker2 =
		    fu_idle_locker_new(idle, FU_IDLE_INHIBIT_SIGNALS, NULL);
		g_assert_nonnull(idle_locker1);
		g_assert_nonnull(idle_locker2);
		g_assert_true(fu_idle_has_inhibit(idle, FU_IDLE_INHIBIT_TIMEOUT));
		g_assert_true(fu_idle_has_inhibit(idle, FU_IDLE_INHIBIT_SIGNALS));
	}
	g_assert_false(fu_idle_has_inhibit(idle, FU_IDLE_INHIBIT_TIMEOUT));
	g_assert_false(fu_idle_has_inhibit(idle, FU_IDLE_INHIBIT_SIGNALS));
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/idle", fu_idle_func);
	return g_test_run();
}
