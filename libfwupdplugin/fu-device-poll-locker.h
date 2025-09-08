/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-device-locker.h"
#include "fu-device.h"

FuDeviceLocker *
fu_device_poll_locker_new(FuDevice *self, GError **error) G_GNUC_NON_NULL(1);
