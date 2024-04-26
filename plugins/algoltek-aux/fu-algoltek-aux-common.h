/*
 * Copyright 2024 Algoltek <Algoltek, Inc.>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#define ALGOLTEK_DEVICE_AUX_TIMEOUT 3000 /* ms */

#define AG_ISP_AUX_SIZE	 0x1000
#define AG_FIRMWARE_AUX_SIZE 0x40000

#define AG_UPDATE_AUX_STATUS 0x860C
#define AG_UPDATE_AUX_PASS	 1
#define AG_UPDATE_AUX_FAIL	 2

#define AG_AUX_CRC_INIT_POLINOM 0x1021
#define AG_AUX_CRC_POLINOM	0x1021
