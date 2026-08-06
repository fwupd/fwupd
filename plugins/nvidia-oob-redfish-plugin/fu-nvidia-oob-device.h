/*
 * Copyright 2026 NVIDIA Corporation
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Galaxy GB300 -- LVFS OOB firmware update plugin;
 * device class for components managed via the Redfish host interface
 *
 * each FuNvidiaOobDevice represents a firmware-updatable component
 * enumerated from the BMC's Redfish UpdateService/FirmwareInventory:
 *   - BMC firmware
 *   - SBIOS
 *   - PLDM bundle (GPU, CX9, FPGA, MCU)
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-nvidia-oob-redfish-client.h"

#define FU_TYPE_NVIDIA_OOB_DEVICE (fu_nvidia_oob_device_get_type())
G_DECLARE_FINAL_TYPE(FuNvidiaOobDevice, fu_nvidia_oob_device, FU, NVIDIA_OOB_DEVICE, FuDevice)

FuNvidiaOobDevice *
fu_nvidia_oob_device_new(FuContext *ctx,
			 FuNvidiaOobRedfishClient *client,
			 const gchar *inventory_uri,
			 FwupdJsonObject *inventory_obj);

/* accessors used by the plugin during update */
const gchar *
fu_nvidia_oob_device_get_inventory_uri(FuNvidiaOobDevice *self);
FuNvidiaOobRedfishClient *
fu_nvidia_oob_device_get_client(FuNvidiaOobDevice *self);
