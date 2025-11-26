/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-device.h"

#define FU_TYPE_DEVICE_LOCKER (fu_device_locker_get_type())

G_DECLARE_FINAL_TYPE(FuDeviceLocker, fu_device_locker, FU, DEVICE_LOCKER, GObject)

/**
 * FuDeviceLockerFunc:
 *
 * Callback to use when opening and closing using [ctor@DeviceLocker.new_full].
 **/
typedef gboolean (*FuDeviceLockerFunc)(FuDevice *device, GError **error);

FuDeviceLocker *
fu_device_locker_new(FuDevice *device, GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
FuDeviceLocker *
fu_device_locker_new_full(FuDevice *device,
			  FuDeviceLockerFunc open_func,
			  FuDeviceLockerFunc close_func,
			  GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_device_locker_close(FuDeviceLocker *self, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
