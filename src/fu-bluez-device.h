/*
 * Copyright 2021 Ricardo Cañuelo <ricardo.canuelo@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-bluetooth-proxy.h"
#include "fu-device.h"
#include "fu-io-channel.h"

G_BEGIN_DECLS

#define FU_TYPE_BLUEZ_DEVICE (fu_bluez_device_get_type())
G_DECLARE_FINAL_TYPE(FuBluezDevice, fu_bluez_device, FU, BLUEZ_DEVICE, FuBluetoothProxy)

G_END_DECLS
