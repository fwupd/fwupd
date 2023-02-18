/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_ACPI_TABLE (fu_acpi_table_get_type())
G_DECLARE_DERIVABLE_TYPE(FuAcpiTable, fu_acpi_table, FU, ACPI_TABLE, FuFirmware)

struct _FuAcpiTableClass {
	FuFirmwareClass parent_class;
};

FuFirmware *
fu_acpi_table_new(void);
guint8
fu_acpi_table_get_revision(FuAcpiTable *self);
const gchar *
fu_acpi_table_get_oem_id(FuAcpiTable *self);
const gchar *
fu_acpi_table_get_oem_table_id(FuAcpiTable *self);
guint32
fu_acpi_table_get_oem_revision(FuAcpiTable *self);
