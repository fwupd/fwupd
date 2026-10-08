/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

static void
fu_dump_func(void)
{
	guint8 buf_small[] = {0x00, 0x41, 0x7f, 0x80};
	g_autofree guint8 *buf_big = g_malloc0(128);
	g_autoptr(GBytes) blob = g_bytes_new_static(buf_small, sizeof(buf_small));

	/* exercise the various code paths; these only log, so there is nothing
	 * to assert other than that they do not crash */
	fu_dump_full(G_LOG_DOMAIN, "none", buf_small, sizeof(buf_small), 32, FU_DUMP_FLAG_NONE);
	fu_dump_full(G_LOG_DOMAIN, NULL, buf_small, sizeof(buf_small), 32, FU_DUMP_FLAG_SHOW_ASCII);
	fu_dump_full(G_LOG_DOMAIN,
		     "addresses+ascii",
		     buf_big,
		     128,
		     16,
		     FU_DUMP_FLAG_SHOW_ADDRESSES | FU_DUMP_FLAG_SHOW_ASCII);

	/* fu_dump_raw() switches to addresses once over 64 bytes */
	fu_dump_raw(G_LOG_DOMAIN, "small", buf_small, sizeof(buf_small));
	fu_dump_raw(G_LOG_DOMAIN, "big", buf_big, 128);

	/* the GBytes wrapper */
	fu_dump_bytes(G_LOG_DOMAIN, "blob", blob);
}

int
main(int argc, char **argv)
{
	/* make sure the debug pre-filter in fu_dump_full() does not short-circuit */
	g_log_set_debug_enabled(TRUE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/dump", fu_dump_func);
	return g_test_run();
}
