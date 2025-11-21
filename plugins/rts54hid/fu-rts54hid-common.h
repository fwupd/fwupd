/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 * Copyright 2018 Realtek Semiconductor Corporation
 * Copyright 2018 Dell Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

#define FU_RTS54HID_TRANSFER_BLOCK_SIZE 0x80
#define FU_RTS54FU_HID_REPORT_LENGTH	0xc0

/* [vendor-cmd:64] [data-payload:128] */
#define FU_RTS54HID_CMD_BUFFER_OFFSET_DATA 0x40
