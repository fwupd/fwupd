/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-xy-kbms-struct.h"

#define FU_XY_KBMS_PINMAP_SENTINEL_OFFSET 0x3B3 /* from the start of the pinmap */
#define FU_XY_KBMS_PINMAP_VERSION	  0x04

guint32
fu_xy_kbms_config_crc(const guint8 *buf, gsize bufsz);
gboolean
fu_xy_kbms_config_update_crc(GByteArray *buf, GError **error) G_GNUC_WARN_UNUSED_RESULT;
const gchar *
fu_xy_kbms_board_type_to_name(FuXyKbmsBoardType board_type);
gboolean
fu_xy_kbms_config_validate(GByteArray *buf, GError **error);
