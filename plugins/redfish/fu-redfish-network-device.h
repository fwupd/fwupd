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
	FU_REDFISH_NETWORK_DEVICE_STATE_UNKNOWN,
	FU_REDFISH_NETWORK_DEVICE_STATE_DISCONNECTED = 30,
	FU_REDFISH_NETWORK_DEVICE_STATE_CONNECTED = 100,
} FuRedfishNetworkDeviceState;

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
