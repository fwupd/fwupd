/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-polkit-agent.h"

static void
fu_polkit_agent_func(void)
{
	gboolean ret;
	g_autoptr(FuPolkitAgent) polkit_agent = fu_polkit_agent_new();
	g_autoptr(GError) error = NULL;

	ret = fu_polkit_agent_open(polkit_agent, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/polkit-agent", fu_polkit_agent_func);
	return g_test_run();
}
