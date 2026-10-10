/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-vbe-device.h"

#define FU_TYPE_VBE_ABREC_DEVICE (fu_vbe_abrec_device_get_type())
G_DECLARE_FINAL_TYPE(FuVbeAbrecDevice, fu_vbe_abrec_device, FU, VBE_ABREC_DEVICE, FuVbeDevice)
