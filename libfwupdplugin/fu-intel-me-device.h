/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-device.h"
#include "fu-intel-me-struct.h"

#define FU_TYPE_INTEL_ME_DEVICE (fu_intel_me_device_get_type())
G_DECLARE_FINAL_TYPE(FuIntelMeDevice, fu_intel_me_device, FU, INTEL_ME_DEVICE, FuDevice)

FuIntelMeFamily
fu_intel_me_device_get_family(FuIntelMeDevice *self) G_GNUC_NON_NULL(1);
FuIntelMeIssue
fu_intel_me_device_get_issue(FuIntelMeDevice *self) G_GNUC_NON_NULL(1);

FuStructIntelMeHfsts *
fu_intel_me_device_get_hfsts(FuIntelMeDevice *self, guint idx) G_GNUC_NON_NULL(1);
void
fu_intel_me_device_set_hfsts(FuIntelMeDevice *self, guint idx, FuStructIntelMeHfsts *hfsts)
    G_GNUC_NON_NULL(1, 3);

FuIntelMeDevice *
fu_intel_me_device_new(FuContext *ctx) G_GNUC_NON_NULL(1);
