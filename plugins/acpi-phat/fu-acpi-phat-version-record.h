/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ACPI_PHAT_VERSION_RECORD (fu_acpi_phat_version_record_get_type())
G_DECLARE_FINAL_TYPE(FuAcpiPhatVersionRecord,
		     fu_acpi_phat_version_record,
		     FU,
		     ACPI_PHAT_VERSION_RECORD,
		     FuFirmware)

FuFirmware *
fu_acpi_phat_version_record_new(void);
