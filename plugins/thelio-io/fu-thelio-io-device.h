/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 * Copyright 2019 Jeremy Soller <jeremy@system76.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_THELIO_IO_DEVICE (fu_thelio_io_device_get_type())
G_DECLARE_FINAL_TYPE(FuThelioIoDevice, fu_thelio_io_device, FU, THELIO_IO_DEVICE, FuUsbDevice)
