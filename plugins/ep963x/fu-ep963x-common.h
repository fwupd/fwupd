/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib-object.h>

#define FU_EP963_FIRMWARE_SIZE 0x1f000

#define FU_EP963_TRANSFER_BLOCK_SIZE 0x200 /* 512 */
#define FU_EP963_TRANSFER_CHUNK_SIZE 0x04
#define FU_EP963_FEATURE_ID1_SIZE    0x08

#define FU_EP963_USB_CONTROL_ID 0x01
