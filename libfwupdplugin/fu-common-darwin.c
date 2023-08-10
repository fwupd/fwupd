/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuCommon"

#include "config.h"

#include <sys/sysctl.h>

#include "fu-common-private.h"

GPtrArray *
fu_common_get_block_devices(GError **error)
{
	g_set_error(error,
		    G_IO_ERROR,
		    G_IO_ERROR_NOT_SUPPORTED,
		    "getting block devices is not supported on Darwin");
	return NULL;
}

guint64
fu_common_get_memory_size_impl(void)
{
	gint mib[] = {CTL_HW, HW_MEMSIZE};
	gint64 physical_memory = 0;
	gsize length = sizeof(physical_memory);
	sysctl(mib, 2, &physical_memory, &length, NULL, 0);
	return (guint64)physical_memory;
}

gchar *
fu_common_get_kernel_cmdline_impl(GError **error)
{
	gchar cmdline[1024] = {0};
	gsize cmdlinesz = sizeof(cmdline);
	sysctlbyname("kern.bootargs", cmdline, &cmdlinesz, NULL, 0);
	return g_strndup(cmdline, sizeof(cmdline));
}
