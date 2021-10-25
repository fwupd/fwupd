/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

#include "fu-redfish-network-device.h"

#define NETWORK_MANAGER_SERVICE_NAME	     "org.freedesktop.NetworkManager"
#define NETWORK_MANAGER_INTERFACE	     "org.freedesktop.NetworkManager"
#define NETWORK_MANAGER_INTERFACE_IP4_CONFIG "org.freedesktop.NetworkManager.IP4Config"
#define NETWORK_MANAGER_INTERFACE_DEVICE     "org.freedesktop.NetworkManager.Device"
#define NETWORK_MANAGER_PATH		     "/org/freedesktop/NetworkManager"

FuRedfishNetworkDevice *
fu_redfish_network_device_for_mac_addr(const gchar *mac_addr, GError **error);
FuRedfishNetworkDevice *
fu_redfish_network_device_for_vid_pid(guint16 vid, guint16 pid, GError **error);
