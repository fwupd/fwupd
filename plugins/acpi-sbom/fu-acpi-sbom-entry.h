/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-acpi-sbom-struct.h"

#define FU_TYPE_ACPI_SBOM_ENTRY (fu_acpi_sbom_entry_get_type())
G_DECLARE_FINAL_TYPE(FuAcpiSbomEntry, fu_acpi_sbom_entry, FU, ACPI_SBOM_ENTRY, FuFirmware)

FuFirmware *
fu_acpi_sbom_entry_new(void);
FuUswidPayloadFormat
fu_acpi_sbom_entry_get_format(FuAcpiSbomEntry *self);
