/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

static void
fu_plugin_linux_sleep_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	gsize bufsz = 0;
	g_autofree gchar *buf = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GFile) file = g_file_new_for_path("/sys/power/mem_sleep");

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_RAM);
	fu_security_attrs_append(attrs, attr);

	/* load file */
	if (!g_file_load_contents(file, NULL, &buf, &bufsz, NULL, &error_local)) {
		g_autofree gchar *fn = g_file_get_path(file);
		g_warning("could not open %s: %s", fn, error_local->message);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}
	if (g_strstr_len(buf, bufsz, "[deep]") != NULL) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_OS);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->add_security_attrs = fu_plugin_linux_sleep_add_security_attrs;
}
