/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_DELL_K2_DPMUX (fu_dell_k2_dpmux_get_type())
G_DECLARE_FINAL_TYPE(FuDellK2Dpmux, fu_dell_k2_dpmux, FU, DELL_K2_DPMUX, FuDevice)

FuDellK2Dpmux *
fu_dell_k2_dpmux_new(FuDevice *proxy);
