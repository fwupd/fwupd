/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-acpi-dmar-struct.h"
#include "fu-acpi-dmar.h"

struct _FuAcpiDmar {
	FuAcpiTable parent_instance;
	gboolean opt_in;
};

G_DEFINE_TYPE(FuAcpiDmar, fu_acpi_dmar, FU_TYPE_ACPI_TABLE)

static gboolean
fu_acpi_dmar_parse(FuFirmware *firmware,
		   FuInputStream *stream,
		   FuFirmwareParseFlags flags,
		   GError **error)
{
	FuAcpiDmar *self = FU_ACPI_DMAR(firmware);
	g_autoptr(FuInputStream) stream_payload = NULL;
	g_autoptr(FuStructAcpiDmar) st = NULL;

	/* FuAcpiTable->parse */
	if (!FU_FIRMWARE_CLASS(fu_acpi_dmar_parent_class)
		 ->parse(FU_FIRMWARE(self),
			 stream,
			 flags | FU_FIRMWARE_PARSE_FLAG_CACHE_STREAM,
			 error))
		return FALSE;

	/* check signature and read flags */
	if (g_strcmp0(fu_firmware_get_id(FU_FIRMWARE(self)), "DMAR") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Not a DMAR table, got %s",
			    fu_firmware_get_id(FU_FIRMWARE(self)));
		return FALSE;
	}

	stream_payload = fu_acpi_table_get_payload(FU_ACPI_TABLE(self), error);
	if (stream_payload == NULL)
		return FALSE;
	st = fu_struct_acpi_dmar_parse_stream(stream_payload, 0x0, error);
	if (st == NULL)
		return FALSE;
	self->opt_in =
	    (fu_struct_acpi_dmar_get_flags(st) & FU_DMAR_FLAG_DMA_CTRL_PLATFORM_OPT_IN_FLAG) > 0;
	return TRUE;
}

static GByteArray *
fu_acpi_dmar_write(FuFirmware *firmware, GError **error)
{
	FuAcpiDmar *self = FU_ACPI_DMAR(firmware);
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(FwupdMemoryInputStream) stream_payload = NULL;
	g_autoptr(FuStructAcpiDmar) st = fu_struct_acpi_dmar_new();

	/* flags */
	if (self->opt_in)
		fu_struct_acpi_dmar_set_flags(st, FU_DMAR_FLAG_DMA_CTRL_PLATFORM_OPT_IN_FLAG);

	/* FuAcpiTable->write */
	blob = fu_struct_acpi_dmar_to_bytes(st);
	stream_payload = fwupd_memory_input_stream_new_from_bytes(blob);
	fu_acpi_table_set_payload(FU_ACPI_TABLE(self), FWUPD_INPUT_STREAM(stream_payload));
	return FU_FIRMWARE_CLASS(fu_acpi_dmar_parent_class)->write(firmware, error);
}

FuAcpiDmar *
fu_acpi_dmar_new(void)
{
	return g_object_new(FU_TYPE_ACPI_DMAR, NULL);
}

gboolean
fu_acpi_dmar_get_opt_in(FuAcpiDmar *self)
{
	g_return_val_if_fail(FU_IS_ACPI_DMAR(self), FALSE);
	return self->opt_in;
}

void
fu_acpi_dmar_set_opt_in(FuAcpiDmar *self, gboolean opt_in)
{
	g_return_if_fail(FU_IS_ACPI_DMAR(self));
	self->opt_in = opt_in;
}

static void
fu_acpi_dmar_class_init(FuAcpiDmarClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_acpi_dmar_parse;
	firmware_class->write = fu_acpi_dmar_write;
}

static void
fu_acpi_dmar_init(FuAcpiDmar *self)
{
	fu_firmware_set_id(FU_FIRMWARE(self), "DMAR");
}
