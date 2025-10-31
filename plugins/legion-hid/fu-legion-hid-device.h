/*
 * Copyright 2025 lazro <2059899519@qq.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_LEGION_HID_DEVICE_IO_TIMEOUT	      500
#define FU_LEGION_HID_DEVICE_REBOOT_WAIT_TIME (10 * 1000)

#define FU_LEGION_HID_DEVICE_FW_SIGNED_LENGTH 384
#define FU_LEGION_HID_DEVICE_FW_ID_LENGTH     4
#define FU_LEGION_HID_DEVICE_FW_PACKET_LENGTH 32
#define FU_LEGION_HID_DEVICE_FW_REPORT_LENGTH 64

typedef struct {
	GByteArray *res;
	guint8 main_id;
	guint8 sub_id;
	guint8 dev_id;
	guint8 step;
} FuLegionHidUpgradeRetryHelper;

typedef struct {
	GByteArray *res;
	guint8 main_id;
	guint8 sub_id;
	guint8 dev_id;
} FuLegionHidNormalRetryHelper;

#define FU_TYPE_LEGION_HID_DEVICE (fu_legion_hid_device_get_type())
G_DECLARE_FINAL_TYPE(FuLegionHidDevice, fu_legion_hid_device, FU, LEGION_HID_DEVICE, FuHidrawDevice)
