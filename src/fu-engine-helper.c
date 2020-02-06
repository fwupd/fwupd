/*
 * Copyright (C) 2020 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuEngine"

#include "config.h"

#include <glib/gi18n.h>

#include "fu-engine.h"
#include "fu-engine-helper.h"

gboolean
fu_engine_update_motd (FuEngine *self, GError **error)
{
	guint upgrade_count = 0;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GString) str = NULL;
	g_autofree gchar *target = NULL;

	/* get devices from daemon, we even want to know if it's nothing */
	devices = fu_engine_get_devices (self, NULL);
	if (devices != NULL) {
		for (guint i = 0; i < devices->len; i++) {
			FwupdDevice *dev = g_ptr_array_index (devices, i);
			g_autoptr(GPtrArray) rels = NULL;

			/* get the releases for this device */
			rels = fu_engine_get_upgrades (self,
							fwupd_device_get_id (dev),
							NULL);
			if (rels == NULL)
				continue;
			upgrade_count++;
		}
	}

	/* If running under systemd unit, use the directory as a base */
	if (g_getenv ("RUNTIME_DIRECTORY") != NULL) {
		target = g_build_filename (g_getenv ("RUNTIME_DIRECTORY"),
					   PACKAGE_NAME,
					   MOTD_FILE,
					   NULL);
	/* otherwise use the cache directory */
	} else {
		g_autofree gchar *directory = fu_common_get_path (FU_PATH_KIND_CACHEDIR_PKG);
		target = g_build_filename (directory, MOTD_DIR, MOTD_FILE, NULL);
	}

	/* create the directory and file, even if zero devices; we want an empty file then */
	if (!fu_common_mkdir_parent (target, error))
		return FALSE;

	if (upgrade_count == 0) {
		g_autoptr(GFile) file = g_file_new_for_path (target);
		g_autoptr(GFileOutputStream) output = NULL;
		output = g_file_replace (file, NULL, FALSE,
					 G_FILE_CREATE_NONE,
					 NULL, error);
		return output != NULL;
	}

	str = g_string_new ("\n");
	g_string_append_printf (str, ngettext ("%u device has a firmware upgrade available.",
					       "%u devices have a firmware upgrade available.",
					       upgrade_count),
					       upgrade_count);
	g_string_append_printf (str,
				"\n%s\n\n",
				_("Run `fwupdmgr get-upgrades` for more information."));
	return g_file_set_contents (target, str->str, str->len, error);
}

