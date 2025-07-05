/*
 * Copyright 2024 HP Development Company, L.P.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_HPI_CFU_DEVICE (fu_hpi_cfu_device_get_type())
G_DECLARE_FINAL_TYPE(FuHpiCfuDevice, fu_hpi_cfu_device, FU, HPI_CFU_DEVICE, FuUsbDevice)
