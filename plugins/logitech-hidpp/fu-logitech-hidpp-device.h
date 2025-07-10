/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_HIDPP_DEVICE (fu_logitech_hidpp_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuLogitechHidppDevice,
			 fu_logitech_hidpp_device,
			 FU,
			 HIDPP_DEVICE,
			 FuUdevDevice)

struct _FuLogitechHidppDeviceClass {
	FuUdevDeviceClass parent_class;
	/* TODO: overridable methods */
};

extern GQuark FU_LOGITECH_HIDPP_DEVICE_FLAG_FORCE_RECEIVER_ID;
extern GQuark FU_LOGITECH_HIDPP_DEVICE_FLAG_BLE;
extern GQuark FU_LOGITECH_HIDPP_DEVICE_FLAG_REBIND_ATTACH;
extern GQuark FU_LOGITECH_HIDPP_DEVICE_FLAG_NO_REQUEST_REQUIRED;
extern GQuark FU_LOGITECH_HIDPP_DEVICE_FLAG_ADD_RADIO;

void
fu_logitech_hidpp_device_set_device_idx(FuLogitechHidppDevice *self, guint8 device_idx);
guint16
fu_logitech_hidpp_device_get_hidpp_pid(FuLogitechHidppDevice *self);
void
fu_logitech_hidpp_device_set_hidpp_pid(FuLogitechHidppDevice *self, guint16 hidpp_pid);
gboolean
fu_logitech_hidpp_device_attach(FuLogitechHidppDevice *self,
				guint8 entity,
				FuProgress *progress,
				GError **error);
FuLogitechHidppDevice *
fu_logitech_hidpp_device_new(FuUdevDevice *parent);
