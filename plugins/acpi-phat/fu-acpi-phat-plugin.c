/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-acpi-phat-health-record.h"
#include "fu-acpi-phat-plugin.h"
#include "fu-acpi-phat-version-element.h"
#include "fu-acpi-phat-version-record.h"
#include "fu-acpi-phat.h"

struct _FuAcpiPhatPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuAcpiPhatPlugin, fu_acpi_phat_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_acpi_phat_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	g_autofree gchar *path = NULL;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FuFirmware) phat = fu_acpi_phat_new();
	g_autoptr(GBytes) blob = NULL;

	path = fu_path_from_kind(FU_PATH_KIND_ACPI_TABLES);
	fn = g_build_filename(path, "PHAT", NULL);
	blob = fu_bytes_get_contents(fn, error);
	if (blob == NULL)
		return FALSE;
	if (!fu_firmware_parse(phat, blob, FWUPD_INSTALL_FLAG_NO_SEARCH, error))
		return FALSE;
	str = fu_acpi_phat_to_report_string(FU_ACPI_PHAT(phat));
	fu_plugin_add_report_metadata(plugin, "PHAT", str);
	return TRUE;
}

static void
fu_acpi_phat_plugin_init(FuAcpiPhatPlugin *self)
{
}

static void
fu_acpi_phat_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_ACPI_PHAT);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_ACPI_PHAT_HEALTH_RECORD);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_ACPI_PHAT_VERSION_ELEMENT);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_ACPI_PHAT_VERSION_RECORD);
}

static void
fu_acpi_phat_plugin_class_init(FuAcpiPhatPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->constructed = fu_acpi_phat_plugin_constructed;
	plugin_class->coldplug = fu_acpi_phat_plugin_coldplug;
}
