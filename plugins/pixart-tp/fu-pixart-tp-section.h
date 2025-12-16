/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "config.h"

#include <fwupdplugin.h>

#include "fu-pixart-tp-struct.h"

#define FU_TYPE_PIXART_TP_SECTION (fu_pixart_tp_section_get_type())
G_DECLARE_FINAL_TYPE(FuPixartTpSection, fu_pixart_tp_section, FU, PIXART_TP_SECTION, FuFirmware)

FuPixartTpSection *
fu_pixart_tp_section_new(void) G_GNUC_WARN_UNUSED_RESULT;

FuPixartTpUpdateType
fu_pixart_tp_section_get_update_type(FuPixartTpSection *self) G_GNUC_NON_NULL(1);
gboolean
fu_pixart_tp_section_has_flag(FuPixartTpSection *self, FuPixartTpFirmwareFlags flag)
    G_GNUC_NON_NULL(1);
guint32
fu_pixart_tp_section_get_target_flash_start(FuPixartTpSection *self) G_GNUC_NON_NULL(1);
guint32
fu_pixart_tp_section_get_crc(FuPixartTpSection *self) G_GNUC_NON_NULL(1);
GByteArray *
fu_pixart_tp_section_get_reserved(FuPixartTpSection *self) G_GNUC_NON_NULL(1);
