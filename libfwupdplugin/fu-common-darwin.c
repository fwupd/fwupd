/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuCommon"

#include <config.h>
#include <fnmatch.h>
#include <sys/sysctl.h>

#include "fu-common-private.h"

GPtrArray *
fu_common_get_block_devices(GError **error)
{
	g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "not supported");
	return NULL;
}

gboolean
fu_common_fnmatch_impl(const gchar *pattern, const gchar *str)
{
	return fnmatch(pattern, str, FNM_NOESCAPE) == 0;
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
