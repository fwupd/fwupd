/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_CCGX_HPI_DEVICE (fu_ccgx_hpi_device_get_type ())
G_DECLARE_FINAL_TYPE (FuCcgxHpiDevice, fu_ccgx_hpi_device, FU, CCGX_HPI_DEVICE, FuUsbDevice)
