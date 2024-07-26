/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_DELL_K2_ILAN (fu_dell_k2_ilan_get_type())
G_DECLARE_FINAL_TYPE(FuDellK2Ilan, fu_dell_k2_ilan, FU, DELL_K2_ILAN, FuDevice)

FuDellK2Ilan *
fu_dell_k2_ilan_new(FuDevice *proxy);
