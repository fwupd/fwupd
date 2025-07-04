/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_INTEL_MKHI_DEVICE (fu_intel_mkhi_device_get_type())
G_DECLARE_FINAL_TYPE(FuIntelMkhiDevice, fu_intel_mkhi_device, FU, INTEL_MKHI_DEVICE, FuHeciDevice)
