/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-dell-kestrel-dpmux.h"
#include "fu-dell-kestrel-ec-struct.h"
#include "fu-dell-kestrel-ec.h"
#include "fu-dell-kestrel-ilan.h"
#include "fu-dell-kestrel-package.h"
#include "fu-dell-kestrel-pd.h"
#include "fu-dell-kestrel-rmm.h"
#include "fu-dell-kestrel-rtshub.h"
#include "fu-dell-kestrel-wtpd.h"

/* device IDs: Main HID on USB Hub */
#define DELL_VID	     0x413C
#define DELL_KESTREL_HID_PID 0xB06E

/* device IDs: mst */
#define MST_VMM89_USB_VID 0x413C
#define MST_VMM89_USB_PID 0xB0A5

/* device IDs: tbt */
#define DELL_KESTREL_T5_DEVID "TBT-00d4b0a2"
#define DELL_KESTREL_T4_DEVID "TBT-00d4b0a1"

/* max retries */
#define DELL_KESTREL_MAX_RETRIES 5
