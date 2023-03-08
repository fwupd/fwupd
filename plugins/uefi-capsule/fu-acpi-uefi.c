/*
 * Copyright (C) 2023 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-acpi-uefi.h"

#define FU_EFI_INSYDE_GUID	 "9d4bf935-a674-4710-ba02-bf0aa1758c7b"
#define INSYDE_QUIRK_COD_WORKING 0x1

struct _FuAcpiUefi {
	FuAcpiTable parent_instance;
	guint32 insyde_cod_status;
	gboolean is_insyde;
	gchar *guid;
};

typedef struct __attribute__((packed)) {
	gchar signature[6];
	guint32 size;  /* le */
	guint32 flags; /* le */
} FuAcpiInsydeQuirkSection;

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
fu_acpi_uefi_parse_insyde(FuAcpiUefi *self,
			  const guint8 *buf,
			  gsize bufsz,
			  gsize offset,
			  GError **error)
{
	const gchar *needle = "$QUIRK";
	gsize data_offset = 0;
	guint32 flags = 0;
	guint32 size = 0;

	if (!fu_memmem_safe(buf,
			    bufsz,
			    (const guint8 *)needle,
			    strlen(needle),
			    &data_offset,
			    error)) {
		g_prefix_error(error, "$QUIRK not found");
		return FALSE;
	}
	offset += data_offset;
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    offset + G_STRUCT_OFFSET(FuAcpiInsydeQuirkSection, size),
				    &size,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (size < sizeof(FuAcpiInsydeQuirkSection)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "$QUIRK structure is too small");
		return FALSE;
	}
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    offset + G_STRUCT_OFFSET(FuAcpiInsydeQuirkSection, flags),
				    &flags,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	self->insyde_cod_status = flags & INSYDE_QUIRK_COD_WORKING;
	return TRUE;
}

static gboolean
fu_acpi_uefi_parse(FuFirmware *firmware,
		   GBytes *fw,
		   gsize offset,
		   FwupdInstallFlags flags,
		   GError **error)
{
	FuAcpiUefi *self = FU_ACPI_UEFI(firmware);
	fwupd_guid_t guid = {0x0};
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	/* FuAcpiTable->parse */
	if (!FU_FIRMWARE_CLASS(fu_acpi_uefi_parent_class)
		 ->parse(FU_FIRMWARE(self), fw, offset, FWUPD_INSTALL_FLAG_NONE, error))
		return FALSE;

	/* check signature and read flags */
	if (g_strcmp0(fu_firmware_get_id(FU_FIRMWARE(self)), "UEFI") != 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "Not a UEFI table, got %s",
			    fu_firmware_get_id(FU_FIRMWARE(self)));
		return FALSE;
	}

	/* GUID for the table */
	if (!fu_memcpy_safe((guint8 *)guid,
			    sizeof(guid),
			    0x0, /* dst */
			    buf,
			    bufsz,
			    offset + 0x24, /* src */
			    sizeof(guid),
			    error))
		return FALSE;
	self->guid = fwupd_guid_to_string(&guid, FWUPD_GUID_FLAG_MIXED_ENDIAN);

	/* parse Insyde-specific data */
	self->is_insyde = (g_strcmp0(self->guid, FU_EFI_INSYDE_GUID) == 0);
	if (self->is_insyde) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_acpi_uefi_parse_insyde(self, buf, bufsz, offset, &error_local))
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
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_acpi_uefi_finalize;
	klass_firmware->parse = fu_acpi_uefi_parse;
	klass_firmware->export = fu_acpi_uefi_export;
}

FuFirmware *
fu_acpi_uefi_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_ACPI_UEFI, NULL));
}

gboolean
fu_acpi_uefi_cod_functional(FuAcpiUefi *self, GError **error)
{
	if (!self->is_insyde || self->insyde_cod_status > 0)
		return TRUE;
	g_set_error_literal(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "Capsule-on-Disk may have a firmware bug");
	return FALSE;
}
