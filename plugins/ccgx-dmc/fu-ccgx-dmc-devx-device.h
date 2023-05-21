/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-ccgx-dmc-struct.h"

#define FU_TYPE_CCGX_DMC_DEVX_DEVICE (fu_ccgx_dmc_devx_device_get_type())
G_DECLARE_FINAL_TYPE(FuCcgxDmcDevxDevice,
		     fu_ccgx_dmc_devx_device,
		     FU,
		     CCGX_DMC_DEVX_DEVICE,
		     FuDevice)

FuCcgxDmcDevxDevice *
fu_ccgx_dmc_devx_device_new(FuDevice *proxy,
			    const guint8 *buf,
			    gsize bufsz,
			    gsize offset,
			    GError **error);
guint
fu_ccgx_dmc_devx_device_get_remove_delay(FuCcgxDmcDevxDevice *self);
const guint8 *
fu_ccgx_dmc_devx_device_get_fw_version(FuCcgxDmcDevxDevice *self);
FuCcgxDmcDevxDeviceType
fu_ccgx_dmc_devx_device_get_device_type(FuCcgxDmcDevxDevice *self);
