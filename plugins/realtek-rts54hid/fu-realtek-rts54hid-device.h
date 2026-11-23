/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_REALTEK_RTS54HID_DEVICE (fu_realtek_rts54hid_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuRealtekRts54hidDevice,
			 fu_realtek_rts54hid_device,
			 FU,
			 REALTEK_RTS54HID_DEVICE,
			 FuHidDevice)

struct _FuRealtekRts54hidDeviceClass {
	FuHidDeviceClass parent_class;
};

#define FU_REALTEK_RTS54HID_DEVICE_TIMEOUT 1000 /* ms */

#define FU_REALTEK_RTS54HID_DEVICE_FLAG_FW_AUTH "fw-auth"

gboolean
fu_realtek_rts54hid_device_get_report(FuRealtekRts54hidDevice *self,
				      GByteArray *buf,
				      GError **error);
gboolean
fu_realtek_rts54hid_device_set_report(FuRealtekRts54hidDevice *self,
				      GByteArray *buf,
				      GError **error);
gboolean
fu_realtek_rts54hid_device_write_chunk(FuRealtekRts54hidDevice *self, FuChunk *chk, GError **error);
