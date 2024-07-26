/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_DELL_K2_RMM (fu_dell_k2_rmm_get_type())
G_DECLARE_FINAL_TYPE(FuDellK2Rmm, fu_dell_k2_rmm, FU, DELL_K2_RMM, FuDevice)

FuDellK2Rmm *
fu_dell_k2_rmm_new(FuDevice *proxy);
