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

#define FU_TYPE_DELL_DOCK_EC (fu_dell_dock_ec_get_type())
G_DECLARE_FINAL_TYPE(FuDellDockEc, fu_dell_dock_ec, FU, DELL_DOCK_EC, FuDevice)

FuDellDockEc *
fu_dell_dock_ec_new(FuDevice *proxy);

const gchar *
fu_dell_dock_ec_get_module_type(FuDellDockEc *self);
gboolean
fu_dell_dock_ec_needs_tbt(FuDellDockEc *self);
gboolean
fu_dell_dock_ec_tbt_passive(FuDellDockEc *self);
gboolean
fu_dell_dock_ec_modify_lock(FuDellDockEc *self, guint8 target, gboolean unlocked, GError **error);

gboolean
fu_dell_dock_ec_reboot_dock(FuDellDockEc *self, GError **error);

const gchar *
fu_dell_dock_ec_get_mst_version(FuDellDockEc *self);
const gchar *
fu_dell_dock_ec_get_tbt_version(FuDellDockEc *self);
guint32
fu_dell_dock_ec_get_status_version(FuDellDockEc *self);
gboolean
fu_dell_dock_ec_commit_package(FuDellDockEc *self, GBytes *blob_fw, GError **error);
gboolean
fu_dell_dock_ec_module_is_usb4(FuDellDockEc *self);
guint8
fu_dell_dock_ec_get_dock_type(FuDellDockEc *self);
