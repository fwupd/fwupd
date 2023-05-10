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
#define GOODIX_DEVICE_IOCTL_TIMEOUT 5000

enum ic_type {
	IC_TYPE_NONE,
	IC_TYPE_PHOENIX,
	IC_TYPE_TYPE_NANJING,
	IC_TYPE_MOUSEPAD,
	IC_TYPE_NORMANDYL,
	IC_TYPE_BERLINB,
	IC_TYPE_YELLOWSTONE,
};

struct transfer_data_t {
	guint32 addr;
	guint8 *buf;
	guint32 len;
};

struct goodix_version_t {
	guint8 patch_pid[9];
	guint8 patch_vid[4];
	guint32 sensor_id;
	guint8 cfg_ver;
	guint32 cfg_id;
	gint version;
};

struct goodix_hw_ops_t {
	gboolean (*get_version)(FuDevice *device, gpointer user_data, GError **error);
	gboolean (*update_prepare)(FuDevice *device, GError **error);
	gboolean (*update_process)(FuDevice *device,
				   guint32 flash_addr,
				   guint8 *buf,
				   guint32 len,
				   GError **error);
	gboolean (*update_finish)(FuDevice *device, GError **error);
};

gboolean
get_report(FuDevice *device, guint8 *buf, GError **error);
gboolean
set_report(FuDevice *device, guint8 *buf, guint32 len, GError **error);

extern struct goodix_hw_ops_t brlb_hw_ops;
extern struct goodix_hw_ops_t gtx8_hw_ops;
