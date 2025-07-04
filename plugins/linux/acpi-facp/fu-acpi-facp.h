/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <gio/gio.h>

#define FU_TYPE_ACPI_FACP (fu_acpi_facp_get_type())
G_DECLARE_FINAL_TYPE(FuAcpiFacp, fu_acpi_facp, FU, ACPI_FACP, GObject)

FuAcpiFacp *
fu_acpi_facp_new(GBytes *blob, GError **error);
gboolean
fu_acpi_facp_get_s2i(FuAcpiFacp *self);
