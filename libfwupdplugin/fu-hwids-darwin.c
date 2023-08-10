/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuContext"

#include "config.h"

#include "fu-context-private.h"
#include "fu-hwids-private.h"

gboolean
fu_hwids_darwin_setup(FuContext *ctx, FuHwids *self, GError **error)
{
#ifdef HOST_MACHINE_SYSTEM_DARWIN
	struct {
		const gchar *hwid;
		const gchar *key;
	} map[] = {{FU_HWIDS_KEY_BIOS_VERSION, "System Firmware Version"},
		   {FU_HWIDS_KEY_FAMILY, "Model Name"},
		   {FU_HWIDS_KEY_PRODUCT_NAME, "Model Identifier"},
		   {NULL}};
	const gchar *family = NULL;
	g_autofree gchar *standard_output = NULL;
	g_auto(GStrv) lines = NULL;

	/* parse the profiler output */
	if (!g_spawn_command_line_sync("system_profiler SPHardwareDataType",
				       &standard_output,
				       NULL,
				       NULL,
				       error))
		return FALSE;
	lines = g_strsplit(standard_output, "\n", -1);
	for (guint j = 0; lines[j] != NULL; j++) {
		for (guint i = 0; map[i].key != NULL; i++) {
			g_auto(GStrv) chunks = g_strsplit(lines[j], ":", 2);
			if (g_strv_length(chunks) != 2)
				continue;
			if (g_strstr_len(chunks[0], -1, map[i].key) != NULL)
				fu_hwids_add_value(self, map[i].hwid, g_strstrip(chunks[1]));
		}
	}

	/* this has to be hardcoded */
	fu_hwids_add_value(self, FU_HWIDS_KEY_BASEBOARD_MANUFACTURER, "Apple");
	fu_hwids_add_value(self, FU_HWIDS_KEY_MANUFACTURER, "Apple");
	fu_hwids_add_value(self, FU_HWIDS_KEY_BIOS_VENDOR, "Apple");

	/* set the chassis kind using the family */
	family = fu_hwids_get_value(self, FU_HWIDS_KEY_FAMILY);
	if (g_strcmp0(family, "MacBook Pro") == 0) {
		fu_hwids_add_value(self, FU_HWIDS_KEY_ENCLOSURE_KIND, "a");
		fu_context_set_chassis_kind(ctx, FU_SMBIOS_CHASSIS_KIND_LAPTOP);
	}
#endif

	/* success */
	return TRUE;
}
