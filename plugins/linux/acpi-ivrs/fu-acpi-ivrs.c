/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 * Copyright 2022 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-acpi-ivrs.h"

struct _FuAcpiIvrs {
	FuAcpiTable parent_instance;
	gboolean remap_support;
};

G_DEFINE_TYPE(FuAcpiIvrs, fu_acpi_ivrs, FU_TYPE_ACPI_TABLE)

/* IVINfo field */
#define IVRS_DMA_REMAP_SUPPORT_FLAG 0x2

static gboolean
fu_acpi_ivrs_parse(FuFirmware *firmware,
		   GInputStream *stream,
		   FuFirmwareParseFlags flags,
		   GError **error)
{
	FuAcpiIvrs *self = FU_ACPI_IVRS(firmware);
	guint8 ivinfo = 0;

	/* FuAcpiTable->parse */
	if (!FU_FIRMWARE_CLASS(fu_acpi_ivrs_parent_class)
		 ->parse(FU_FIRMWARE(self), stream, flags, error))
		return FALSE;

	/* check signature and read flags */
	if (g_strcmp0(fu_firmware_get_id(FU_FIRMWARE(self)), "IVRS") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Not a IVRS table, got %s",
			    fu_firmware_get_id(FU_FIRMWARE(self)));
		return FALSE;
	}
	if (!fu_input_stream_read_u8(stream, 0x24, &ivinfo, error))
		return FALSE;
	g_debug("Flags: 0x%02x", ivinfo);
	self->remap_support = ivinfo & IVRS_DMA_REMAP_SUPPORT_FLAG;
	return TRUE;
}

FuAcpiIvrs *
fu_acpi_ivrs_new(void)
{
	return g_object_new(FU_TYPE_ACPI_IVRS, NULL);
}

gboolean
fu_acpi_ivrs_get_dma_remap(FuAcpiIvrs *self)
{
	g_return_val_if_fail(FU_IS_ACPI_IVRS(self), FALSE);
	return self->remap_support;
}

static void
fu_acpi_ivrs_class_init(FuAcpiIvrsClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_acpi_ivrs_parse;
}

static void
fu_acpi_ivrs_init(FuAcpiIvrs *self)
{
}
