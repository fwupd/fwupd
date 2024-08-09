/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_DELL_DOCK2_PACKAGE (fu_dell_dock2_package_get_type())
G_DECLARE_FINAL_TYPE(FuDellDock2Package, fu_dell_dock2_package, FU, DELL_DOCK2_PACKAGE, FuDevice)

FuDellDock2Package *
fu_dell_dock2_package_new(FuDevice *proxy);
