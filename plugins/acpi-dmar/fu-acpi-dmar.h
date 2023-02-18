/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ACPI_DMAR (fu_acpi_dmar_get_type())
G_DECLARE_FINAL_TYPE(FuAcpiDmar, fu_acpi_dmar, FU, ACPI_DMAR, FuAcpiTable)

FuAcpiDmar *
fu_acpi_dmar_new(GBytes *blob, GError **error);
gboolean
fu_acpi_dmar_get_opt_in(FuAcpiDmar *self);
