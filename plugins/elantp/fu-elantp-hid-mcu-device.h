/*
 * Copyright 2026 Jingle Wu <jingle.wu@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ELANTP_HID_MCU_DEVICE (fu_elantp_hid_mcu_device_get_type())
G_DECLARE_FINAL_TYPE(FuElantpHidMcuDevice,
		     fu_elantp_hid_mcu_device,
		     FU,
		     ELANTP_HID_MCU_DEVICE,
		     FuDevice)

FuElantpHidMcuDevice *
fu_elantp_hid_mcu_device_new(FuDevice *proxy);
