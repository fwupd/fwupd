/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_DELL_K2_PACKAGE (fu_dell_k2_package_get_type())
G_DECLARE_FINAL_TYPE(FuDellK2Package, fu_dell_k2_package, FU, DELL_K2_PACKAGE, FuDevice)

FuDellK2Package *
fu_dell_k2_package_new(FuDevice *proxy);
