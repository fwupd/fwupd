/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-telink-dfu-common.h"

static void
fu_telink_dfu_common_version_func(void)
{
	/* forced update */
	g_assert_cmpint(fu_telink_dfu_parse_image_version(NULL, FWUPD_VERSION_FORMAT_TRIPLET),
			==,
			0);

	/* triplet */
	g_assert_cmpint(fu_telink_dfu_parse_image_version("1.2.3", FWUPD_VERSION_FORMAT_TRIPLET),
			==,
			(1u << 24) | (2u << 16) | 3u);

	/* pair */
	g_assert_cmpint(fu_telink_dfu_parse_image_version("4.5", FWUPD_VERSION_FORMAT_PAIR),
			==,
			(4u << 16) | 5u);

	/* invalid triplet */
	g_test_expect_message(G_LOG_DOMAIN,
			      G_LOG_LEVEL_WARNING,
			      "*invalid version string(FORMAT_TRIPLET)*");
	g_assert_cmpint(fu_telink_dfu_parse_image_version("1000.0.0", FWUPD_VERSION_FORMAT_TRIPLET),
			==,
			0);
	g_test_assert_expected_messages();

	/* invalid pair */
	g_test_expect_message(G_LOG_DOMAIN,
			      G_LOG_LEVEL_WARNING,
			      "*invalid version string(FORMAT_PAIR)*");
	g_assert_cmpint(fu_telink_dfu_parse_image_version("100.0", FWUPD_VERSION_FORMAT_PAIR),
			==,
			0);
	g_test_assert_expected_messages();

	/* unsupported format */
	g_test_expect_message(G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, "*unsupported version format*");
	g_assert_cmpint(fu_telink_dfu_parse_image_version("1.2.3", FWUPD_VERSION_FORMAT_NUMBER),
			==,
			0);
	g_test_assert_expected_messages();
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/telink-dfu/common{version}", fu_telink_dfu_common_version_func);
	return g_test_run();
}
