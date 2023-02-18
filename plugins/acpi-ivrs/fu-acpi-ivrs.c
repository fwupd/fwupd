/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2022 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
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

FuAcpiIvrs *
fu_acpi_ivrs_new(GBytes *blob, GError **error)
{
	FuAcpiIvrs *self = g_object_new(FU_TYPE_ACPI_IVRS, NULL);
	gsize bufsz = 0;
	guint8 ivinfo = 0;
	const guint8 *buf = g_bytes_get_data(blob, &bufsz);

	/* FuAcpiTable->parse */
	if (!FU_FIRMWARE_CLASS(fu_acpi_ivrs_parent_class)
		 ->parse(FU_FIRMWARE(self), blob, 0x0, FWUPD_INSTALL_FLAG_NONE, error))
		return NULL;

	/* check signature and read flags */
	if (g_strcmp0(fu_firmware_get_id(FU_FIRMWARE(self)), "IVRS") != 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "Not a IVRS table, got %s",
			    fu_firmware_get_id(FU_FIRMWARE(self)));
		return NULL;
	}
	if (!fu_memread_uint8_safe(buf, bufsz, 0x24, &ivinfo, error))
		return NULL;
	g_debug("Flags: 0x%02x", ivinfo);
	self->remap_support = ivinfo & IVRS_DMA_REMAP_SUPPORT_FLAG;
	return self;
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
}

static void
fu_acpi_ivrs_init(FuAcpiIvrs *self)
{
}
