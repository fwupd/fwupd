/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-egis-moc-common.h"

static void
fu_egis_moc_checksum_func(void)
{
	guint32 temp_chksum;
	const guint8 buf[] = {0x40, 0xC3, 0xE6, 0xC8, 0xDF, 0x5B};

	/* even */
	temp_chksum = fu_egis_moc_checksum_add(0, buf, sizeof(buf));
	g_assert_cmpint(temp_chksum, ==, 124933);
	temp_chksum = fu_egis_moc_checksum_finish(temp_chksum);
	g_assert_cmpint(temp_chksum, ==, 6137);

	/* odd */
	temp_chksum = fu_egis_moc_checksum_add(0, buf, sizeof(buf) - 1);
	g_assert_cmpint(temp_chksum, ==, 101637);
	temp_chksum = fu_egis_moc_checksum_finish(temp_chksum);
	g_assert_cmpint(temp_chksum, ==, 29433);

	/* chained */
	temp_chksum = fu_egis_moc_checksum_add(0, buf, sizeof(buf));
	g_assert_cmpint(temp_chksum, ==, 124933);
	temp_chksum = fu_egis_moc_checksum_add(temp_chksum, buf, sizeof(buf) - 1);
	g_assert_cmpint(temp_chksum, ==, 226570);
	temp_chksum = fu_egis_moc_checksum_finish(temp_chksum);
	g_assert_cmpint(temp_chksum, ==, 35570);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);

	/* tests go here */
	g_test_add_func("/egic-moc/checksum", fu_egis_moc_checksum_func);
	return g_test_run();
}
