/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_DFU_CSR_DEVICE (fu_dfu_csr_device_get_type())
G_DECLARE_FINAL_TYPE(FuDfuCsrDevice, fu_dfu_csr_device, FU, DFU_CSR_DEVICE, FuHidDevice)
