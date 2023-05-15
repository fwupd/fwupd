/*
 * Copyright (C) 2023 Goodix.inc <xulinkun@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define PACKAGE_LEN    65
#define REPORT_ID      0x0E
#define I2C_DIRECT_RW  0x20
#define I2C_READ_FLAG  1
#define I2C_WRITE_FLAG 0

#define RAM_BUFFER_SIZE		    4096
#define CFG_MAX_SIZE		    4096

struct FuGoodixTransferData {
	guint32 addr;
	guint8 *buf;
	guint32 len;
};

struct FuGoodixVersion {
	guint8 patch_pid[9];
	guint8 patch_vid[4];
	guint8 sensor_id;
	guint8 cfg_ver;
	guint32 cfg_id;
	guint32 ver_num;
};
