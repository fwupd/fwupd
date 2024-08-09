/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_DELL_DOCK2_WTPD (fu_dell_dock2_wtpd_get_type())
G_DECLARE_FINAL_TYPE(FuDellDock2Wtpd, fu_dell_dock2_wtpd, FU, DELL_DOCK2_WTPD, FuDevice)

FuDellDock2Wtpd *
fu_dell_dock2_wtpd_new(FuDevice *proxy);
