/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-daemon.h"
#include "fu-test.h"

static void
fu_daemon_codec_func(void)
{
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_autoptr(FuDaemon) daemon = g_object_new(FU_TYPE_DAEMON, NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	FuEngine *engine = fu_daemon_get_engine(daemon);
	FuContext *ctx = fu_engine_get_context(engine);

	/* load dummy hwids */
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_HWID_CONFIG, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* dump */
	str = fwupd_codec_to_string(FWUPD_CODEC(daemon));
	g_debug("%s", str);
	g_assert_nonnull(str);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	(void)g_setenv("FWUPD_SELF_TEST", "1", TRUE);
	g_test_add_func("/fwupd/daemon/codec", fu_daemon_codec_func);
	return g_test_run();
}
