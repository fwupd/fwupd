/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ACPI_PHAT_HEALTH_RECORD (fu_acpi_phat_health_record_get_type ())
G_DECLARE_FINAL_TYPE (FuAcpiPhatHealthRecord, fu_acpi_phat_health_record, FU, ACPI_PHAT_HEALTH_RECORD, FuFirmware)

FuFirmware		*fu_acpi_phat_health_record_new	(void);
