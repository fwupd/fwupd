/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"

#include "fu-redfish-client.h"
#include "fu-redfish-common.h"

struct FuPluginData {
	FuRedfishClient		*client;
};

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	GPtrArray *devices;

	/* get the list of devices */
	if (!fu_redfish_client_coldplug (data->client, error))
		return FALSE;
	devices = fu_redfish_client_get_devices (data->client);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index (devices, i);
		fu_plugin_device_add (plugin, device);
	}
	return TRUE;
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	GBytes *smbios_data = fu_plugin_get_smbios_data (plugin, REDFISH_SMBIOS_TABLE_TYPE);
	const gchar *redfish_uri;

	/* using the emulator */
	redfish_uri = g_getenv ("FWUPD_REDFISH_URI");
	if (redfish_uri != NULL) {
		guint64 port;
		g_auto(GStrv) split = g_strsplit (redfish_uri, ":", 2);
		fu_redfish_client_set_hostname (data->client, split[0]);
		port = g_ascii_strtoull (split[1], NULL, 10);
		if (port == 0) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "no port specified");
			return FALSE;
		}
		fu_redfish_client_set_port (data->client, port);
	} else {
		if (smbios_data == NULL) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "no SMBIOS table");
			return FALSE;
		}
	}
	return fu_redfish_client_setup (data->client, smbios_data, error);
}

void
fu_plugin_init (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	data->client = fu_redfish_client_new ();
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_object_unref (data->client);
}
