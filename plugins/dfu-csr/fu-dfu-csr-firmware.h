/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_DFU_CSR_FIRMWARE (fu_dfu_csr_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuDfuCsrFirmware, fu_dfu_csr_firmware, FU, DFU_CSR_FIRMWARE, FuFirmware)

FuFirmware *
fu_dfu_csr_firmware_new(void);
guint32
fu_dfu_csr_firmware_get_total_sz(FuDfuCsrFirmware *self);
