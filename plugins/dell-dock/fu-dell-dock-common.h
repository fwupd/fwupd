/*
 * Copyright 2018 Dell Inc.
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

#include "fu-dell-dock-hid.h"
#include "fu-dell-dock-hub.h"
#include "fu-dell-dock-i2c-ec.h"
#include "fu-dell-dock-i2c-mst.h"
#include "fu-dell-dock-i2c-tbt.h"
#include "fu-dell-dock-status.h"
#include "fu-dell-dock-struct.h"

/* Device IDs: Main HID on USB Hub */
#define DELL_DOCK_VID	  0x413C
#define DELL_DOCK_HID_PID 0xB06E

/* Device IDs: USB Hub */
#define DELL_DOCK_USB_HUB_RTS5413_PID 0xB06F

/* Device IDs: TBT */
#define DELL_DOCK_TBT3 "TBT-00d4b070"
#define DELL_DOCK_TBT4 "TBT-00d4b071"

typedef struct {
	guint8 dock_type;
	guint16 vid;
	guint16 pid;
	const gchar *instance_id;
} DellDockComponent;

gboolean
fu_dell_dock_set_power(FuDevice *device, guint8 target, gboolean enabled, GError **error);
const gchar *
fu_dell_dock_get_instance_id(guint8 type, DellDockComponent *dev_list, guint16 vid, guint16 pid);
