/*
 * Copyright 2025 lazro <2059899519@qq.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>


#define FU_LEGION_GO2_DEVICE_VID       0x17EF
#define FU_LEGION_GO2_DEVICE_PID_BEGIN 0x61EB
#define FU_LEGION_GO2_DEVICE_PID_END   0x61EE

#define FU_LEGION_GO2_DEVICE_IO_TIMEOUT       500
#define FU_LEGION_GO2_DEVICE_REBOOT_WAIT_TIME (10*1000)

#define FU_LEGION_GO2_DEVICE_FW_SIGNED_LENGTH 384
#define FU_LEGION_GO2_DEVICE_FW_ID_LENGTH     4
#define FU_LEGION_GO2_DEVICE_FW_PACKET_LENGTH 32
#define FU_LEGION_GO2_DEVICE_FW_REPORT_LENGTH 64

struct FuStructLegionGo2UpgradeRetryParam
{
	GByteArray* res;
	guint8 main_id;
	guint8 sub_id;
	guint8 dev_id;
	guint8 step;
};

struct FuStructLegionGo2NormalRetryParam
{
	GByteArray* res;
	guint8 main_id;
	guint8 sub_id;
	guint8 dev_id;
};

#define FU_TYPE_LEGION_GO2_DEVICE (fu_legion_go2_device_get_type())
G_DECLARE_FINAL_TYPE(FuLegionGo2Device,
		     fu_legion_go2_device,
		     FU,
		     LEGION_GO2_DEVICE,
		     FuHidrawDevice)
