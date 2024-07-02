/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-dfu-common.h"
#include "fu-dfu-struct.h"
#include "fu-dfu-target.h"

#define FU_TYPE_DFU_DEVICE (fu_dfu_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuDfuDevice, fu_dfu_device, FU, DFU_DEVICE, FuUsbDevice)

struct _FuDfuDeviceClass {
	FuUsbDeviceClass parent_class;
};

const gchar *
fu_dfu_device_get_chip_id(FuDfuDevice *self);
gboolean
fu_dfu_device_reset(FuDfuDevice *self, FuProgress *progress, GError **error);
gboolean
fu_dfu_device_refresh(FuDfuDevice *self, guint timeout_ms, GError **error);
gboolean
fu_dfu_device_abort(FuDfuDevice *self, GError **error);

guint8
fu_dfu_device_get_interface(FuDfuDevice *self);
FuDfuState
fu_dfu_device_get_state(FuDfuDevice *self);
FuDfuStatus
fu_dfu_device_get_status(FuDfuDevice *self);
guint16
fu_dfu_device_get_transfer_size(FuDfuDevice *self);
guint16
fu_dfu_device_get_version(FuDfuDevice *self);
guint
fu_dfu_device_get_timeout(FuDfuDevice *self);

void
fu_dfu_device_error_fixup(FuDfuDevice *self, GError **error);
guint
fu_dfu_device_get_download_timeout(FuDfuDevice *self);
gboolean
fu_dfu_device_ensure_interface(FuDfuDevice *self, GError **error);
