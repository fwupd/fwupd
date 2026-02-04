/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-client-list.h"

static void
fu_client_list_func(void)
{
	g_autoptr(FuClient) client_find = NULL;
	g_autoptr(FuClient) client = NULL;
	g_autoptr(FuClient) client_orig = NULL;
	g_autoptr(FuClientList) client_list = fu_client_list_new(NULL);
	g_autoptr(GPtrArray) clients_empty = NULL;
	g_autoptr(GPtrArray) clients_full = NULL;

	/* ensure empty */
	clients_empty = fu_client_list_get_all(client_list);
	g_assert_cmpint(clients_empty->len, ==, 0);

	/* register a client, then find it */
	client_orig = fu_client_list_register(client_list, ":hello");
	g_assert_nonnull(client_orig);
	client_find = fu_client_list_get_by_sender(client_list, ":hello");
	g_assert_nonnull(client_find);
	g_assert_true(client_orig == client_find);
	clients_full = fu_client_list_get_all(client_list);
	g_assert_cmpint(clients_full->len, ==, 1);

	/* register a duplicate, check properties */
	client = fu_client_list_register(client_list, ":hello");
	g_assert_nonnull(client);
	g_assert_true(client_orig == client);
	g_assert_cmpstr(fu_client_get_sender(client), ==, ":hello");
	g_assert_cmpint(fu_client_get_feature_flags(client), ==, FWUPD_FEATURE_FLAG_NONE);
	g_assert_cmpstr(fu_client_lookup_hint(client, "key"), ==, NULL);
	g_assert_true(fu_client_has_flag(client, FU_CLIENT_FLAG_ACTIVE));
	fu_client_insert_hint(client, "key", "value");
	fu_client_set_feature_flags(client, FWUPD_FEATURE_FLAG_UPDATE_ACTION);
	g_assert_cmpstr(fu_client_lookup_hint(client, "key"), ==, "value");
	g_assert_cmpint(fu_client_get_feature_flags(client), ==, FWUPD_FEATURE_FLAG_UPDATE_ACTION);

	/* emulate disconnect */
	fu_client_remove_flag(client, FU_CLIENT_FLAG_ACTIVE);
	g_assert_false(fu_client_has_flag(client, FU_CLIENT_FLAG_ACTIVE));
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/client-list", fu_client_list_func);
	return g_test_run();
}
