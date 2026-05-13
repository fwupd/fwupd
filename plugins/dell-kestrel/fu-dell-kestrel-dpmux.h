/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_DELL_KESTREL_DPMUX (fu_dell_kestrel_dpmux_get_type())
G_DECLARE_FINAL_TYPE(FuDellKestrelDpmux, fu_dell_kestrel_dpmux, FU, DELL_KESTREL_DPMUX, FuDevice)

FuDellKestrelDpmux *
fu_dell_kestrel_dpmux_new(FuDevice *proxy);
