/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ACPI_SBOM_PLUGIN (fu_acpi_sbom_plugin_get_type())
G_DECLARE_FINAL_TYPE(FuAcpiSbomPlugin, fu_acpi_sbom_plugin, FU, ACPI_SBOM_PLUGIN, FuPlugin)
