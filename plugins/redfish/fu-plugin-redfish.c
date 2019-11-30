/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"

#include "fu-redfish-client.h"
#include "fu-redfish-common.h"

struct FuPluginData {
	FuRedfishClient		*client;
};

gboolean
fu_plugin_update (FuPlugin *plugin,
		  FuDevice *device,
		  GBytes *blob_fw,
		  FwupdInstallFlags flags,
		  GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);

	return fu_redfish_client_update (data->client, device, blob_fw, error);
}

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
	g_autofree gchar *redfish_uri = NULL;
	g_autofree gchar *ca_check = NULL;
	g_autoptr(GBytes) smbios_data = NULL;

	/* optional */
	smbios_data = fu_plugin_get_smbios_data (plugin, REDFISH_SMBIOS_TABLE_TYPE);

	/* read the conf file */
	redfish_uri = fu_plugin_get_config_value (plugin, "Uri");
	if (redfish_uri != NULL) {
		g_autofree gchar *username = NULL;
		g_autofree gchar *password = NULL;
		const gchar *ip_str = NULL;
		g_auto(GStrv) split = NULL;
		guint64 port;

		if (g_str_has_prefix (redfish_uri, "https://")) {
			fu_redfish_client_set_https (data->client, TRUE);
			ip_str = redfish_uri + strlen ("https://");
		} else if (g_str_has_prefix (redfish_uri, "http://")) {
			fu_redfish_client_set_https (data->client, FALSE);
			ip_str = redfish_uri + strlen ("http://");
		} else {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "in valid scheme");
			return FALSE;
		}

		split = g_strsplit (ip_str, ":", 2);
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

		username = fu_plugin_get_config_value (plugin, "Username");
		password = fu_plugin_get_config_value (plugin, "Password");
		if (username != NULL && password != NULL) {
			fu_redfish_client_set_username (data->client, username);
			fu_redfish_client_set_password (data->client, password);
		}
	} else {
		if (smbios_data == NULL) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "no SMBIOS table");
			return FALSE;
		}
	}

	ca_check = fu_plugin_get_config_value (plugin, "CACheck");
	if (ca_check != NULL && g_ascii_strcasecmp (ca_check, "false") == 0)
		fu_redfish_client_set_cacheck (data->client, FALSE);
	else
		fu_redfish_client_set_cacheck (data->client, TRUE);

	return fu_redfish_client_setup (data->client, smbios_data, error);
}

void
fu_plugin_init (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	data->client = fu_redfish_client_new ();
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_object_unref (data->client);
}
