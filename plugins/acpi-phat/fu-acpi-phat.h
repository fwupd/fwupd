/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ACPI_PHAT (fu_acpi_phat_get_type ())
G_DECLARE_FINAL_TYPE (FuAcpiPhat, fu_acpi_phat, FU, ACPI_PHAT, FuFirmware)

#define FU_ACPI_PHAT_RECORD_TYPE_VERSION	0x0000
#define FU_ACPI_PHAT_RECORD_TYPE_HEALTH		0x0001
#define FU_ACPI_PHAT_REVISION			0x01

FuFirmware		*fu_acpi_phat_new		(void);
gchar			*fu_acpi_phat_to_report_string	(FuAcpiPhat	*self);
