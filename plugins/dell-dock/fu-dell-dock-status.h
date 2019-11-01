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

#include "fu-device.h"

#define FU_TYPE_DELL_DOCK_STATUS (fu_dell_dock_status_get_type ())
G_DECLARE_FINAL_TYPE (FuDellDockStatus, fu_dell_dock_status, FU, DELL_DOCK_STATUS, FuDevice)

FuDellDockStatus	*fu_dell_dock_status_new	(void);
