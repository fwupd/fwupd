/*
 * Copyright 2019 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-mm-device.h"

#define FU_TYPE_MM_QMI_DEVICE (fu_mm_qmi_device_get_type())
G_DECLARE_FINAL_TYPE(FuMmQmiDevice, fu_mm_qmi_device, FU, MM_QMI_DEVICE, FuMmDevice)
