/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_CSR_DEVICE (fu_csr_device_get_type ())
G_DECLARE_FINAL_TYPE (FuCsrDevice, fu_csr_device, FU, CSR_DEVICE, FuUsbDevice)
