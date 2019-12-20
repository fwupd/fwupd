/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>

#include "fu-vli-common.h"

static void
fu_test_common_device_kind_func (void)
{
	for (guint i = 0; i < 0xffff; i++) {
		const gchar *tmp = fu_vli_common_device_kind_to_string (i);
		if (tmp == NULL)
			continue;
		g_assert_cmpint (fu_vli_common_device_kind_from_string (tmp), ==, i);
	}
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	g_test_add_func ("/vli/common{device-kind}", fu_test_common_device_kind_func);
	return g_test_run ();
}
