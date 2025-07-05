/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_INTEL_CVS_FIRMWARE (fu_intel_cvs_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuIntelCvsFirmware, fu_intel_cvs_firmware, FU, INTEL_CVS_FIRMWARE, FuFirmware)

FuFirmware *
fu_intel_cvs_firmware_new(void);
guint16
fu_intel_cvs_firmware_get_vid(FuIntelCvsFirmware *self);
guint16
fu_intel_cvs_firmware_get_pid(FuIntelCvsFirmware *self);
