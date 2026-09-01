/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"
#include "fu-input-stream.h"

G_BEGIN_DECLS

#define FU_TYPE_ACPI_TABLE (fu_acpi_table_get_type())
G_DECLARE_DERIVABLE_TYPE(FuAcpiTable, fu_acpi_table, FU, ACPI_TABLE, FuFirmware)

struct _FuAcpiTableClass {
	FuFirmwareClass parent_class;
};

FuFirmware *
fu_acpi_table_new(void);
guint8
fu_acpi_table_get_revision(FuAcpiTable *self) G_GNUC_NON_NULL(1) G_GNUC_PURE;
const gchar *
fu_acpi_table_get_oem_id(FuAcpiTable *self) G_GNUC_NON_NULL(1) G_GNUC_PURE;
const gchar *
fu_acpi_table_get_oem_table_id(FuAcpiTable *self) G_GNUC_NON_NULL(1) G_GNUC_PURE;
guint32
fu_acpi_table_get_oem_revision(FuAcpiTable *self) G_GNUC_NON_NULL(1) G_GNUC_PURE;
FuInputStream *
fu_acpi_table_get_payload(FuAcpiTable *self, GError **error) G_GNUC_NON_NULL(1);

G_END_DECLS
