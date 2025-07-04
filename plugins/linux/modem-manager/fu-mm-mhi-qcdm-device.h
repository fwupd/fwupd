/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-mm-qcdm-device.h"

#define FU_TYPE_MM_MHI_QCDM_DEVICE (fu_mm_mhi_qcdm_device_get_type())
G_DECLARE_FINAL_TYPE(FuMmMhiQcdmDevice,
		     fu_mm_mhi_qcdm_device,
		     FU,
		     MM_MHI_QCDM_DEVICE,
		     FuMmQcdmDevice)
