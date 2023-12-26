/*
 * Copyright (C) 2023 Ling.Chen <ling.chen@algoltek.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#define ALGOLTEK_DEVICE_USB_TIMEOUT 3000 /* ms */
/* command types in USB messages from PC to device */
#define ALGOLTEK_RDR 0x06
#define ALGOLTEK_WRR 0x07
#define ALGOLTEK_RDV 0x08
#define ALGOLTEK_EN 0x09
#define ALGOLTEK_WRF 0x10
#define ALGOLTEK_ISP 0x13
#define ALGOLTEK_ERS 0x19
#define ALGOLTEK_BOT 0x1D
#define ALGOLTEK_RST 0x20

#define AG_BOOT_ISP_ADDR 0x2000
#define AG_UPDATE_STATUS_REG  0x860C
#define AG_UPDATE_PASS  1
#define AG_UPDATE_FAIL 2
