/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2022 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

#define FU_TYPE_ACPI_IVRS (fu_acpi_ivrs_get_type())
G_DECLARE_FINAL_TYPE(FuAcpiIvrs, fu_acpi_ivrs, FU, ACPI_IVRS, GObject)

FuAcpiIvrs *
fu_acpi_ivrs_new(GBytes *blob, GError **error);
gboolean
fu_acpi_ivrs_get_dma_remap(FuAcpiIvrs *self);
