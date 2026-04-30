/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-acpi-sbom-entry.h"
#include "fu-acpi-sbom-struct.h"
#include "fu-acpi-sbom-table.h"
#include "fu-acpi-table-struct.h"

struct _FuAcpiSbomTable {
	FuAcpiTable parent_instance;
};

G_DEFINE_TYPE(FuAcpiSbomTable, fu_acpi_sbom_table, FU_TYPE_ACPI_TABLE)

static gboolean
fu_acpi_sbom_table_parse(FuFirmware *firmware,
			 GInputStream *stream,
			 FuFirmwareParseFlags flags,
			 GError **error)
{
	gsize offset = 0;
	gsize streamsz = 0;
	g_autoptr(GInputStream) stream_payload = NULL;

	/* FuAcpiTable->parse */
	if (!FU_FIRMWARE_CLASS(fu_acpi_sbom_table_parent_class)
		 ->parse(firmware, stream, flags | FU_FIRMWARE_PARSE_FLAG_CACHE_STREAM, error))
		return FALSE;

	/* check signature and read flags */
	if (g_strcmp0(fu_firmware_get_id(firmware), "SBOM") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Not a SBOM table, got %s",
			    fu_firmware_get_id(firmware));
		return FALSE;
	}

	/* read each entry */
	stream_payload = fu_acpi_table_get_payload(FU_ACPI_TABLE(firmware), error);
	if (stream_payload == NULL)
		return FALSE;
	if (!fu_input_stream_size(stream_payload, &streamsz, error))
		return FALSE;
	while (offset < streamsz) {
		g_autoptr(FuFirmware) entry = fu_acpi_sbom_entry_new();
		if (!fu_firmware_parse_stream(entry, stream_payload, offset, flags, error))
			return FALSE;
		if (fu_firmware_get_size(entry) == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "SBOM entry had zero size @0%x",
				    (guint)offset);
			return FALSE;
		}
		if (!fu_size_checked_inc(&offset, fu_firmware_get_size(entry), error))
			return FALSE;
		if (!fu_firmware_add_image(firmware, entry, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_acpi_sbom_table_write(FuFirmware *firmware, GError **error)
{
	fu_firmware_set_id(firmware, "SBOM");
	return FU_FIRMWARE_CLASS(fu_acpi_sbom_table_parent_class)->write(firmware, error);
}

static void
fu_acpi_sbom_table_init(FuAcpiSbomTable *self)
{
	fu_firmware_add_image_gtype(FU_FIRMWARE(self), FU_TYPE_ACPI_SBOM_ENTRY);
	fu_firmware_set_images_max(FU_FIRMWARE(self), 5000);
	fu_firmware_set_size_max(FU_FIRMWARE(self), 5 * FU_MB);
}

static void
fu_acpi_sbom_table_class_init(FuAcpiSbomTableClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_acpi_sbom_table_parse;
	firmware_class->write = fu_acpi_sbom_table_write;
}

FuFirmware *
fu_acpi_sbom_table_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_ACPI_SBOM_TABLE, NULL));
}
