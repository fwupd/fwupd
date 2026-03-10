/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LENOVO_LDC_FIRMWARE (fu_lenovo_ldc_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuLenovoLdcFirmware,
		     fu_lenovo_ldc_firmware,
		     FU,
		     LENOVO_LDC_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_lenovo_ldc_firmware_new(void);
guint16
fu_lenovo_ldc_firmware_get_pid(FuLenovoLdcFirmware *self);
