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

#include "fu-dell-dock-ec-v2-struct.h"
#include "fu-dell-dock-struct.h"

#define FU_TYPE_DELL_DOCK_EC_V2 (fu_dell_dock_ec_v2_get_type())
G_DECLARE_FINAL_TYPE(FuDellDockEcV2, fu_dell_dock_ec_v2, FU, DELL_DOCK_EC_V2, FuHidDevice)

FuDellDockEcV2 *
fu_dell_dock_ec_v2_new(FuDevice *proxy);
const gchar *
fu_dell_dock_ec_v2_get_data_module_type(FuDevice *device);
gboolean
fu_dell_dock_ec_v2_enable_tbt_passive(FuDevice *device);
gboolean
fu_dell_dock_ec_v2_modify_lock(FuDevice *device, guint8 target, gboolean unlocked, GError **error);
gboolean
fu_dell_dock_ec_v2_trigger_passive_flow(FuDevice *device, GError **error);
guint32
fu_dell_dock_ec_v2_get_pd_version(FuDevice *device, guint8 sub_type, guint8 instance);
guint32
fu_dell_dock_ec_v2_get_wtpd_version(FuDevice *device);
guint32
fu_dell_dock_ec_v2_get_dpmux_version(FuDevice *device);
guint32
fu_dell_dock_ec_v2_get_package_version(FuDevice *device);
gboolean
fu_dell_dock_ec_v2_commit_package(FuDevice *device, GBytes *blob_fw, GError **error);
DockBaseType
fu_dell_dock_ec_v2_get_dock_type(FuDevice *device);
guint8
fu_dell_dock_ec_v2_get_dock_sku(FuDevice *device);
const gchar *
fu_dell_dock_ec_v2_devicetype_to_str(guint8 device_type, guint8 sub_type, guint8 instance);
struct FuDellDockEcV2QueryEntry *
fu_dell_dock_ec_v2_dev_entry(FuDevice *device,
			     guint8 device_type,
			     guint8 sub_type,
			     guint8 instance);
