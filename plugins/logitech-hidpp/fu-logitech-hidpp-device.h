/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LOGITECH_HIDPP_DEVICE (fu_logitech_hidpp_device_get_type())
G_DECLARE_FINAL_TYPE(FuLogitechHidppDevice,
		     fu_logitech_hidpp_device,
		     FU,
		     LOGITECH_HIDPP_DEVICE,
		     FuDevice)

#define FU_LOGITECH_HIDPP_DEVICE_FLAG_FORCE_RECEIVER_ID	  "force-receiver-id"
#define FU_LOGITECH_HIDPP_DEVICE_FLAG_BLE		  "ble"
#define FU_LOGITECH_HIDPP_DEVICE_FLAG_REBIND_ATTACH	  "rebind-attach"
#define FU_LOGITECH_HIDPP_DEVICE_FLAG_NO_REQUEST_REQUIRED "no-request-required"

void
fu_logitech_hidpp_device_set_device_idx(FuLogitechHidppDevice *self, guint8 device_idx)
    G_GNUC_NON_NULL(1);
guint16
fu_logitech_hidpp_device_get_hidpp_pid(FuLogitechHidppDevice *self) G_GNUC_NON_NULL(1);
void
fu_logitech_hidpp_device_set_hidpp_pid(FuLogitechHidppDevice *self, guint16 hidpp_pid)
    G_GNUC_NON_NULL(1);
FuLogitechHidppDevice *
fu_logitech_hidpp_device_new(FuUdevDevice *proxy) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
