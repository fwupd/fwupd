/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-systemd.h"

static void
fu_systemd_unit_stop_error_func(void)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;

	ret = fu_systemd_unit_stop("nonexistent-unit-XXXXXX.service", &error);
	g_assert_false(ret);
	if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
		g_test_skip("no systemd available");
		return;
	}
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/systemd/unit-stop/error", fu_systemd_unit_stop_error_func);
	return g_test_run();
}
