/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ACPI_PHAT_VERSION_ELEMENT (fu_acpi_phat_version_element_get_type ())
G_DECLARE_FINAL_TYPE (FuAcpiPhatVersionElement, fu_acpi_phat_version_element, FU, ACPI_PHAT_VERSION_ELEMENT, FuFirmware)

FuFirmware	*fu_acpi_phat_version_element_new	(void);
