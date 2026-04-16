/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <gio/gio.h>

#define FU_TYPE_ACPI_FACP (fu_acpi_facp_get_type())
G_DECLARE_FINAL_TYPE(FuAcpiFacp, fu_acpi_facp, FU, ACPI_FACP, GObject)

/* ACPI FADT Preferred_PM_Profile values */
#define FU_ACPI_FACP_PM_PROFILE_UNSPECIFIED	   0
#define FU_ACPI_FACP_PM_PROFILE_DESKTOP		   1
#define FU_ACPI_FACP_PM_PROFILE_MOBILE		   2
#define FU_ACPI_FACP_PM_PROFILE_WORKSTATION	   3
#define FU_ACPI_FACP_PM_PROFILE_ENTERPRISE_SERVER  4
#define FU_ACPI_FACP_PM_PROFILE_SOHO_SERVER	   5
#define FU_ACPI_FACP_PM_PROFILE_APPLIANCE_PC	   6
#define FU_ACPI_FACP_PM_PROFILE_PERFORMANCE_SERVER 7
#define FU_ACPI_FACP_PM_PROFILE_TABLET		   8

FuAcpiFacp *
fu_acpi_facp_new(GBytes *blob, GError **error);
gboolean
fu_acpi_facp_get_s2i(FuAcpiFacp *self);
guint8
fu_acpi_facp_get_pm_profile(FuAcpiFacp *self);
gboolean
fu_acpi_facp_is_server(FuAcpiFacp *self);
