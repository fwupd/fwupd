/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 * Copyright 2012 Synaptics Incorporated.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-synaptics-rmi-device.h"

#define FU_TYPE_SYNAPTICS_RMI_HID_DEVICE (fu_synaptics_rmi_hid_device_get_type())
G_DECLARE_FINAL_TYPE(FuSynapticsRmiHidDevice,
		     fu_synaptics_rmi_hid_device,
		     FU,
		     SYNAPTICS_RMI_HID_DEVICE,
		     FuSynapticsRmiDevice)
