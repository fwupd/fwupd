/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_DELL_DOCK2_ILAN (fu_dell_dock2_ilan_get_type())
G_DECLARE_FINAL_TYPE(FuDellDock2Ilan, fu_dell_dock2_ilan, FU, DELL_DOCK2_ILAN, FuDevice)

FuDellDock2Ilan *
fu_dell_dock2_ilan_new(FuDevice *proxy);
