/*
 * Copyright (C) 2021 Peter Marheine <pmarheine@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-plugin-vfuncs.h"
#include "fu-realtek-mst-device.h"

#include <libflashrom.h>

#define SELFCHECK_TRUE 1

void
fu_plugin_init (FuPlugin *plugin)
{
	FuContext *ctx = fu_plugin_get_context (plugin);

	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_context_add_quirk_key (ctx, "RealtekMstDpAuxName");

	fu_context_add_udev_subsystem (ctx, "i2c");
	fu_plugin_add_device_gtype (plugin, FU_TYPE_REALTEK_MST_DEVICE);
}

static int
fu_plugin_flashrom_debug_cb (enum flashrom_log_level lvl,
			     const char *fmt,
			     va_list args)
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
	g_autofree gchar *tmp = g_strdup_vprintf (fmt, args);
#pragma clang diagnostic pop
	g_autofree gchar *str = fu_common_strstrip (tmp);
	if (g_strcmp0 (str, "OK.") == 0 || g_strcmp0 (str, ".") == 0)
		return 0;
	switch (lvl) {
		case FLASHROM_MSG_ERROR:
		case FLASHROM_MSG_WARN: g_warning ("%s", str);
			break;
		case FLASHROM_MSG_INFO: g_debug ("%s", str);
			break;
		case FLASHROM_MSG_DEBUG:
		case FLASHROM_MSG_DEBUG2:
			if (g_getenv ("FWUPD_REALTEK_MST_VERBOSE") != NULL)
				g_debug ("%s", str);
			break;
		case FLASHROM_MSG_SPEW: break;
		default: break;
	}
	return 0;
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	if (flashrom_init (SELFCHECK_TRUE)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "flashrom initialization error");
		return FALSE;
	}
	flashrom_set_log_callback (fu_plugin_flashrom_debug_cb);
	return TRUE;
}
