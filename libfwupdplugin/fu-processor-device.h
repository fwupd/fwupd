/*
 * Copyright 2020 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-device.h"
#include "fu-processor-struct.h"

#define FU_TYPE_PROCESSOR_DEVICE (fu_processor_device_get_type())
G_DECLARE_FINAL_TYPE(FuProcessorDevice, fu_processor_device, FU, PROCESSOR_DEVICE, FuDevice)

FuProcessorDevice *
fu_processor_device_new(FuContext *ctx);

FuProcessorKind
fu_processor_device_get_kind(FuProcessorDevice *self) G_GNUC_NON_NULL(1);
gboolean
fu_processor_device_needs_mitigation(FuProcessorDevice *self,
				     FuProcessorMitigationFlags mitigation_flag) G_GNUC_NON_NULL(1);
guint32
fu_processor_device_get_sinkclose_microcode_ver(FuProcessorDevice *self) G_GNUC_NON_NULL(1);
