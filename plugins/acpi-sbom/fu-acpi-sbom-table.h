/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ACPI_SBOM_TABLE (fu_acpi_sbom_table_get_type())
G_DECLARE_FINAL_TYPE(FuAcpiSbomTable, fu_acpi_sbom_table, FU, ACPI_SBOM_TABLE, FuAcpiTable)

FuFirmware *
fu_acpi_sbom_table_new(void);
