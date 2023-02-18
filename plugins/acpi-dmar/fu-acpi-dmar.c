/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-acpi-dmar.h"

struct _FuAcpiDmar {
	FuAcpiTable parent_instance;
	gboolean opt_in;
};

G_DEFINE_TYPE(FuAcpiDmar, fu_acpi_dmar, FU_TYPE_ACPI_TABLE)

#define DMAR_DMA_CTRL_PLATFORM_OPT_IN_FLAG 0x4

FuAcpiDmar *
fu_acpi_dmar_new(GBytes *blob, GError **error)
{
	FuAcpiDmar *self = g_object_new(FU_TYPE_ACPI_DMAR, NULL);
	gsize bufsz = 0;
	guint8 flags = 0;
	const guint8 *buf = g_bytes_get_data(blob, &bufsz);

	/* FuAcpiTable->parse */
	if (!FU_FIRMWARE_CLASS(fu_acpi_dmar_parent_class)
		 ->parse(FU_FIRMWARE(self), blob, 0x0, FWUPD_INSTALL_FLAG_NONE, error))
		return NULL;

	/* check signature and read flags */
	if (g_strcmp0(fu_firmware_get_id(FU_FIRMWARE(self)), "DMAR") != 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "Not a DMAR table, got %s",
			    fu_firmware_get_id(FU_FIRMWARE(self)));
		return NULL;
	}
	if (!fu_memread_uint8_safe(buf, bufsz, 0x25, &flags, error))
		return NULL;
	g_debug("Flags: 0x%02x", flags);
	self->opt_in = (flags & DMAR_DMA_CTRL_PLATFORM_OPT_IN_FLAG) > 0;
	return self;
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
}

static void
fu_acpi_dmar_init(FuAcpiDmar *self)
{
}
