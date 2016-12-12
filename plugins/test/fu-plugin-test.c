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

	/* only enable when testing */
	if (g_getenv ("FWUPD_ENABLE_TEST_PLUGIN") == NULL) {
		fu_plugin_set_enabled (plugin, FALSE);
		return;
	}
	g_debug ("init");
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	//FuPluginData *data = fu_plugin_get_data (plugin);
	g_debug ("destroy");
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	g_debug ("startup");
	return TRUE;
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	g_autoptr(FuDevice) device = NULL;
	device = fu_device_new ();
	fu_device_set_id (device, "FakeDevice");
	fu_device_add_guid (device, "00000000-0000-0000-0000-000000000000");
	fu_device_set_name (device, "Integrated_Webcam(TM)");
	fu_plugin_device_add (plugin, device);
	return TRUE;
}

gboolean
fu_plugin_update_online (FuPlugin *plugin,
			 FuDevice *device,
			 GBytes *blob_fw,
			 FwupdInstallFlags flags,
			 GError **error)
{
	if (flags & FWUPD_INSTALL_FLAG_OFFLINE) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "cannot handle offline");
	}
	fu_plugin_set_status (plugin, FWUPD_STATUS_DECOMPRESSING);
	fu_plugin_set_status (plugin, FWUPD_STATUS_DEVICE_WRITE);
	return TRUE;
}
