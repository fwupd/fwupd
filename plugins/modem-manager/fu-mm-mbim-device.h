/*
 * Copyright 2021 Jarvis Jiang <jarvis.w.jiang@gmail.com>
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <libmbim-glib.h>

#include "fu-mm-device.h"

#define FU_TYPE_MM_MBIM_DEVICE (fu_mm_mbim_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuMmMbimDevice, fu_mm_mbim_device, FU, MM_MBIM_DEVICE, FuMmDevice)

struct _FuMmMbimDeviceClass {
	FuMmDeviceClass parent_class;
};

gboolean
fu_mm_mbim_device_error_convert(GError **error);
MbimMessage *
fu_mm_mbim_device_command_sync(FuMmMbimDevice *self,
			       MbimMessage *mbim_message,
			       guint timeout_ms,
			       GError **error);
