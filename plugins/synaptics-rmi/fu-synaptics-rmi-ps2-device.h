/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 * Copyright (c) 2020 Synaptics Incorporated.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-synaptics-rmi-device.h"

#define FU_TYPE_SYNAPTICS_RMI_PS2_DEVICE (fu_synaptics_rmi_ps2_device_get_type ())
G_DECLARE_FINAL_TYPE (FuSynapticsRmiPs2Device, fu_synaptics_rmi_ps2_device, FU, SYNAPTICS_RMI_PS2_DEVICE, FuSynapticsRmiDevice)

