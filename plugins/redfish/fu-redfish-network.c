/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-redfish-network.h"

#define NETWORK_MANAGER_SERVICE_NAME		"org.freedesktop.NetworkManager"
#define NETWORK_MANAGER_INTERFACE		"org.freedesktop.NetworkManager"
#define NETWORK_MANAGER_INTERFACE_IP4_CONFIG	"org.freedesktop.NetworkManager.IP4Config"
#define NETWORK_MANAGER_INTERFACE_DEVICE	"org.freedesktop.NetworkManager.Device"

typedef struct {
	gchar		*address;
	gboolean	 device_found;
	const gchar	*mac_addr;
	guint16		 vid;
	guint16		 pid;
} FuRedfishNetworkMatchHelper;

static gboolean
fu_redfish_network_ip_match_ip4_config (FuRedfishNetworkMatchHelper *helper,
					const gchar *object_path,
					GError **error)
{
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GVariant) addr_data = NULL;

	proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
					       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
					       NULL,
					       NETWORK_MANAGER_SERVICE_NAME,
					       object_path,
					       NETWORK_MANAGER_INTERFACE_IP4_CONFIG,
					       NULL,
					       error);
	if (proxy == NULL)
		return FALSE;

	addr_data = g_dbus_proxy_get_cached_property (proxy, "AddressData");
	if (addr_data != NULL) {
		g_autoptr(GVariant) addr_data0 = g_variant_get_child_value (addr_data, 0);
		g_autoptr(GVariantDict) dict = g_variant_dict_new (addr_data0);
		g_variant_dict_lookup (dict, "address", "s", &helper->address);
	}

	return TRUE;
}

static gboolean
fu_redfish_network_ip_match_device (FuRedfishNetworkMatchHelper *helper,
				    const gchar *object_path,
				    GError **error)
{
	g_autoptr(GDBusProxy) proxy = NULL;

	/* connect to device */
	proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
					       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
					       NULL,
					       NETWORK_MANAGER_SERVICE_NAME,
					       object_path,
					       NETWORK_MANAGER_INTERFACE_DEVICE,
					       NULL,
					       error);
	if (proxy == NULL)
		return FALSE;

	/* compare MAC address different */
	if (helper->mac_addr != NULL) {
		const gchar *mac_addr = NULL;
		g_autoptr(GVariant) hw_address = NULL;

		hw_address = g_dbus_proxy_get_cached_property (proxy, "HwAddress");
		if (hw_address == NULL)
			return TRUE;
		mac_addr = g_variant_get_string (hw_address, NULL);

		/* verify */
		if (g_getenv ("FWUPD_REDFISH_VERBOSE") != NULL)
			g_debug ("mac_addr=%s", mac_addr);
		if (g_strcmp0 (mac_addr, helper->mac_addr) == 0)
			helper->device_found = TRUE;
	}

	/* compare VID:PID */
	if (helper->vid != 0x0 && helper->pid != 0x0) {
#ifdef HAVE_GUDEV
		const gchar *sysfs_path = NULL;
		const gchar *tmp;
		guint16 pid = 0;
		guint16 vid = 0;
		g_autoptr(GVariant) udi = NULL;
		g_autoptr(GUdevClient) udev_client = NULL;
		g_autoptr(GUdevDevice) udev_device = NULL;

		udi = g_dbus_proxy_get_cached_property (proxy, "Udi");
		if (udi == NULL)
			return TRUE;
		sysfs_path = g_variant_get_string (udi, NULL);

		/* get the VID and PID */
		udev_client = g_udev_client_new (NULL);
		udev_device = g_udev_client_query_by_sysfs_path (udev_client, sysfs_path);
		if (udev_device == NULL)
			return TRUE;
		tmp = g_udev_device_get_property (udev_device, "ID_VENDOR_ID");
		if (tmp != NULL)
			vid = g_ascii_strtoull (tmp, NULL, 16);
		tmp = g_udev_device_get_property (udev_device, "ID_MODEL_ID");
		if (tmp != NULL)
			pid = g_ascii_strtoull (tmp, NULL, 16);

		/* verify */
		if (g_getenv ("FWUPD_REDFISH_VERBOSE") != NULL)
			g_debug ("%s: 0x%04x, 0x%04x", sysfs_path, vid, pid);
		if (vid == helper->vid && pid == helper->pid)
			helper->device_found = TRUE;
#else
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED,
				     "no UDev support");
		return FALSE;
#endif
	}

	/* look at the config */
	if (helper->device_found) {
		g_autoptr(GVariant) ip4_config = NULL;
		ip4_config = g_dbus_proxy_get_cached_property (proxy, "Ip4Config");
		if (ip4_config != NULL) {
			const gchar *tmp = g_variant_get_string (ip4_config, NULL);
			if (g_getenv ("FWUPD_REDFISH_VERBOSE") != NULL)
				g_debug ("using IP config %s", tmp);
			if (!fu_redfish_network_ip_match_ip4_config (helper, tmp, error))
				return FALSE;
		}
	}

	/* assume success */
	return TRUE;
}

static gboolean
fu_redfish_network_ip_match (FuRedfishNetworkMatchHelper *helper, GError **error)
{
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GVariant) devices = NULL;
	g_auto(GStrv) paths = NULL;

	/* get devices */
	proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
					       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
					       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
					       NULL,
					       NETWORK_MANAGER_SERVICE_NAME,
					       "/org/freedesktop/NetworkManager",
					       NETWORK_MANAGER_INTERFACE,
					       NULL,
					       error);
	if (proxy == NULL)
		return FALSE;
	devices = g_dbus_proxy_call_sync (proxy, "GetDevices",
					  NULL, G_DBUS_CALL_FLAGS_NONE, -1,
					  NULL, &error_local);
	if (devices == NULL) {
		if (g_error_matches (error_local, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN)) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_NOT_FOUND,
					     "NetworkManager is not running");
			return FALSE;
		}
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}

	/* look at each device */
	g_variant_get (devices, "(^ao)", &paths);
	for (guint i = 0; paths[i] != NULL; i++) {
		if (g_getenv ("FWUPD_REDFISH_VERBOSE") != NULL)
			g_debug ("device %u: %s", i, paths[i]);
		if (!fu_redfish_network_ip_match_device (helper, paths[i], error))
			return FALSE;
		if (helper->device_found)
			break;
	}
	if (!helper->device_found) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_FOUND,
				     "could not find device");
		return FALSE;
	}
	if (helper->address == NULL) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_CONNECTED,
				     "could not find IP address for device");
		return FALSE;
	}
	return TRUE;
}

gchar *
fu_redfish_network_ip_for_mac_addr (const gchar *mac_addr, GError **error)
{
	FuRedfishNetworkMatchHelper helper = {
		.mac_addr = mac_addr,
	};
	if (!fu_redfish_network_ip_match (&helper, error)) {
		g_prefix_error (error, "missing %s: ", mac_addr);
		return NULL;
	}
	return helper.address;
}

gchar *
fu_redfish_network_ip_for_vid_pid (guint16 vid, guint16 pid, GError **error)
{
	FuRedfishNetworkMatchHelper helper = {
		.vid = vid,
		.pid = pid,
	};
	if (!fu_redfish_network_ip_match (&helper, error)) {
		g_prefix_error (error, "missing 0x%04x:0x%04x: ", vid, pid);
		return NULL;
	}
	return helper.address;
}
