/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-redfish-backend.h"
#include "fu-redfish-common.h"
#include "fu-redfish-network.h"
#include "fu-redfish-smbios.h"

struct FuPluginData {
	FuRedfishBackend	*backend;
};

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GError) error_local = NULL;

	/* get the list of devices */
	if (!fu_backend_coldplug (FU_BACKEND (data->backend), &error_local)) {
		if (g_error_matches (error_local, FWUPD_ERROR, FWUPD_ERROR_AUTH_FAILED))
			fu_plugin_add_flag (plugin, FWUPD_PLUGIN_FLAG_AUTH_REQUIRED);
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}
	devices = fu_backend_get_devices (FU_BACKEND (data->backend));
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index (devices, i);
		fu_plugin_device_add (plugin, device);
	}
	return TRUE;
}

static gboolean
fu_redfish_plugin_discover_uefi_credentials (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	gsize bufsz = 0;
	guint32 indications = 0x0;
	g_autofree gchar *userpass_safe = NULL;
	g_autofree guint8 *buf = NULL;
	g_auto(GStrv) split = NULL;
	g_autoptr(GBytes) userpass = NULL;

	/* get the uint32 specifying if there are EFI variables set */
	if (!fu_efivar_get_data (REDFISH_EFI_INFORMATION_GUID,
				 REDFISH_EFI_INFORMATION_INDICATIONS,
				 &buf, &bufsz, NULL, error))
		return FALSE;
	if (!fu_common_read_uint32_safe (buf, bufsz, 0x0,
					 &indications, G_LITTLE_ENDIAN,
					 error))
		return FALSE;
	if ((indications & REDFISH_EFI_INDICATIONS_OS_CREDENTIALS) == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no indications for OS credentials");
		return FALSE;
	}

	/* read the correct EFI var for runtime */
	userpass = fu_efivar_get_data_bytes (REDFISH_EFI_INFORMATION_GUID,
					     REDFISH_EFI_INFORMATION_OS_CREDENTIALS,
					     NULL, error);
	if (userpass == NULL)
		return FALSE;

	/* it might not be NUL terminated */
	userpass_safe = g_strndup (g_bytes_get_data (userpass, NULL),
				   g_bytes_get_size (userpass));
	split = g_strsplit (userpass_safe, ":", -1);
	if (g_strv_length (split) != 2) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "invalid format for username:password, got '%s'",
			     userpass_safe);
		return FALSE;
	}
	fu_redfish_backend_set_username (data->backend, split[0]);
	fu_redfish_backend_set_password (data->backend, split[1]);
	return TRUE;
}

static gboolean
fu_redfish_plugin_discover_smbios_table (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	FuContext *ctx = fu_plugin_get_context (plugin);
	const gchar *smbios_data_fn;
	g_autofree gchar *hostname = NULL;
	g_autoptr(FuRedfishNetworkDevice) device = NULL;
	g_autoptr(FuRedfishSmbios) redfish_smbios = fu_redfish_smbios_new ();
	g_autoptr(GBytes) smbios_data = NULL;

	/* is optional if not in self tests */
	smbios_data_fn = g_getenv ("FWUPD_REDFISH_SMBIOS_DATA");
	if (smbios_data_fn != NULL) {
		smbios_data = fu_common_get_contents_bytes (smbios_data_fn, error);
		if (smbios_data == NULL)
			return FALSE;
	} else {
		smbios_data = fu_context_get_smbios_data (ctx, REDFISH_SMBIOS_TABLE_TYPE);
		if (smbios_data == NULL)
			return TRUE;
	}
	if (!fu_firmware_parse (FU_FIRMWARE (redfish_smbios),
				smbios_data, FWUPD_INSTALL_FLAG_NONE, error)) {
		g_prefix_error (error, "failed to parse SMBIOS table entry type 42: ");
		return FALSE;
	}

	/* get IP, falling back to hostname, then MAC, then VID:PID */
	hostname = g_strdup (fu_redfish_smbios_get_ip_addr (redfish_smbios));
	if (hostname == NULL)
		hostname = g_strdup (fu_redfish_smbios_get_hostname (redfish_smbios));
	if (device == NULL) {
		const gchar *mac_addr = fu_redfish_smbios_get_mac_addr (redfish_smbios);
		if (mac_addr != NULL) {
			g_autoptr(GError) error_network = NULL;
			device = fu_redfish_network_device_for_mac_addr (mac_addr, &error_network);
			if (device == NULL)
				g_debug ("failed to get device: %s", error_network->message);
		}
	}
	if (device == NULL) {
		guint16 vid = fu_redfish_smbios_get_vid (redfish_smbios);
		guint16 pid = fu_redfish_smbios_get_pid (redfish_smbios);
		if (vid != 0x0 && pid != 0x0) {
			g_autoptr(GError) error_network = NULL;
			device = fu_redfish_network_device_for_vid_pid (vid, pid, &error_network);
			if (device == NULL)
				g_debug ("failed to get device: %s", error_network->message);
		}
	}

	/* autoconnect device if required */
	if (device != NULL) {
		FuRedfishNetworkDeviceState state = FU_REDFISH_NETWORK_DEVICE_STATE_UNKNOWN;
		if (!fu_redfish_network_device_get_state (device, &state, error))
			return FALSE;
		if (g_getenv ("FWUPD_REDFISH_VERBOSE") != NULL) {
			g_debug ("device state is now %u", state);
		}
		if (state == FU_REDFISH_NETWORK_DEVICE_STATE_DISCONNECTED) {
			if (!fu_redfish_network_device_connect (device, error))
				return FALSE;
		}
		if (hostname == NULL) {
			hostname = fu_redfish_network_device_get_address (device, error);
			if (hostname == NULL)
				return FALSE;
		}
	}
	if (hostname == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no hostname");
		return FALSE;
	}
	fu_redfish_backend_set_hostname (data->backend, hostname);
	fu_redfish_backend_set_port (data->backend, fu_redfish_smbios_get_port (redfish_smbios));
	return TRUE;
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_autofree gchar *ca_check_str = NULL;
	g_autofree gchar *password = NULL;
	g_autofree gchar *redfish_uri = NULL;
	g_autofree gchar *username = NULL;
	g_autoptr(GError) error_uefi = NULL;

	/* optional */
	if (!fu_redfish_plugin_discover_smbios_table (plugin, error))
		return FALSE;
	if (!fu_redfish_plugin_discover_uefi_credentials (plugin, &error_uefi)) {
		g_debug ("failed to get username and password automatically: %s",
			 error_uefi->message);
	}

	/* override with the conf file */
	redfish_uri = fu_plugin_get_config_value (plugin, "Uri");
	if (redfish_uri != NULL) {
		const gchar *ip_str = NULL;
		g_auto(GStrv) split = NULL;
		guint64 port;

		if (g_str_has_prefix (redfish_uri, "https://")) {
			fu_redfish_backend_set_https (data->backend, TRUE);
			ip_str = redfish_uri + strlen ("https://");
		} else if (g_str_has_prefix (redfish_uri, "http://")) {
			fu_redfish_backend_set_https (data->backend, FALSE);
			ip_str = redfish_uri + strlen ("http://");
		} else {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "in valid scheme");
			return FALSE;
		}

		split = g_strsplit (ip_str, ":", 2);
		fu_redfish_backend_set_hostname (data->backend, split[0]);
		port = g_ascii_strtoull (split[1], NULL, 10);
		if (port == 0) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "no port specified");
			return FALSE;
		}
		fu_redfish_backend_set_port (data->backend, port);
	}
	username = fu_plugin_get_config_value (plugin, "Username");
	if (username != NULL)
		fu_redfish_backend_set_username (data->backend, username);
	password = fu_plugin_get_config_value (plugin, "Password");
	if (password != NULL)
		fu_redfish_backend_set_password (data->backend, password);
	ca_check_str = fu_plugin_get_config_value (plugin, "CACheck");
	if (ca_check_str != NULL) {
		gboolean ca_check = fu_plugin_get_config_value_boolean (plugin, "CACheck");
		fu_redfish_backend_set_cacheck (data->backend, ca_check);
	}
	if (fu_plugin_has_custom_flag (plugin, "wildcard-targets"))
		fu_redfish_backend_set_wildcard_targets (data->backend, TRUE);
	return fu_backend_setup (FU_BACKEND (data->backend), error);
}

void
fu_plugin_init (FuPlugin *plugin)
{
	FuContext *ctx = fu_plugin_get_context (plugin);
	FuPluginData *data = fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	data->backend = fu_redfish_backend_new (ctx);
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_firmware_gtype (plugin, NULL, FU_TYPE_REDFISH_SMBIOS);
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_object_unref (data->backend);
}
