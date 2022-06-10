/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_REDFISH_NETWORK_DEVICE (fu_redfish_network_device_get_type())
G_DECLARE_FINAL_TYPE(FuRedfishNetworkDevice,
		     fu_redfish_network_device,
		     FU,
		     REDFISH_NETWORK_DEVICE,
		     GObject)

typedef enum {
	FU_REDFISH_NETWORK_DEVICE_STATE_UNKNOWN = 0,
	FU_REDFISH_NETWORK_DEVICE_STATE_UNMANAGED = 10,
	FU_REDFISH_NETWORK_DEVICE_STATE_UNAVAILABLE = 20,
	FU_REDFISH_NETWORK_DEVICE_STATE_DISCONNECTED = 30,
	FU_REDFISH_NETWORK_DEVICE_STATE_PREPARE = 40,
	FU_REDFISH_NETWORK_DEVICE_STATE_CONFIG = 50,
	FU_REDFISH_NETWORK_DEVICE_STATE_NEED_AUTH = 60,
	FU_REDFISH_NETWORK_DEVICE_STATE_IP_CONFIG = 70,
	FU_REDFISH_NETWORK_DEVICE_STATE_IP_CHECK = 80,
	FU_REDFISH_NETWORK_DEVICE_STATE_SECONDARIES = 90,
	FU_REDFISH_NETWORK_DEVICE_STATE_ACTIVATED = 100,
	FU_REDFISH_NETWORK_DEVICE_STATE_DEACTIVATING = 110,
	FU_REDFISH_NETWORK_DEVICE_STATE_FAILED = 120,
} FuRedfishNetworkDeviceState;

const gchar *
fu_redfish_network_device_state_to_string(FuRedfishNetworkDeviceState state);
FuRedfishNetworkDevice *
fu_redfish_network_device_new(const gchar *object_path);
gboolean
fu_redfish_network_device_get_state(FuRedfishNetworkDevice *self,
				    FuRedfishNetworkDeviceState *state,
				    GError **error);
gchar *
fu_redfish_network_device_get_address(FuRedfishNetworkDevice *self, GError **error);
gboolean
fu_redfish_network_device_connect(FuRedfishNetworkDevice *self, GError **error);
