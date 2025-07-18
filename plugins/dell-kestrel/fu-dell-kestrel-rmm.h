/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include "fu-dell-kestrel-hid-device.h"

/* Device IDs: USB RMM */
#define DELL_KESTREL_USB_RMM_PID 0xB0A4

#define FU_TYPE_DELL_KESTREL_RMM (fu_dell_kestrel_rmm_get_type())
G_DECLARE_FINAL_TYPE(FuDellKestrelRmm,
		     fu_dell_kestrel_rmm,
		     FU,
		     DELL_KESTREL_RMM,
		     FuDellKestrelHidDevice)

FuDellKestrelRmm *
fu_dell_kestrel_rmm_new(FuUsbDevice *device);
gboolean
fu_dell_kestrel_rmm_fix_version(FuDellKestrelRmm *self, GError **error);
