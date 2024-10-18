/*
 * Copyright 2023 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-acpi-uefi.h"
#include "fu-uefi-struct.h"

#define INSYDE_QUIRK_COD_WORKING 0x1

struct _FuAcpiUefi {
	FuAcpiTable parent_instance;
	guint32 insyde_cod_status;
	gboolean is_insyde;
	gchar *guid;
};

G_DEFINE_TYPE(FuAcpiUefi, fu_acpi_uefi, FU_TYPE_ACPI_TABLE)

static void
fu_acpi_uefi_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuAcpiUefi *self = FU_ACPI_UEFI(firmware);

	/* FuAcpiTable->export */
	FU_FIRMWARE_CLASS(fu_acpi_uefi_parent_class)->export(firmware, flags, bn);

	fu_xmlb_builder_insert_kb(bn, "is_insyde", self->is_insyde);
	fu_xmlb_builder_insert_kx(bn, "insyde_cod_status", self->insyde_cod_status);
	fu_xmlb_builder_insert_kv(bn, "guid", self->guid);
}

static gboolean
fu_acpi_uefi_parse_insyde(FuAcpiUefi *self, GInputStream *stream, GError **error)
{
	const gchar *needle = "$QUIRK";
	gsize data_offset = 0;
	g_autoptr(GByteArray) st_qrk = NULL;
	g_autoptr(GBytes) fw = NULL;

	fw = fu_input_stream_read_bytes(stream, 0x0, G_MAXSIZE, NULL, error);
	if (fw == NULL)
		return FALSE;
	if (!fu_memmem_safe(g_bytes_get_data(fw, NULL),
			    g_bytes_get_size(fw),
			    (const guint8 *)needle,
			    strlen(needle),
			    &data_offset,
			    error)) {
		g_prefix_error(error, "$QUIRK not found");
		return FALSE;
	}

	/* parse */
	st_qrk = fu_struct_acpi_insyde_quirk_parse_stream(stream, data_offset, error);
	if (st_qrk == NULL)
		return FALSE;
	if (fu_struct_acpi_insyde_quirk_get_size(st_qrk) < st_qrk->len) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "$QUIRK structure is too small");
		return FALSE;
	}
	self->insyde_cod_status =
	    fu_struct_acpi_insyde_quirk_get_flags(st_qrk) & INSYDE_QUIRK_COD_WORKING;
	return TRUE;
}

static gboolean
fu_acpi_uefi_parse(FuFirmware *firmware,
		   GInputStream *stream,
		   FwupdInstallFlags flags,
		   GError **error)
{
	FuAcpiUefi *self = FU_ACPI_UEFI(firmware);
	fwupd_guid_t guid = {0x0};

	/* FuAcpiTable->parse */
	if (!FU_FIRMWARE_CLASS(fu_acpi_uefi_parent_class)
		 ->parse(FU_FIRMWARE(self), stream, FWUPD_INSTALL_FLAG_NONE, error))
		return FALSE;

	/* check signature and read flags */
	if (g_strcmp0(fu_firmware_get_id(FU_FIRMWARE(self)), "UEFI") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Not a UEFI table, got %s",
			    fu_firmware_get_id(FU_FIRMWARE(self)));
		return FALSE;
	}

	/* GUID for the table */
	if (!fu_input_stream_read_safe(stream,
				       (guint8 *)guid,
				       sizeof(guid),
				       0x0,  /* dst */
				       0x24, /* src */
				       sizeof(guid),
				       error))
		return FALSE;
	self->guid = fwupd_guid_to_string(&guid, FWUPD_GUID_FLAG_MIXED_ENDIAN);

	/* parse Insyde-specific data */
	self->is_insyde = (g_strcmp0(self->guid, FU_EFI_INSYDE_GUID) == 0);
	if (self->is_insyde) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_acpi_uefi_parse_insyde(self, stream, &error_local))
			g_debug("%s", error_local->message);
	}

	/* success */
	return TRUE;
}

static void
fu_acpi_uefi_init(FuAcpiUefi *self)
{
}

static void
fu_acpi_uefi_finalize(GObject *object)
{
	FuAcpiUefi *self = FU_ACPI_UEFI(object);
	g_free(self->guid);
	G_OBJECT_CLASS(fu_acpi_uefi_parent_class)->finalize(object);
}

static void
fu_acpi_uefi_class_init(FuAcpiUefiClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_acpi_uefi_finalize;
	firmware_class->parse = fu_acpi_uefi_parse;
	firmware_class->export = fu_acpi_uefi_export;
}

FuFirmware *
fu_acpi_uefi_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_ACPI_UEFI, NULL));
}

gboolean
fu_acpi_uefi_cod_functional(FuAcpiUefi *self, GError **error)
{
	g_return_val_if_fail(FU_IS_ACPI_UEFI(self), FALSE);
	if (!self->is_insyde || self->insyde_cod_status > 0)
		return TRUE;
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Capsule-on-Disk may have a firmware bug");
	return FALSE;
}

gboolean
fu_acpi_uefi_cod_indexed_filename(FuAcpiUefi *self)
{
	g_return_val_if_fail(FU_IS_ACPI_UEFI(self), FALSE);
	if (self->is_insyde)
		return TRUE;
	return FALSE;
}
