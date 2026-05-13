/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-pixart-tp-section.h"
#include "fu-pixart-tp-struct.h"

#define FU_TYPE_PIXART_TP_FIRMWARE (fu_pixart_tp_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuPixartTpFirmware, fu_pixart_tp_firmware, FU, PIXART_TP_FIRMWARE, FuFirmware)

FuFirmware *
fu_pixart_tp_firmware_new(void);
FuPixartTpSection *
fu_pixart_tp_firmware_find_section_by_type(FuPixartTpFirmware *self,
					   FuPixartTpUpdateType update_type,
					   GError **error) G_GNUC_NON_NULL(1);
