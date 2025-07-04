/*
 * Copyright 2020 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright 2021 Ivan Mikhanchuk <ivan.mikhanchuk@quectel.com>
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-mm-device.h"

#define FU_TYPE_MM_FIREHOSE_DEVICE (fu_mm_firehose_device_get_type())
G_DECLARE_FINAL_TYPE(FuMmFirehoseDevice, fu_mm_firehose_device, FU, MM_FIREHOSE_DEVICE, FuMmDevice)
