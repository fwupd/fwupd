/*
 * Copyright 2026 NVIDIA Corporation
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * plugin private data for the singleton Redfish client and coldplug state
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-nvidia-oob-redfish-client.h"

#define NVIDIA_OOB_PLUGIN_VERSION "1.0.0"
#define NVIDIA_OOB_PLUGIN_NAME	  "nvidia-oob-redfish"

struct FuPluginData {
	FuNvidiaOobRedfishClient *client;
	gboolean curl_global_inited;
};

typedef struct FuPluginData FuNvidiaOobPlugin;

#define FU_NVIDIA_OOB_PLUGIN(o) ((FuNvidiaOobPlugin *)fu_plugin_get_data(FU_PLUGIN(o)))
