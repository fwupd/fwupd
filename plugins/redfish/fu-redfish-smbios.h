/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-redfish-struct.h"

#define FU_TYPE_REDFISH_SMBIOS (fu_redfish_smbios_get_type())
G_DECLARE_FINAL_TYPE(FuRedfishSmbios, fu_redfish_smbios, FU, REDFISH_SMBIOS, FuFirmware)

FuRedfishSmbios *
fu_redfish_smbios_new(void);

FuRedfishSmbiosInterfaceType
fu_redfish_smbios_get_interface_type(FuRedfishSmbios *self);
guint16
fu_redfish_smbios_get_port(FuRedfishSmbios *self);
guint16
fu_redfish_smbios_get_vid(FuRedfishSmbios *self);
guint16
fu_redfish_smbios_get_pid(FuRedfishSmbios *self);
const gchar *
fu_redfish_smbios_get_hostname(FuRedfishSmbios *self);
const gchar *
fu_redfish_smbios_get_mac_addr(FuRedfishSmbios *self);
const gchar *
fu_redfish_smbios_get_ip_addr(FuRedfishSmbios *self);
