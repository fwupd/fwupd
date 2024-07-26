/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-dell-dock2-dpmux.h"
#include "fu-dell-dock2-ec-hid.h"
#include "fu-dell-dock2-ec-struct.h"
#include "fu-dell-dock2-ec.h"
#include "fu-dell-dock2-package.h"
#include "fu-dell-dock2-pd.h"
#include "fu-dell-dock2-rmm.h"
#include "fu-dell-dock2-rtshub.h"
#include "fu-dell-dock2-struct.h"
#include "fu-dell-dock2-wtpd.h"

/* Device IDs: Main HID on USB Hub */
#define DELL_VID	   0x413C
#define DELL_DOCK2_HID_PID 0xB06E

/* device IDs: mst */
#define MST_VMM8430_USB_VID 0x06CB
#define MST_VMM8430_USB_PID 0x8430

#define MST_VMM9430_USB_VID 0x06CB
#define MST_VMM9430_USB_PID 0x9430

/* Device IDs: TBT */
#define DELL_DOCK2_TBT5 "TBT-80871234"
#define DELL_DOCK2_TBT4 "TBT-00d4b0a1"

/* Device IDs: rmm */
#define DELL_DOCK2_RMM_USB_VID 0x2FE3
#define DELL_DOCK2_RMM_USB_PID 0x0100

gchar *
fu_hex_version_from_uint32(guint32 val, FwupdVersionFormat kind);
