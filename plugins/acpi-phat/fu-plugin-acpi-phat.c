/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-acpi-phat.h"
#include "fu-acpi-phat-health-record.h"
#include "fu-acpi-phat-version-element.h"
#include "fu-acpi-phat-version-record.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_firmware_gtype (plugin, NULL, FU_TYPE_ACPI_PHAT);
	fu_plugin_add_firmware_gtype (plugin, NULL, FU_TYPE_ACPI_PHAT_HEALTH_RECORD);
	fu_plugin_add_firmware_gtype (plugin, NULL, FU_TYPE_ACPI_PHAT_VERSION_ELEMENT);
	fu_plugin_add_firmware_gtype (plugin, NULL, FU_TYPE_ACPI_PHAT_VERSION_RECORD);
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	g_autofree gchar *path = NULL;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FuFirmware) phat = fu_acpi_phat_new ();
	g_autoptr(GBytes) blob = NULL;

	path = fu_common_get_path (FU_PATH_KIND_ACPI_TABLES);
	fn = g_build_filename (path, "PHAT", NULL);
	blob = fu_common_get_contents_bytes (fn, error);
	if (blob == NULL)
		return FALSE;
	if (!fu_firmware_parse (phat, blob, FWUPD_INSTALL_FLAG_NONE, error))
		return FALSE;
	str = fu_acpi_phat_to_report_string (FU_ACPI_PHAT (phat));
	fu_plugin_add_report_metadata (plugin, "PHAT", str);
	return TRUE;
}
