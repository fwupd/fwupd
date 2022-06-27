/*
 * Copyright (C) 2018 Dell Inc.
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
 * SPDX-License-Identifier: LGPL-2.1+ OR MIT
 */

#pragma once

#include "config.h"

#include <fwupdplugin.h>

#define FU_TYPE_DELL_DOCK_EC (fu_dell_dock_ec_get_type())
G_DECLARE_FINAL_TYPE(FuDellDockEc, fu_dell_dock_ec, FU, DELL_DOCK_EC, FuDevice)

FuDellDockEc *
fu_dell_dock_ec_new(FuDevice *proxy);

const gchar *
fu_dell_dock_ec_get_module_type(FuDevice *device);
gboolean
fu_dell_dock_ec_needs_tbt(FuDevice *device);
gboolean
fu_dell_dock_ec_tbt_passive(FuDevice *device);
gboolean
fu_dell_dock_ec_modify_lock(FuDevice *self, guint8 target, gboolean unlocked, GError **error);

gboolean
fu_dell_dock_ec_reboot_dock(FuDevice *device, GError **error);

const gchar *
fu_dell_dock_ec_get_mst_version(FuDevice *device);
const gchar *
fu_dell_dock_ec_get_tbt_version(FuDevice *device);
guint32
fu_dell_dock_ec_get_status_version(FuDevice *device);
gboolean
fu_dell_dock_ec_commit_package(FuDevice *device, GBytes *blob_fw, GError **error);
gboolean
fu_dell_dock_module_is_usb4(FuDevice *device);
guint8
fu_dell_dock_get_dock_type(FuDevice *device);
