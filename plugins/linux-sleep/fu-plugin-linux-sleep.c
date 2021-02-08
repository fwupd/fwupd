/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
}

void
fu_plugin_add_security_attrs (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	gsize bufsz = 0;
	g_autofree gchar *buf = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GFile) file = g_file_new_for_path ("/sys/power/mem_sleep");

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_RAM);
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_THEORETICAL);
	fu_security_attrs_append (attrs, attr);

	/* load file */
	if (!g_file_load_contents (file, NULL, &buf, &bufsz, NULL, &error_local)) {
		g_autofree gchar *fn = g_file_get_path (file);
		g_warning ("could not open %s: %s", fn, error_local->message);
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}
	if (g_strstr_len (buf, bufsz, "[deep]") != NULL) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
}
