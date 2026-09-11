/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-amd-afc-acpi-table.h"

struct _FuAmdAfcAcpiTable {
	FuAcpiTable parent_instance;
	GBytes *payload;
};

G_DEFINE_TYPE(FuAmdAfcAcpiTable, fu_amd_afc_acpi_table, FU_TYPE_ACPI_TABLE)

static gboolean
fu_amd_afc_acpi_table_parse(FuFirmware *firmware,
			    FuInputStream *stream,
			    FuFirmwareParseFlags flags,
			    GError **error)
{
	FuAmdAfcAcpiTable *self = FU_AMD_AFC_ACPI_TABLE(firmware);
	g_autoptr(FuInputStream) payload_stream = NULL;

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
	payload_stream = fu_acpi_table_get_payload(FU_ACPI_TABLE(self), error);
	if (payload_stream == NULL)
		return FALSE;
	g_clear_pointer(&self->payload, g_bytes_unref);
	self->payload = fu_input_stream_read_bytes(payload_stream, 0, G_MAXSIZE, NULL, error);
	return self->payload != NULL;
}

FuAmdAfcAcpiTable *
fu_amd_afc_acpi_table_new(void)
{
	return g_object_new(FU_TYPE_AMD_AFC_ACPI_TABLE, NULL);
}

GBytes *
fu_amd_afc_acpi_table_get_payload(FuAmdAfcAcpiTable *self)
{
	g_return_val_if_fail(FU_IS_AMD_AFC_ACPI_TABLE(self), NULL);
	return self->payload;
}

static void
fu_amd_afc_acpi_table_finalize(GObject *object)
{
	FuAmdAfcAcpiTable *self = FU_AMD_AFC_ACPI_TABLE(object);
	g_clear_pointer(&self->payload, g_bytes_unref);
	G_OBJECT_CLASS(fu_amd_afc_acpi_table_parent_class)->finalize(object);
}

static void
fu_amd_afc_acpi_table_class_init(FuAmdAfcAcpiTableClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	firmware_class->parse = fu_amd_afc_acpi_table_parse;
	object_class->finalize = fu_amd_afc_acpi_table_finalize;
}

static void
fu_amd_afc_acpi_table_init(FuAmdAfcAcpiTable *self)
{
}
