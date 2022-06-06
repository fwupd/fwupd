/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <string.h>

#include "fu-acpi-facp.h"

struct _FuAcpiFacp {
	GObject parent_instance;
	gboolean get_s2i;
};

G_DEFINE_TYPE(FuAcpiFacp, fu_acpi_facp, G_TYPE_OBJECT)

#define LOW_POWER_S0_IDLE_CAPABLE (1 << 21)

FuAcpiFacp *
fu_acpi_facp_new(GBytes *blob, GError **error)
{
	FuAcpiFacp *self = g_object_new(FU_TYPE_ACPI_FACP, NULL);
	gsize bufsz = 0;
	guint32 flags = 0;
	const guint8 *buf = g_bytes_get_data(blob, &bufsz);

	/* parse table */
	if (!fu_memread_uint32_safe(buf, bufsz, 0x70, &flags, G_LITTLE_ENDIAN, error))
		return NULL;
	g_debug("Flags: 0x%04x", flags);
	self->get_s2i = (flags & LOW_POWER_S0_IDLE_CAPABLE) > 0;
	return self;
}

gboolean
fu_acpi_facp_get_s2i(FuAcpiFacp *self)
{
	g_return_val_if_fail(FU_IS_ACPI_FACP(self), FALSE);
	return self->get_s2i;
}

static void
fu_acpi_facp_class_init(FuAcpiFacpClass *klass)
{
}

static void
fu_acpi_facp_init(FuAcpiFacp *self)
{
}
