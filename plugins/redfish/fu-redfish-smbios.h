/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_REDFISH_SMBIOS (fu_redfish_smbios_get_type ())
G_DECLARE_FINAL_TYPE (FuRedfishSmbios, fu_redfish_smbios, FU, REDFISH_SMBIOS, FuFirmware)

FuRedfishSmbios	*fu_redfish_smbios_new		(void);

guint16		 fu_redfish_smbios_get_port	(FuRedfishSmbios	*self);
const gchar	*fu_redfish_smbios_get_hostname	(FuRedfishSmbios	*self);
const gchar	*fu_redfish_smbios_get_ip_addr	(FuRedfishSmbios	*self);
