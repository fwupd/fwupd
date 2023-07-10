/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-linux-sleep-plugin.h"

struct _FuLinuxSleepPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuLinuxSleepPlugin, fu_linux_sleep_plugin, FU_TYPE_PLUGIN)

static void
fu_linux_sleep_plugin_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	gsize bufsz = 0;
	g_autofree gchar *buf = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GFile) file = g_file_new_for_path("/sys/power/mem_sleep");

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_RAM);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
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
}

static void
fu_linux_sleep_plugin_init(FuLinuxSleepPlugin *self)
{
}

static void
fu_linux_sleep_plugin_class_init(FuLinuxSleepPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->add_security_attrs = fu_linux_sleep_plugin_add_security_attrs;
}
