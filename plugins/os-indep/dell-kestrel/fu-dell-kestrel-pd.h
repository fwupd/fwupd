/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_DELL_KESTREL_PD (fu_dell_kestrel_pd_get_type())
G_DECLARE_FINAL_TYPE(FuDellKestrelPd, fu_dell_kestrel_pd, FU, DELL_KESTREL_PD, FuDevice)

FuDellKestrelPd *
fu_dell_kestrel_pd_new(FuDevice *parent,
		       FuDellKestrelEcDevSubtype pd_subtype,
		       FuDellKestrelEcDevInstance pd_instance);
