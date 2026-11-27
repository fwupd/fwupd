/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-acpi-table-struct.h"

#define FU_TYPE_ACPI_FACP (fu_acpi_facp_get_type())
G_DECLARE_FINAL_TYPE(FuAcpiFacp, fu_acpi_facp, FU, ACPI_FACP, FuAcpiTable)

FuAcpiFacp *
fu_acpi_facp_new(void);
gboolean
fu_acpi_facp_get_s2i(FuAcpiFacp *self);
void
fu_acpi_facp_set_s2i(FuAcpiFacp *self, gboolean s2i);
FuAcpiFadtPmProfile
fu_acpi_facp_get_pm_profile(FuAcpiFacp *self);
void
fu_acpi_facp_set_pm_profile(FuAcpiFacp *self, FuAcpiFadtPmProfile pm_profile);
