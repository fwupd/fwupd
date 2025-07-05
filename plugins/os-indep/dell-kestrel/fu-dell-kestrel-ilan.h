/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_DELL_KESTREL_ILAN (fu_dell_kestrel_ilan_get_type())
G_DECLARE_FINAL_TYPE(FuDellKestrelIlan, fu_dell_kestrel_ilan, FU, DELL_KESTREL_ILAN, FuDevice)

FuDellKestrelIlan *
fu_dell_kestrel_ilan_new(FuDevice *proxy);
