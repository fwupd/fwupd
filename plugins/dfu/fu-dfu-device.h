/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include <gio/gio.h>
#include <glib-object.h>
#include <gusb.h>

#include "fu-dfu-common.h"
#include "fu-dfu-target.h"

#define FU_TYPE_DFU_DEVICE (fu_dfu_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuDfuDevice, fu_dfu_device, FU, DFU_DEVICE, FuUsbDevice)

struct _FuDfuDeviceClass {
	FuUsbDeviceClass parent_class;
};

FuDfuDevice *
fu_dfu_device_new(FuContext *ctx, GUsbDevice *usb_device);
FuDfuTarget *
fu_dfu_device_get_target_by_alt_setting(FuDfuDevice *self, guint8 alt_setting, GError **error);
FuDfuTarget *
fu_dfu_device_get_target_by_alt_name(FuDfuDevice *self, const gchar *alt_name, GError **error);
const gchar *
fu_dfu_device_get_chip_id(FuDfuDevice *self);
void
fu_dfu_device_set_chip_id(FuDfuDevice *self, const gchar *chip_id);
guint16
fu_dfu_device_get_runtime_vid(FuDfuDevice *self);
guint16
fu_dfu_device_get_runtime_pid(FuDfuDevice *self);
guint16
fu_dfu_device_get_runtime_release(FuDfuDevice *self);
gboolean
fu_dfu_device_reset(FuDfuDevice *self, FuProgress *progress, GError **error);
FuFirmware *
fu_dfu_device_upload(FuDfuDevice *self,
		     FuProgress *progress,
		     FuDfuTargetTransferFlags flags,
		     GError **error);
gboolean
fu_dfu_device_refresh(FuDfuDevice *self, GError **error);
gboolean
fu_dfu_device_refresh_and_clear(FuDfuDevice *self, GError **error);
gboolean
fu_dfu_device_abort(FuDfuDevice *self, GError **error);
gboolean
fu_dfu_device_clear_status(FuDfuDevice *self, GError **error);

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
fu_dfu_device_set_transfer_size(FuDfuDevice *self, guint16 transfer_size);
void
fu_dfu_device_set_timeout(FuDfuDevice *self, guint timeout_ms);
void
fu_dfu_device_error_fixup(FuDfuDevice *self, GError **error);
guint
fu_dfu_device_get_download_timeout(FuDfuDevice *self);
gboolean
fu_dfu_device_ensure_interface(FuDfuDevice *self, GError **error);
