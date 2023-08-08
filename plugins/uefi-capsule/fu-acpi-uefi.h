/*
 * Copyright (C) 2023 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ACPI_UEFI (fu_acpi_uefi_get_type())
G_DECLARE_FINAL_TYPE(FuAcpiUefi, fu_acpi_uefi, FU, ACPI_UEFI, FuAcpiTable)

#define FU_EFI_INSYDE_GUID "9d4bf935-a674-4710-ba02-bf0aa1758c7b"

FuFirmware *
fu_acpi_uefi_new(void);
gboolean
fu_acpi_uefi_cod_functional(FuAcpiUefi *self, GError **error);
gboolean
fu_acpi_uefi_cod_indexed_filename(FuAcpiUefi *self);
