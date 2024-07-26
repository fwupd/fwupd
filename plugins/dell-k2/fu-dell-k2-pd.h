/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_DELL_K2_PD (fu_dell_k2_pd_get_type())
G_DECLARE_FINAL_TYPE(FuDellK2Pd, fu_dell_k2_pd, FU, DELL_K2_PD, FuDevice)

FuDellK2Pd *
fu_dell_k2_pd_new(FuDevice *parent, guint8 pd_subtype, guint8 pd_instance);
