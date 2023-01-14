/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuContext"

#include "config.h"

#include "fu-context-hwid.h"
#include "fu-context-private.h"
#include "fu-path.h"

gboolean
fu_context_hwid_config_setup(FuContext *self, GError **error)
{
	g_autofree gchar *localstatedir = fu_path_from_kind(FU_PATH_KIND_LOCALSTATEDIR_PKG);
	g_autofree gchar *sysconfigdir = fu_path_from_kind(FU_PATH_KIND_SYSCONFDIR_PKG);
	g_autoptr(GKeyFile) kf = g_key_file_new();
	g_autoptr(GPtrArray) fns = g_ptr_array_new_with_free_func(g_free);
	g_autoptr(GPtrArray) keys = fu_context_get_hwid_keys(self);

	/* per-system configuration and optional overrides */
	g_ptr_array_add(fns, g_build_filename(sysconfigdir, "daemon.conf", NULL));
	g_ptr_array_add(fns, g_build_filename(localstatedir, "daemon.conf", NULL));
	for (guint i = 0; i < fns->len; i++) {
		const gchar *fn = g_ptr_array_index(fns, i);
		if (g_file_test(fn, G_FILE_TEST_EXISTS)) {
			g_debug("loading HwId overrides from %s", fn);
			if (!g_key_file_load_from_file(kf, fn, G_KEY_FILE_NONE, error))
				return FALSE;
		} else {
			g_debug("not loading HwId overrides from %s", fn);
		}
	}

	/* all keys are optional */
	for (guint i = 0; i < keys->len; i++) {
		const gchar *key = g_ptr_array_index(keys, i);
		g_autofree gchar *value = g_key_file_get_string(kf, "fwupd", key, NULL);
		if (value != NULL)
			fu_context_add_hwid_value(self, key, value);
	}

	/* success */
	return TRUE;
}
