/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2022 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <string.h>

#include "fu-acpi-ivrs.h"

struct _FuAcpiIvrs {
	GObject parent_instance;
	gboolean remap_support;
};

G_DEFINE_TYPE(FuAcpiIvrs, fu_acpi_ivrs, G_TYPE_OBJECT)

/* IVINfo field */
#define IVRS_DMA_REMAP_SUPPORT_FLAG 0x2

FuAcpiIvrs *
fu_acpi_ivrs_new(GBytes *blob, GError **error)
{
	FuAcpiIvrs *self = g_object_new(FU_TYPE_ACPI_IVRS, NULL);
	gchar creator_id[5] = {'\0'};
	gchar oem_table_id[9] = {'\0'};
	gchar signature[5] = {'\0'};
	gsize bufsz = 0;
	guint8 ivinfo = 0;
	const guint8 *buf = g_bytes_get_data(blob, &bufsz);

	/* parse table */
	if (!fu_memcpy_safe((guint8 *)signature,
			    sizeof(signature),
			    0x0, /* dst */
			    buf,
			    bufsz,
			    0x00, /* src */
			    sizeof(signature) - 1,
			    error))
		return NULL;
	if (strcmp(signature, "IVRS") != 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "Not a IVRS table, got %s",
			    signature);
		return NULL;
	}
	if (!fu_memcpy_safe((guint8 *)oem_table_id,
			    sizeof(oem_table_id),
			    0x0, /* dst */
			    buf,
			    bufsz,
			    0x10, /* src */
			    sizeof(oem_table_id) - 1,
			    error))
		return NULL;
	g_debug("OemTableId: %s", oem_table_id);
	if (!fu_memcpy_safe((guint8 *)creator_id,
			    sizeof(creator_id),
			    0x0, /* dst */
			    buf,
			    bufsz,
			    0x1c, /* src */
			    sizeof(creator_id) - 1,
			    error))
		return NULL;
	g_debug("CreatorId: %s", creator_id);
	if (!fu_memcpy_safe(&ivinfo,
			    sizeof(ivinfo),
			    0x0, /* dst */
			    buf,
			    bufsz,
			    0x24, /* src */
			    sizeof(ivinfo),
			    error))
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
