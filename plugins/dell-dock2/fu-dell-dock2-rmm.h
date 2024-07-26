/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_DELL_DOCK2_RMM (fu_dell_dock2_rmm_get_type())
G_DECLARE_FINAL_TYPE(FuDellDock2Rmm, fu_dell_dock2_rmm, FU, DELL_DOCK2_RMM, FuHidDevice)

FuDellDock2Rmm *
fu_dell_dock2_rmm_new(FuUsbDevice *device, FuDellDock2BaseType dock_type);
void
fu_dell_dock2_rmm_setup_version_raw(FuDevice *device, const guint32 version_raw);
