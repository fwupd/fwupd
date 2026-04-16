/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-linux-sleep-plugin.h"

struct _FuLinuxSleepPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuLinuxSleepPlugin, fu_linux_sleep_plugin, FU_TYPE_PLUGIN)

/* ACPI FADT Preferred_PM_Profile values (offset 0x2D) */
#define FU_LINUX_SLEEP_PM_PROFILE_ENTERPRISE_SERVER  4
#define FU_LINUX_SLEEP_PM_PROFILE_SOHO_SERVER	     5
#define FU_LINUX_SLEEP_PM_PROFILE_PERFORMANCE_SERVER 7

static gboolean
fu_linux_sleep_plugin_is_server(FuPlugin *plugin)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	guint8 pm_profile = 0;
	gsize bufsz = 0;
	const guint8 *buf;
	g_autofree gchar *fn = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error_local = NULL;

	fn = fu_context_build_filename(ctx, &error_local, FU_PATH_KIND_ACPI_TABLES, "FACP", NULL);
	if (fn == NULL)
		return FALSE;
	blob = fu_bytes_get_contents(fn, &error_local);
	if (blob == NULL)
		return FALSE;
	buf = g_bytes_get_data(blob, &bufsz);
	if (!fu_memread_uint8_safe(buf, bufsz, 0x2D, &pm_profile, NULL))
		return FALSE;
	return pm_profile == FU_LINUX_SLEEP_PM_PROFILE_ENTERPRISE_SERVER ||
	       pm_profile == FU_LINUX_SLEEP_PM_PROFILE_SOHO_SERVER ||
	       pm_profile == FU_LINUX_SLEEP_PM_PROFILE_PERFORMANCE_SERVER;
}

static void
fu_linux_sleep_plugin_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	gsize bufsz = 0;
	g_autofree gchar *buf = NULL;
	g_autofree gchar *fn = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GFile) file = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_RAM);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
	fu_security_attrs_append(attrs, attr);

	/* load file */
	fn = fu_context_build_filename(ctx,
				       &error_local,
				       FU_PATH_KIND_SYSFSDIR,
				       "power",
				       "mem_sleep",
				       NULL);
	if (fn == NULL) {
		g_debug("failed to build: %s", error_local->message);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}
	file = g_file_new_for_path(fn);
	if (!g_file_load_contents(file, NULL, &buf, &bufsz, NULL, &error_local)) {
		g_warning("could not open %s: %s", fn, error_local->message);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}

	/* on server platforms, S3 (Suspend-to-RAM) is often not supported */
	if (g_strstr_len(buf, bufsz, "deep") == NULL && fu_linux_sleep_plugin_is_server(plugin)) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
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
