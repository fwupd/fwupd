/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-wacom-raw-struct.h"

#define FU_WACOM_RAW_CMD_RETRIES 1000

#define FU_WACOM_RAW_BL_START_ADDR  (0x11FF8)
#define FU_WACOM_RAW_BL_BYTES_CHECK 8

#define FU_WACOM_RAW_BL_TYPE_FINALIZER 0x00

gboolean
fu_wacom_raw_common_rc_set_error(const FuStructWacomRawResponse *st_rsp, GError **error);
gboolean
fu_wacom_raw_common_check_reply(const FuStructWacomRawRequest *st_req,
				const FuStructWacomRawResponse *st_rsp,
				GError **error);
gboolean
fu_wacom_raw_common_block_is_empty(const guint8 *data, guint16 datasz);
