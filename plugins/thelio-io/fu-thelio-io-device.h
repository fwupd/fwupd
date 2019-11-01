/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2019 Jeremy Soller <jeremy@system76.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_THELIO_IO_DEVICE (fu_thelio_io_device_get_type ())
G_DECLARE_FINAL_TYPE (FuThelioIoDevice, fu_thelio_io_device, FU, THELIO_IO_DEVICE, FuUsbDevice)
