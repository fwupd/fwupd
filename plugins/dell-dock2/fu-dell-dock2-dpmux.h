/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_DELL_DOCK2_DPMUX (fu_dell_dock2_dpmux_get_type())
G_DECLARE_FINAL_TYPE(FuDellDock2Dpmux, fu_dell_dock2_dpmux, FU, DELL_DOCK2_DPMUX, FuDevice)

FuDellDock2Dpmux *
fu_dell_dock2_dpmux_new(FuDevice *parent);
