/*
 * Copyright (C) 2018 Dell Inc.
 * All rights reserved.
 *
 * This software and associated documentation (if any) is furnished
 * under a license and may only be used or copied in accordance
 * with the terms of the license.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * Dell Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1+ OR MIT
 */

#pragma once

#include "config.h"

#include <fwupdplugin.h>

#define FU_TYPE_DELL_DOCK_HUB (fu_dell_dock_hub_get_type())
G_DECLARE_FINAL_TYPE(FuDellDockHub, fu_dell_dock_hub, FU, DELL_DOCK_HUB, FuHidDevice)

/**
 * FU_DELL_DOCK_HUB_FLAG_HAS_BRIDGE:
 *
 * A bridge is present, possibly with extended devices.
 */
#define FU_DELL_DOCK_HUB_FLAG_HAS_BRIDGE (1 << 0)

FuDellDockHub *
fu_dell_dock_hub_new(FuUsbDevice *device);
void
fu_dell_dock_hub_add_instance(FuDevice *device, guint8 dock_type);
