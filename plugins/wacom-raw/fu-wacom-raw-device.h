/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-wacom-raw-common.h"
#include "fu-wacom-raw-struct.h"

#define FU_TYPE_WACOM_RAW_DEVICE (fu_wacom_raw_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuWacomRawDevice,
			 fu_wacom_raw_device,
			 FU,
			 WACOM_RAW_DEVICE,
			 FuHidrawDevice)

struct _FuWacomRawDeviceClass {
	FuHidrawDeviceClass parent_class;
	gboolean (*write_firmware)(FuDevice *self,
				   FuChunkArray *chunks,
				   FuProgress *progress,
				   GError **error);
};

#define FU_WACOM_RAW_DEVICE_FLAG_REQUIRES_WAIT_FOR_REPLUG "requires-wait-for-replug"

guint8
fu_wacom_raw_device_get_echo_next(FuWacomRawDevice *self);
gboolean
fu_wacom_raw_device_set_feature(FuWacomRawDevice *self,
				const guint8 *data,
				guint datasz,
				GError **error);
gboolean
fu_wacom_raw_device_get_feature(FuWacomRawDevice *self, guint8 *data, guint datasz, GError **error);
gboolean
fu_wacom_raw_device_cmd(FuWacomRawDevice *self,
			const FuStructWacomRawRequest *st_req,
			guint8 *rsp_value,
			guint delay_ms,
			FuWacomRawDeviceCmdFlags flags,
			GError **error);
gboolean
fu_wacom_raw_device_erase_all(FuWacomRawDevice *self, GError **error);
gboolean
fu_wacom_raw_device_check_mpu(FuWacomRawDevice *self, GError **error);
gsize
fu_wacom_raw_device_get_block_sz(FuWacomRawDevice *self);
