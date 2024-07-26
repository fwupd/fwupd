/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-dell-k2-dpmux.h"
#include "fu-dell-k2-ec-hid.h"
#include "fu-dell-k2-ec-struct.h"
#include "fu-dell-k2-ec.h"
#include "fu-dell-k2-ilan.h"
#include "fu-dell-k2-package.h"
#include "fu-dell-k2-pd.h"
#include "fu-dell-k2-rmm.h"
#include "fu-dell-k2-rtshub.h"
#include "fu-dell-k2-struct.h"
#include "fu-dell-k2-wtpd.h"

/* device IDs: Main HID on USB Hub */
#define DELL_VID	0x413C
#define DELL_K2_HID_PID 0xB06E

/* device IDs: mst */
#define MST_VMM89430_USB_VID 0x413C
#define MST_VMM89430_USB_PID 0xB0A5

/* device IDs: tbt */
#define DELL_K2_TBT5 "TBT-00d4b0a2"
#define DELL_K2_TBT4 "TBT-00d4b0a1"
