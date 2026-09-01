/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-dfu-firmware.h"
#include "fu-input-stream.h"

G_BEGIN_DECLS

guint8
fu_dfu_firmware_get_footer_len(FuDfuFirmware *self) G_GNUC_NON_NULL(1) G_GNUC_PURE;
GByteArray *
fu_dfu_firmware_append_footer(FuDfuFirmware *self, GBytes *contents, GError **error)
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_dfu_firmware_parse_footer(FuDfuFirmware *self,
			     FuInputStream *stream,
			     FuFirmwareParseFlags flags,
			     GError **error) G_GNUC_NON_NULL(1, 2);

G_END_DECLS
