 /*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>
#include <efivar.h>

/* SMBIOS */
#define REDFISH_SMBIOS_TABLE_TYPE			0x42

#define REDFISH_PROTOCOL_REDFISH_OVER_IP		0x04

#define REDFISH_CONTROLLER_INTERFACE_TYPE_NETWORK_HOST	0x40

#define REDFISH_INTERFACE_TYPE_USB_NEWORK		0x02
#define REDFISH_INTERFACE_TYPE_PCI_NEWORK		0x03

#define REDFISH_IP_ASSIGNMENT_TYPE_STATIC		0x00
#define REDFISH_IP_ASSIGNMENT_TYPE_DHCP			0x02
#define REDFISH_IP_ASSIGNMENT_TYPE_AUTO_CONFIG		0x03
#define REDFISH_IP_ASSIGNMENT_TYPE_HOST_SELECT		0x04

#define REDFISH_IP_ADDRESS_FORMAT_UNKNOWN		0x00
#define REDFISH_IP_ADDRESS_FORMAT_V4			0x01
#define REDFISH_IP_ADDRESS_FORMAT_V6			0x02

/* EFI */
#define REDFISH_EFI_INFORMATION_GUID			EFI_GUID(0x16faa37e,0x4b6a,0x4891,0x9028,0x24,0x2d,0xe6,0x5a,0x3b,0x70)

#define REDFISH_EFI_INFORMATION_INDICATIONS		"RedfishIndications"
#define REDFISH_EFI_INFORMATION_FW_CREDENTIALS		"RedfishFWCredentials"
#define REDFISH_EFI_INFORMATION_OS_CREDENTIALS		"RedfishOSCredentials"

#define REDFISH_EFI_INDICATIONS_FW_CREDENTIALS		0x00000001
#define REDFISH_EFI_INDICATIONS_OS_CREDENTIALS		0x00000002

/* shared */
GBytes		*fu_redfish_common_get_evivar_raw	(efi_guid_t	 guid,
							 const gchar	*name,
							 GError		**error);
gchar		*fu_redfish_common_buffer_to_ipv4	(const guint8	*buffer);
gchar		*fu_redfish_common_buffer_to_ipv6	(const guint8	*buffer);
