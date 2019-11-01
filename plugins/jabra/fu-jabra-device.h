/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_JABRA_DEVICE (fu_jabra_device_get_type ())
G_DECLARE_FINAL_TYPE (FuJabraDevice, fu_jabra_device, FU, JABRA_DEVICE, FuUsbDevice)
