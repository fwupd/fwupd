/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_RTS54HID_DEVICE (fu_rts54hid_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuRts54hidDevice, fu_rts54hid_device, FU, RTS54HID_DEVICE, FuHidDevice)

struct _FuRts54hidDeviceClass {
	FuHidDeviceClass parent_class;
};

#define FU_RTS54HID_DEVICE_TIMEOUT 1000 /* ms */

#define FU_RTS54HID_DEVICE_FLAG_FW_AUTH "fw-auth"

gboolean
fu_rts54hid_device_get_report(FuRts54hidDevice *self, GByteArray *buf, GError **error);
gboolean
fu_rts54hid_device_set_report(FuRts54hidDevice *self, GByteArray *buf, GError **error);
gboolean
fu_rts54hid_device_write_chunk(FuRts54hidDevice *self, FuChunk *chk, GError **error);
