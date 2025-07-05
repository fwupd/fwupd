/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_INTEL_CVS_DEVICE (fu_intel_cvs_device_get_type())
G_DECLARE_FINAL_TYPE(FuIntelCvsDevice, fu_intel_cvs_device, FU, INTEL_CVS_DEVICE, FuI2cDevice)
