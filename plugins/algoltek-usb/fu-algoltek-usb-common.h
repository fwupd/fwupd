/*
 * Copyright (C) 2024 Algoltek, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#define ALGOLTEK_DEVICE_USB_TIMEOUT 3000 /* ms */

#define AG_ISP_ADDR	 0x2000
#define AG_ISP_SIZE	 0x1000
#define AG_FIRMWARE_SIZE 0x20000

#define AG_UPDATE_STATUS 0x860C
#define AG_UPDATE_PASS	 1
#define AG_UPDATE_FAIL	 2

#define AG_IDENTIFICATION_128K_ADDR 31
#define AG_IDENTIFICATION_256K_ADDR 63
