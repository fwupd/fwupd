/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-fresco-pd-common.h"

static void
fu_fresco_pd_common_version_func(void)
{
	struct {
		guint8 buf[4];
		const gchar *ver;
	} map[] = {
	    {{1, 2, 3, 1}, "1.2.3.1"},
	    {{1, 2, 3, 2}, "1.2.3.2"},
	    {{1, 2, 3, 4}, "4.2.3.1"},
	    {{0, 0, 0, 0}, "0.0.0.0"},
	};
	for (guint i = 0; i < G_N_ELEMENTS(map); i++) {
		g_autofree gchar *ver = fu_fresco_pd_version_from_buf(map[i].buf);
		g_assert_cmpstr(ver, ==, map[i].ver);
	}
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fresco-pd/common{version}", fu_fresco_pd_common_version_func);
	return g_test_run();
}
