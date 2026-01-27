/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-test.h"

/* nocheck:static */
static GMainLoop *_test_loop = NULL;
static guint _test_loop_timeout_id = 0;

static gboolean
fu_test_hang_check_cb(gpointer user_data)
{
	g_main_loop_quit(_test_loop);
	_test_loop_timeout_id = 0;
	return G_SOURCE_REMOVE;
}

void
fu_test_loop_run_with_timeout(guint timeout_ms)
{
	g_assert_cmpint(_test_loop_timeout_id, ==, 0);
	g_assert_null(_test_loop);
	_test_loop = g_main_loop_new(NULL, FALSE);
	_test_loop_timeout_id = g_timeout_add(timeout_ms, fu_test_hang_check_cb, NULL);
	g_main_loop_run(_test_loop);
}

void
fu_test_loop_quit(void)
{
	if (_test_loop_timeout_id > 0) {
		g_source_remove(_test_loop_timeout_id);
		_test_loop_timeout_id = 0;
	}
	if (_test_loop != NULL) {
		g_main_loop_quit(_test_loop);
		g_main_loop_unref(_test_loop);
		_test_loop = NULL;
	}
}
