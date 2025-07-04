/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-acpi-dmar.h"

struct _FuAcpiDmar {
	FuAcpiTable parent_instance;
	gboolean opt_in;
};

G_DEFINE_TYPE(FuAcpiDmar, fu_acpi_dmar, FU_TYPE_ACPI_TABLE)

#define DMAR_DMA_CTRL_PLATFORM_OPT_IN_FLAG 0x4

static gboolean
fu_acpi_dmar_parse(FuFirmware *firmware,
		   GInputStream *stream,
		   FuFirmwareParseFlags flags,
		   GError **error)
{
	FuAcpiDmar *self = FU_ACPI_DMAR(firmware);
	guint8 dma_flags = 0;

	/* FuAcpiTable->parse */
	if (!FU_FIRMWARE_CLASS(fu_acpi_dmar_parent_class)
		 ->parse(FU_FIRMWARE(self), stream, flags, error))
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
	if (!fu_input_stream_read_u8(stream, 0x25, &dma_flags, error))
		return FALSE;
	g_debug("Flags: 0x%02x", dma_flags);
	self->opt_in = (dma_flags & DMAR_DMA_CTRL_PLATFORM_OPT_IN_FLAG) > 0;
	return TRUE;
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

static void
fu_acpi_dmar_class_init(FuAcpiDmarClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_acpi_dmar_parse;
}

static void
fu_acpi_dmar_init(FuAcpiDmar *self)
{
}
