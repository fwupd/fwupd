/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

G_BEGIN_DECLS

#define FU_TYPE_AMD_AFC_ACPI_TABLE (fu_amd_afc_acpi_table_get_type())
G_DECLARE_FINAL_TYPE(FuAmdAfcAcpiTable, fu_amd_afc_acpi_table, FU, AMD_AFC_ACPI_TABLE, FuAcpiTable)

FuAmdAfcAcpiTable *
fu_amd_afc_acpi_table_new(void);

G_END_DECLS
