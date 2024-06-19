/*
 * Copyright 2024 Dell Inc.
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
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_DELL_DOCK_PD (fu_dell_dock_pd_get_type())
G_DECLARE_FINAL_TYPE(FuDellDockPd, fu_dell_dock_pd, FU, DELL_DOCK_PD, FuDevice)

FuDellDockPd *
fu_dell_dock_pd_new(FuDevice *parent, guint8 pd_subtype, guint8 pd_instance);
