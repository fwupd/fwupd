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

#ifndef __FU_DELL_DOCK_I2C_MST_H
#define __FU_DELL_DOCK_I2C_MST_H

#include "config.h"

#include "fu-device.h"

G_BEGIN_DECLS

#define FU_TYPE_DELL_DOCK_MST (fu_dell_dock_mst_get_type ())
G_DECLARE_FINAL_TYPE (FuDellDockMst, fu_dell_dock_mst, FU, DELL_DOCK_MST, FuDevice)

FuDellDockMst 	*fu_dell_dock_mst_new	(void);

G_END_DECLS

#endif /* __FU_DELL_DOCK_I2C_MST_H */
