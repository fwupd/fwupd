/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-console.h"

static void
fu_console_func(void)
{
	g_autoptr(FuConsole) console = fu_console_new();

	fu_console_set_status_length(console, 20);
	fu_console_set_percentage_length(console, 50);

	for (guint i = 0; i < 100; i++) {
		fu_console_set_progress(console, FWUPD_STATUS_DECOMPRESSING, i);
		g_usleep(10000);
	}
	fu_console_set_progress(console, FWUPD_STATUS_IDLE, 0);
	for (guint i = 0; i < 100; i++) {
		guint pc = (i > 25 && i < 75) ? 0 : i;
		fu_console_set_progress(console, FWUPD_STATUS_LOADING, pc);
		g_usleep(10000);
	}
	fu_console_set_progress(console, FWUPD_STATUS_IDLE, 0);

	for (guint i = 0; i < 5000; i++) {
		fu_console_set_progress(console, FWUPD_STATUS_LOADING, 0);
		g_usleep(1000);
	}
	fu_console_set_progress(console, FWUPD_STATUS_IDLE, 0);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	if (g_test_slow())
		g_test_add_func("/fwupd/console", fu_console_func);
	return g_test_run();
}
