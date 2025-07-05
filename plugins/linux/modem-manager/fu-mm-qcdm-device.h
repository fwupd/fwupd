/*
 * Copyright 2020 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright 2021 Ivan Mikhanchuk <ivan.mikhanchuk@quectel.com>
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-mm-device.h"

#define FU_TYPE_MM_QCDM_DEVICE (fu_mm_qcdm_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuMmQcdmDevice, fu_mm_qcdm_device, FU, MM_QCDM_DEVICE, FuMmDevice)

struct _FuMmQcdmDeviceClass {
	FuMmDeviceClass parent_class;
};
