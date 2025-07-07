/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-wacom-common.h"
#include "fu-wacom-raw-struct.h"

#define FU_TYPE_WACOM_DEVICE (fu_wacom_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuWacomDevice, fu_wacom_device, FU, WACOM_DEVICE, FuHidrawDevice)

struct _FuWacomDeviceClass {
	FuHidrawDeviceClass parent_class;
	gboolean (*write_firmware)(FuDevice *self,
				   FuChunkArray *chunks,
				   FuProgress *progress,
				   GError **error);
};

#define FU_WACOM_RAW_DEVICE_FLAG_REQUIRES_WAIT_FOR_REPLUG "requires-wait-for-replug"

guint8
fu_wacom_device_get_echo_next(FuWacomDevice *self);
gboolean
fu_wacom_device_set_feature(FuWacomDevice *self, const guint8 *data, guint datasz, GError **error);
gboolean
fu_wacom_device_get_feature(FuWacomDevice *self, guint8 *data, guint datasz, GError **error);
gboolean
fu_wacom_device_cmd(FuWacomDevice *self,
		    const FuStructWacomRawRequest *st_req,
		    guint8 *rsp_value,
		    guint delay_ms,
		    FuWacomDeviceCmdFlags flags,
		    GError **error);
gboolean
fu_wacom_device_erase_all(FuWacomDevice *self, GError **error);
gboolean
fu_wacom_device_check_mpu(FuWacomDevice *self, GError **error);
gsize
fu_wacom_device_get_block_sz(FuWacomDevice *self);
