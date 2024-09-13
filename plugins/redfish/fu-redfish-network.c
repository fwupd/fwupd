/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-redfish-network.h"

typedef struct {
	FuContext *ctx;
	FuRedfishNetworkDevice *device;
	const gchar *mac_addr;
	guint16 vid;
	guint16 pid;
} FuRedfishNetworkMatchHelper;

static gboolean
fu_redfish_network_device_match_device(FuRedfishNetworkMatchHelper *helper,
				       const gchar *object_path,
				       GError **error)
{
	g_autoptr(GDBusProxy) proxy = NULL;

	/* connect to device */
	proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
					      G_DBUS_PROXY_FLAGS_NONE,
					      NULL,
					      NETWORK_MANAGER_SERVICE_NAME,
					      object_path,
					      NETWORK_MANAGER_INTERFACE_DEVICE,
					      NULL,
					      error);
	if (proxy == NULL) {
		g_prefix_error(error, "failed to connect to interface %s: ", object_path);
		return FALSE;
	}

	/* compare MAC address different */
	if (helper->mac_addr != NULL) {
		const gchar *mac_addr = NULL;
		g_autoptr(GVariant) hw_address = NULL;

		hw_address = g_dbus_proxy_get_cached_property(proxy, "HwAddress");
		if (hw_address == NULL)
			return TRUE;
		mac_addr = g_variant_get_string(hw_address, NULL);

		/* verify */
		g_debug("mac_addr=%s", mac_addr);
		if (g_strcmp0(mac_addr, helper->mac_addr) == 0)
			helper->device = fu_redfish_network_device_new(object_path);
	}

	/* compare VID:PID */
	if (helper->vid != 0x0 && helper->pid != 0x0) {
		g_autoptr(FuBackend) udev_backend = NULL;
		g_autoptr(FuDevice) udev_device = NULL;
		g_autoptr(GVariant) udi = NULL;

		udi = g_dbus_proxy_get_cached_property(proxy, "Udi");
		if (udi == NULL)
			return TRUE;
		udev_backend = fu_context_get_backend_by_name(helper->ctx, "udev", error);
		if (udev_backend == NULL)
			return FALSE;
		udev_device =
		    fu_backend_create_device(udev_backend, g_variant_get_string(udi, NULL), error);
		if (udev_device == NULL)
			return FALSE;

		/* verify */
		g_debug("%s: 0x%04x, 0x%04x",
			g_variant_get_string(udi, NULL),
			fu_device_get_vid(udev_device),
			fu_device_get_pid(udev_device));
		if (fu_device_get_vid(udev_device) == helper->vid &&
		    fu_device_get_pid(udev_device) == helper->pid)
			helper->device = fu_redfish_network_device_new(object_path);
	}

	/* assume success */
	return TRUE;
}

static gboolean
fu_redfish_network_device_match(FuRedfishNetworkMatchHelper *helper, GError **error)
{
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GVariant) devices = NULL;
	g_auto(GStrv) paths = NULL;

	/* get devices */
	proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
					      G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
						  G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
					      NULL,
					      NETWORK_MANAGER_SERVICE_NAME,
					      NETWORK_MANAGER_PATH,
					      NETWORK_MANAGER_INTERFACE,
					      NULL,
					      &error_local);
	if (proxy == NULL) {
		if (g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_DBUS_ERROR) ||
		    g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY) ||
		    g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "D-Bus is not running");
			return FALSE;
		}
		g_propagate_prefixed_error(error,
					   g_steal_pointer(&error_local),
					   "failed to construct proxy for %s: ",
					   NETWORK_MANAGER_SERVICE_NAME);
		return FALSE;
	}
	devices = g_dbus_proxy_call_sync(proxy,
					 "GetDevices",
					 NULL,
					 G_DBUS_CALL_FLAGS_NONE,
					 -1,
					 NULL,
					 &error_local);
	if (devices == NULL) {
		if (g_error_matches(error_local, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN) ||
		    g_error_matches(error_local, G_DBUS_ERROR, G_DBUS_ERROR_NAME_HAS_NO_OWNER)) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "NetworkManager is not running");
			return FALSE;
		}
		g_propagate_prefixed_error(error,
					   g_steal_pointer(&error_local),
					   "failed to call GetDevices() on %s: ",
					   g_dbus_proxy_get_name(proxy));
		return FALSE;
	}

	/* look at each device */
	g_variant_get(devices, "(^ao)", &paths);
	for (guint i = 0; paths[i] != NULL; i++) {
		g_debug("device %u: %s", i, paths[i]);
		if (!fu_redfish_network_device_match_device(helper, paths[i], error))
			return FALSE;
		if (helper->device != NULL)
			break;
	}
	if (helper->device == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "could not find device");
		return FALSE;
	}
	return TRUE;
}

FuRedfishNetworkDevice *
fu_redfish_network_device_for_mac_addr(FuContext *ctx, const gchar *mac_addr, GError **error)
{
	FuRedfishNetworkMatchHelper helper = {
	    .ctx = ctx,
	    .mac_addr = mac_addr,
	};
	if (!fu_redfish_network_device_match(&helper, error)) {
		g_prefix_error(error, "missing %s: ", mac_addr);
		return NULL;
	}
	return helper.device;
}

FuRedfishNetworkDevice *
fu_redfish_network_device_for_vid_pid(FuContext *ctx, guint16 vid, guint16 pid, GError **error)
{
	FuRedfishNetworkMatchHelper helper = {
	    .ctx = ctx,
	    .vid = vid,
	    .pid = pid,
	};
	if (!fu_redfish_network_device_match(&helper, error)) {
		g_prefix_error(error, "missing 0x%04x:0x%04x: ", vid, pid);
		return NULL;
	}
	return helper.device;
}
