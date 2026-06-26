/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupd.h>

#include "fu-polkit-agent.h"

static void
fu_polkit_agent_func(void)
{
	gboolean ret;
	g_autoptr(FuPathStore) pstore = fu_path_store_new();
	g_autoptr(FuPolkitAgent) polkit_agent = fu_polkit_agent_new();
	g_autoptr(GError) error = NULL;

	fu_path_store_load_from_env(pstore);
	ret = fu_polkit_agent_open(polkit_agent, pstore, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_polkit_agent_open_idempotent_func(void)
{
	gboolean ret;
	g_autoptr(FuPathStore) pstore = fu_path_store_new();
	g_autoptr(FuPolkitAgent) polkit_agent = fu_polkit_agent_new();
	g_autoptr(GError) error = NULL;

	fu_path_store_load_from_env(pstore);
	ret = fu_polkit_agent_open(polkit_agent, pstore, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_polkit_agent_open(polkit_agent, pstore, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_polkit_agent_open_no_pkttyagent_func(void)
{
	gboolean ret;
	g_autoptr(FuPathStore) pstore = fu_path_store_new();
	g_autoptr(FuPolkitAgent) polkit_agent = fu_polkit_agent_new();
	g_autoptr(GError) error = NULL;

	fu_path_store_add_program_path(pstore, "/nonexistent");
	ret = fu_polkit_agent_open(polkit_agent, pstore, &error);
	g_assert_false(ret);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/polkit-agent", fu_polkit_agent_func);
	g_test_add_func("/fwupd/polkit-agent/open/idempotent",
			fu_polkit_agent_open_idempotent_func);
	g_test_add_func("/fwupd/polkit-agent/open/no-pkttyagent",
			fu_polkit_agent_open_no_pkttyagent_func);
	return g_test_run();
}
