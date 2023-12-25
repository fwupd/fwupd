/*
 * Copyright (C) 2023 Ling.Chen <ling.chen@algoltek.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#define ALGOLTEK_DEVICE_USB_TIMEOUT 3000 /* ms */
/* command types in USB messages from PC to device */
#define ALGOLTEK_USB_REQUEST_CMD01 0x08
#define ALGOLTEK_USB_REQUEST_CMD02 0x06
#define ALGOLTEK_USB_REQUEST_CMD03 0x09
#define ALGOLTEK_USB_REQUEST_CMD04 0x20
#define ALGOLTEK_USB_REQUEST_CMD05 0x07
#define ALGOLTEK_USB_REQUEST_CMD06 0x13
#define ALGOLTEK_USB_REQUEST_CMD07 0x1D
#define ALGOLTEK_USB_REQUEST_CMD08 0x19
#define ALGOLTEK_USB_REQUEST_CMD09 0x10

#define Result_PASS  1
#define Result_ERROR 2
