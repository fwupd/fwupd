/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-amd-afc-acpi-table.h"

struct _FuAmdAfcAcpiTable {
	FuAcpiTable parent_instance;
};

G_DEFINE_TYPE(FuAmdAfcAcpiTable, fu_amd_afc_acpi_table, FU_TYPE_ACPI_TABLE)

static gboolean
fu_amd_afc_acpi_table_parse(FuFirmware *firmware,
			    FuInputStream *stream,
			    FuFirmwareParseFlags flags,
			    GError **error)
{
	FuAmdAfcAcpiTable *self = FU_AMD_AFC_ACPI_TABLE(firmware);

	if (!FU_FIRMWARE_CLASS(fu_amd_afc_acpi_table_parent_class)
		 ->parse(firmware, stream, flags | FU_FIRMWARE_PARSE_FLAG_CACHE_STREAM, error))
		return FALSE;
	if (g_strcmp0(fu_firmware_get_id(firmware), "SSDT") != 0 ||
	    g_strcmp0(fu_acpi_table_get_oem_table_id(FU_ACPI_TABLE(self)), "AmdFwCfg") != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "ACPI table does not contain AFC data");
		return FALSE;
	}
	return TRUE;
}

static GByteArray *
fu_amd_afc_acpi_table_write(FuFirmware *firmware, GError **error)
{
	fu_firmware_set_id(firmware, "SSDT");
	fu_acpi_table_set_oem_table_id(FU_ACPI_TABLE(firmware), "AmdFwCfg");
	return FU_FIRMWARE_CLASS(fu_amd_afc_acpi_table_parent_class)->write(firmware, error);
}

FuAmdAfcAcpiTable *
fu_amd_afc_acpi_table_new(void)
{
	return g_object_new(FU_TYPE_AMD_AFC_ACPI_TABLE, NULL);
}

static void
fu_amd_afc_acpi_table_class_init(FuAmdAfcAcpiTableClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_amd_afc_acpi_table_parse;
	firmware_class->write = fu_amd_afc_acpi_table_write;
}

static void
fu_amd_afc_acpi_table_init(FuAmdAfcAcpiTable *self)
{
}
