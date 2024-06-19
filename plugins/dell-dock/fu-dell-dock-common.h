/*
 * Copyright 2024 Dell Inc.
 * All rights reserved.
 *
 * This software and associated documentation (if any) is furnished
 * under a license and may only be used or copied in accordance
 * with the terms of the license.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * Dell Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-dell-dock-ec-v2.h"
#include "fu-dell-dock-hid-v2.h"
#include "fu-dell-dock-hid.h"
#include "fu-dell-dock-hub.h"
#include "fu-dell-dock-i2c-dpmux.h"
#include "fu-dell-dock-i2c-ec.h"
#include "fu-dell-dock-i2c-mst.h"
#include "fu-dell-dock-i2c-pd.h"
#include "fu-dell-dock-i2c-tbt.h"
#include "fu-dell-dock-i2c-wtpd.h"
#include "fu-dell-dock-status.h"
#include "fu-dell-dock-struct.h"

/* Device IDs: Main HID on USB Hub */
#define DELL_VID	  0x413C
#define DELL_DOCK_HID_PID 0xB06E

/* Device IDs: USB Hub */
#define DELL_DOCK_USB_RTS5413_PID    0xB06F
#define DELL_DOCK_USB_RTS5480_GEN1_PID 0xB0A1
#define DELL_DOCK_USB_RTS5480_GEN2_PID 0xB0A2
#define DELL_DOCK_USB_RTS5485_PID    0xB0A3
#define DELL_DOCK_USB_RMM_PID	       0xB0A4

/* device IDs: tbt */
#define GR_USB_VID 0x8087
#define GR_USB_PID 0x0B40

/* device IDs: mst */
#define MST_VMM8430_USB_VID 0x06CB
#define MST_VMM8430_USB_PID 0x8430
#define MST_VMM9430_USB_VID 0x06CB
#define MST_VMM9430_USB_PID 0x9430

/* Device IDs: TBT */
#define DELL_DOCK_TBT3    "TBT-00d4b070"
#define DELL_DOCK_TBT4    "TBT-00d4b071"
#define DELL_DOCK_TBT5    "TBT-00d4b072"
#define DELL_DOCK_TBT4_K2 "TBT-00d4b073"

typedef struct {
	DockBaseType dock_type;
	guint16 vid;
	guint16 pid;
	const gchar *instance_id;
} DellDockComponent;

gboolean
fu_dell_dock_set_power(FuDevice *device, guint8 target, gboolean enabled, GError **error);
const gchar *
fu_dell_dock_get_instance_id(DockBaseType type,
			     const DellDockComponent *dev_list,
			     guint16 vid,
			     guint16 pid);
