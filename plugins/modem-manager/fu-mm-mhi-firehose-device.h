/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-mm-device.h"

#define FU_TYPE_MM_MHI_FIREHOSE_DEVICE (fu_mm_mhi_firehose_device_get_type())
G_DECLARE_FINAL_TYPE(FuMmMhiFirehoseDevice,
		     fu_mm_mhi_firehose_device,
		     FU,
		     MM_MHI_FIREHOSE_DEVICE,
		     FuMmDevice)
