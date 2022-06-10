/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-redfish-network-device.h"
#include "fu-redfish-network.h"

struct _FuRedfishNetworkDevice {
	GObject parent_instance;
	gchar *object_path;
};

G_DEFINE_TYPE(FuRedfishNetworkDevice, fu_redfish_network_device, G_TYPE_OBJECT)

const gchar *
fu_redfish_network_device_state_to_string(FuRedfishNetworkDeviceState state)
{
	if (state == FU_REDFISH_NETWORK_DEVICE_STATE_UNKNOWN)
		return "unknown";
	if (state == FU_REDFISH_NETWORK_DEVICE_STATE_UNMANAGED)
		return "unmanaged";
	if (state == FU_REDFISH_NETWORK_DEVICE_STATE_UNAVAILABLE)
		return "unavailable";
	if (state == FU_REDFISH_NETWORK_DEVICE_STATE_DISCONNECTED)
		return "disconnected";
	if (state == FU_REDFISH_NETWORK_DEVICE_STATE_PREPARE)
		return "prepare";
	if (state == FU_REDFISH_NETWORK_DEVICE_STATE_CONFIG)
		return "config";
	if (state == FU_REDFISH_NETWORK_DEVICE_STATE_NEED_AUTH)
		return "need-auth";
	if (state == FU_REDFISH_NETWORK_DEVICE_STATE_IP_CONFIG)
		return "ip-config";
	if (state == FU_REDFISH_NETWORK_DEVICE_STATE_IP_CHECK)
		return "ip-check";
	if (state == FU_REDFISH_NETWORK_DEVICE_STATE_SECONDARIES)
		return "secondaries";
	if (state == FU_REDFISH_NETWORK_DEVICE_STATE_ACTIVATED)
		return "activated";
	if (state == FU_REDFISH_NETWORK_DEVICE_STATE_DEACTIVATING)
		return "deactivating";
	if (state == FU_REDFISH_NETWORK_DEVICE_STATE_FAILED)
		return "failed";
	return NULL;
}

gboolean
fu_redfish_network_device_get_state(FuRedfishNetworkDevice *self,
				    FuRedfishNetworkDeviceState *state,
				    GError **error)
{
	g_autoptr(GVariant) retval = NULL;
	g_autoptr(GDBusProxy) proxy = NULL;

	g_return_val_if_fail(FU_IS_REDFISH_NETWORK_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* connect to device */
	proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
					      G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
					      NULL,
					      NETWORK_MANAGER_SERVICE_NAME,
					      self->object_path,
					      NETWORK_MANAGER_INTERFACE_DEVICE,
					      NULL,
					      error);
	if (proxy == NULL)
		return FALSE;
	retval = g_dbus_proxy_get_cached_property(proxy, "State");
	if (retval == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_CONNECTED,
				    "could not find State");
		return FALSE;
	}
	if (state != NULL)
		*state = g_variant_get_uint32(retval);
	return TRUE;
}

gboolean
fu_redfish_network_device_connect(FuRedfishNetworkDevice *self, GError **error)
{
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GTimer) timer = g_timer_new();
	g_autoptr(GVariant) success = NULL;

	g_return_val_if_fail(FU_IS_REDFISH_NETWORK_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* connect to manager */
	proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
					      G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
					      NULL,
					      NETWORK_MANAGER_SERVICE_NAME,
					      NETWORK_MANAGER_PATH,
					      NETWORK_MANAGER_INTERFACE,
					      NULL,
					      error);
	if (proxy == NULL)
		return FALSE;

	/* activate with some good defaults */
	success = g_dbus_proxy_call_sync(proxy,
					 "ActivateConnection",
					 g_variant_new("(ooo)", "/", self->object_path, "/"),
					 G_DBUS_CALL_FLAGS_NONE,
					 -1,
					 NULL,
					 error);
	if (success == NULL)
		return FALSE;

	/* wait until the network interface comes up */
	do {
		FuRedfishNetworkDeviceState state = FU_REDFISH_NETWORK_DEVICE_STATE_UNKNOWN;
		if (!fu_redfish_network_device_get_state(self, &state, error))
			return FALSE;
		if (g_getenv("FWUPD_REDFISH_VERBOSE") != NULL) {
			g_debug("%s device state is now %s [%u]",
				self->object_path,
				fu_redfish_network_device_state_to_string(state),
				state);
		}
		if (state == FU_REDFISH_NETWORK_DEVICE_STATE_ACTIVATED)
			return TRUE;
		g_usleep(50 * 1000);
	} while (g_timer_elapsed(timer, NULL) < 5.f);

	/* timed out */
	g_set_error_literal(error,
			    G_IO_ERROR,
			    G_IO_ERROR_TIMED_OUT,
			    "could not activate connection");
	return FALSE;
}

gchar *
fu_redfish_network_device_get_address(FuRedfishNetworkDevice *self, GError **error)
{
	g_autofree gchar *address = NULL;
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GDBusProxy) proxy2 = NULL;
	g_autoptr(GVariant) addr_data = NULL;
	g_autoptr(GVariant) ip4_config = NULL;

	g_return_val_if_fail(FU_IS_REDFISH_NETWORK_DEVICE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* connect to device */
	proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
					      G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
					      NULL,
					      NETWORK_MANAGER_SERVICE_NAME,
					      self->object_path,
					      NETWORK_MANAGER_INTERFACE_DEVICE,
					      NULL,
					      error);
	if (proxy == NULL)
		return NULL;
	ip4_config = g_dbus_proxy_get_cached_property(proxy, "Ip4Config");
	if (ip4_config == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_CONNECTED,
				    "could not find IPv4 config");
		return NULL;
	}
	proxy2 = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
					       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
					       NULL,
					       NETWORK_MANAGER_SERVICE_NAME,
					       g_variant_get_string(ip4_config, NULL),
					       NETWORK_MANAGER_INTERFACE_IP4_CONFIG,
					       NULL,
					       error);
	if (proxy2 == NULL)
		return NULL;

	addr_data = g_dbus_proxy_get_cached_property(proxy2, "AddressData");
	if (addr_data != NULL) {
		g_autoptr(GVariant) addr_data0 = g_variant_get_child_value(addr_data, 0);
		g_autoptr(GVariantDict) dict = g_variant_dict_new(addr_data0);
		g_variant_dict_lookup(dict, "address", "s", &address);
	}
	if (address == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_CONNECTED,
				    "could not find IP address for device");
		return NULL;
	}

	/* success */
	return g_steal_pointer(&address);
}

FuRedfishNetworkDevice *
fu_redfish_network_device_new(const gchar *object_path)
{
	FuRedfishNetworkDevice *self = g_object_new(FU_TYPE_REDFISH_NETWORK_DEVICE, NULL);
	self->object_path = g_strdup(object_path);
	return self;
}

static void
fu_redfish_network_device_init(FuRedfishNetworkDevice *self)
{
}

static void
fu_redfish_network_device_finalize(GObject *object)
{
	FuRedfishNetworkDevice *self = FU_REDFISH_NETWORK_DEVICE(object);
	g_free(self->object_path);
	G_OBJECT_CLASS(fu_redfish_network_device_parent_class)->finalize(object);
}

static void
fu_redfish_network_device_class_init(FuRedfishNetworkDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_redfish_network_device_finalize;
}
