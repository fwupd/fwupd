/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_HAILUCK_TP_DEVICE (fu_hailuck_tp_device_get_type())
G_DECLARE_FINAL_TYPE(FuHailuckTpDevice, fu_hailuck_tp_device, FU, HAILUCK_TP_DEVICE, FuDevice)

FuHailuckTpDevice *
fu_hailuck_tp_device_new(FuDevice *parent);
