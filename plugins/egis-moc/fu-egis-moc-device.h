/*
 * Copyright 2025 Jason Huang <jason.huang@egistec.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_EGIS_MOC_DEVICE (fu_egis_moc_device_get_type())
G_DECLARE_FINAL_TYPE(FuEgisMocDevice, fu_egis_moc_device, FU, EGIS_MOC_DEVICE, FuUsbDevice)
