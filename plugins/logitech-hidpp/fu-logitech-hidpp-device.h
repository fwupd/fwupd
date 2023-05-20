/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
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

/**
 * FU_LOGITECH_HIDPP_DEVICE_FLAG_FORCE_RECEIVER_ID:
 *
 * Device is a unifying or Bolt receiver.
 *
 * Since: 1.7.0
 */
#define FU_LOGITECH_HIDPP_DEVICE_FLAG_FORCE_RECEIVER_ID (1 << 0)

/**
 * FU_LOGITECH_HIDPP_DEVICE_FLAG_BLE:
 *
 * Device is connected using Bluetooth Low Energy.
 *
 * Since: 1.7.0
 */
#define FU_LOGITECH_HIDPP_DEVICE_FLAG_BLE (1 << 1)

/**
 * FU_LOGITECH_HIDPP_DEVICE_FLAG_REBIND_ATTACH:
 *
 * The device file is automatically unbound and re-bound after the
 * device is attached.
 *
 * Since: 1.7.0
 */
#define FU_LOGITECH_HIDPP_DEVICE_FLAG_REBIND_ATTACH (1 << 2)

/**
 * FU_LOGITECH_HIDPP_DEVICE_FLAG_NO_REQUEST_REQUIRED:
 *
 * No user-action is required for detach and attach.
 *
 * Since: 1.7.0
 */
#define FU_LOGITECH_HIDPP_DEVICE_FLAG_NO_REQUEST_REQUIRED (1 << 3)

/**
 * FU_LOGITECH_HIDPP_DEVICE_FLAG_ADD_RADIO:
 *
 * The device should add a softdevice (index 0x5), typically a radio.
 *
 * Since: 1.7.0
 */
#define FU_LOGITECH_HIDPP_DEVICE_FLAG_ADD_RADIO (1 << 5)

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
