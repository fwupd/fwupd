/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-test.h"

static void
fu_volume_gpt_type_func(void)
{
	g_assert_cmpstr(fu_volume_kind_convert_to_gpt("0xef"),
			==,
			"c12a7328-f81f-11d2-ba4b-00a0c93ec93b");
	g_assert_cmpstr(fu_volume_kind_convert_to_gpt("0x0b"),
			==,
			"ebd0a0a2-b9e5-4433-87c0-68b6b72699c7");
	g_assert_cmpstr(fu_volume_kind_convert_to_gpt("fat32lba"),
			==,
			"ebd0a0a2-b9e5-4433-87c0-68b6b72699c7");
	g_assert_cmpstr(fu_volume_kind_convert_to_gpt("0x00"), ==, "0x00");
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/volume/gpt-type", fu_volume_gpt_type_func);
	return g_test_run();
}
