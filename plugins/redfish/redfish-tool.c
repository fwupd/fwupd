/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useredfishl,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include "fu-common.h"
#include "fu-device.h"
#include "fu-smbios.h"

#include "redfish-client.h"
#include "redfish-common.h"

int
main (int argc, char **argv)
{
	GPtrArray *devices;
	GBytes *redfish_blob = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GError) error_smbios = NULL;
	g_autoptr(FuSmbios) smbios = fu_smbios_new ();
	g_autoptr(RedfishClient) client = redfish_client_new ();

	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

	if (!fu_smbios_setup (smbios, &error_smbios)) {
		g_printerr ("Failed to parse SMBIOS: %s\n", error_smbios->message);
	} else {
		redfish_blob = fu_smbios_get_data (smbios,
						   REDFISH_SMBIOS_TABLE_TYPE,
						   &error_smbios);
		if (redfish_blob == NULL)
			g_printerr ("No SMBIOS data: %s\n", error_smbios->message);
	}
	if (redfish_blob == NULL) {
		redfish_client_set_hostname (client, "localhost");
		redfish_client_set_port (client, 5000);
	}
	if (!redfish_client_setup (client, redfish_blob, &error)) {
		g_printerr ("Failed to setup: %s\n", error->message);
		return 1;
	}
	if (!redfish_client_coldplug (client, &error)) {
		g_printerr ("Failed to coldplug: %s\n", error->message);
		return 1;
	}
	devices = redfish_client_get_devices (client);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index (devices, i);
		g_autofree gchar *tmp = fwupd_device_to_string (FWUPD_DEVICE (device));
		g_print ("%s\n\n", tmp);
	}
	return 0;
}
