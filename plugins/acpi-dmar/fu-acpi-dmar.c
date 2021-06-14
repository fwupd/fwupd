/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>
#include <string.h>

#include "fu-acpi-dmar.h"

struct _FuAcpiDmar {
	GObject		 parent_instance;
	gboolean	 opt_in;
};

G_DEFINE_TYPE (FuAcpiDmar, fu_acpi_dmar, G_TYPE_OBJECT)

#define DMAR_DMA_CTRL_PLATFORM_OPT_IN_FLAG	0x4

FuAcpiDmar *
fu_acpi_dmar_new (GBytes *blob, GError **error)
{
	FuAcpiDmar *self = g_object_new (FU_TYPE_ACPI_DMAR, NULL);
	gchar creator_id[5] = { '\0' };
	gchar oem_table_id[9] = { '\0' };
	gchar signature[5] = { '\0' };
	gsize bufsz = 0;
	guint8 flags = 0;
	const guint8 *buf = g_bytes_get_data (blob, &bufsz);

	/* parse table */
	if (!fu_memcpy_safe ((guint8 *) signature, sizeof(signature), 0x0,	/* dst */
			     buf, bufsz, 0x00,					/* src */
			     sizeof(signature) - 1, error))
		return NULL;
	if (strcmp (signature, "DMAR") != 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "Not a DMAR table, got %s",
			     signature);
		return NULL;
	}
	if (!fu_memcpy_safe ((guint8 *) oem_table_id, sizeof(oem_table_id), 0x0,/* dst */
			     buf, bufsz, 0x10,					/* src */
			     sizeof(oem_table_id) - 1, error))
		return NULL;
	g_debug ("OemTableId: %s", oem_table_id);
	if (!fu_memcpy_safe ((guint8 *) creator_id, sizeof(creator_id), 0x0,	/* dst */
			     buf, bufsz, 0x1c,					/* src */
			     sizeof(creator_id) - 1, error))
		return NULL;
	g_debug ("CreatorId: %s", creator_id);
	if (!fu_memcpy_safe (&flags, sizeof(flags), 0x0,			/* dst */
			     buf, bufsz, 0x25,					/* src */
			     sizeof(flags), error))
		return NULL;
	g_debug ("Flags: 0x%02x", flags);
	self->opt_in = (flags & DMAR_DMA_CTRL_PLATFORM_OPT_IN_FLAG) > 0;
	return self;
}

gboolean
fu_acpi_dmar_get_opt_in (FuAcpiDmar *self)
{
	g_return_val_if_fail (FU_IS_ACPI_DMAR (self), FALSE);
	return self->opt_in;
}

static void
fu_acpi_dmar_class_init (FuAcpiDmarClass *klass)
{
}

static void
fu_acpi_dmar_init (FuAcpiDmar *self)
{
}
