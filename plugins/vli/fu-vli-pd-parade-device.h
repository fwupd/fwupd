/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-vli-pd-common.h"

#define FU_TYPE_VLI_PD_PARADE_DEVICE (fu_vli_pd_parade_device_get_type())
G_DECLARE_FINAL_TYPE(FuVliPdParadeDevice,
		     fu_vli_pd_parade_device,
		     FU,
		     VLI_PD_PARADE_DEVICE,
		     FuDevice)

FuDevice *
fu_vli_pd_parade_device_new(FuVliDevice *parent);
