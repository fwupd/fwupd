/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"

struct FuPluginData {
	GMutex			 mutex;
};

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	g_debug ("init");
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	//FuPluginData *data = fu_plugin_get_data (plugin);
	g_debug ("destroy");
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	g_autoptr(FuDevice) device = NULL;
	device = fu_device_new ();
	fu_device_set_id (device, "FakeDevice");
	fu_device_add_guid (device, "b585990a-003e-5270-89d5-3705a17f9a43");
	fu_device_set_name (device, "Integrated_Webcam(TM)");
	fu_device_add_icon (device, "preferences-desktop-keyboard");
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_summary (device, "A fake webcam");
	fu_device_set_vendor (device, "ACME Corp.");
	fu_device_set_vendor_id (device, "USB:0x046D");
	fu_device_set_version_bootloader (device, "0.1.2");
	fu_device_set_version (device, "1.2.3");
	fu_device_set_version_lowest (device, "1.2.0");
	fu_plugin_device_register (plugin, device);
	if (fu_device_get_metadata (device, "BestDevice") == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "Device not set by another plugin");
		return FALSE;
	}
	fu_plugin_device_add (plugin, device);
	return TRUE;
}

void
fu_plugin_device_registered (FuPlugin *plugin, FuDevice *device)
{
	fu_device_set_metadata (device, "BestDevice", "/dev/urandom");
}

gboolean
fu_plugin_verify (FuPlugin *plugin,
		  FuDevice *device,
		  FuPluginVerifyFlags flags,
		  GError **error)
{
	if (g_strcmp0 (fu_device_get_version (device), "1.2.3") == 0) {
		fu_device_add_checksum (device, "7998cd212721e068b2411135e1f90d0ad436d730");
		fu_device_add_checksum (device, "dbae6a0309b3de8e850921631916a60b2956056e109fc82c586e3f9b64e2401a");
		return TRUE;
	}
	if (g_strcmp0 (fu_device_get_version (device), "1.2.4") == 0) {
		fu_device_add_checksum (device, "2b8546ba805ad10bf8a2e5ad539d53f303812ba5");
		fu_device_add_checksum (device, "b546c241029ce4e16c99eb6bfd77b86e4490aa3826ba71b8a4114e96a2d69bcd");
		return TRUE;
	}
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_NOT_SUPPORTED,
		     "no checksum for %s", fu_device_get_version (device));
	return FALSE;
}

gboolean
fu_plugin_update (FuPlugin *plugin,
		  FuDevice *device,
		  GBytes *blob_fw,
		  FwupdInstallFlags flags,
		  GError **error)
{
	fu_plugin_set_status (plugin, FWUPD_STATUS_DECOMPRESSING);
	for (guint i = 1; i <= 100; i++) {
		g_usleep (1000);
		fu_plugin_set_percentage (plugin, i);
	}
	fu_plugin_set_status (plugin, FWUPD_STATUS_DEVICE_WRITE);
	for (guint i = 1; i <= 100; i++) {
		g_usleep (1000);
		fu_plugin_set_percentage (plugin, i);
	}
	fu_plugin_set_status (plugin, FWUPD_STATUS_DEVICE_VERIFY);
	for (guint i = 1; i <= 100; i++) {
		g_usleep (1000);
		fu_plugin_set_percentage (plugin, i);
	}

	/* upgrade, or downgrade */
	if ((flags & FWUPD_INSTALL_FLAG_ALLOW_OLDER) == 0) {
		fu_device_set_version (device, "1.2.4");
	} else {
		fu_device_set_version (device, "1.2.3");
	}
	return TRUE;
}
