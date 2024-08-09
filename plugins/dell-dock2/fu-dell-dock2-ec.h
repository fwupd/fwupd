/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-dell-dock2-common.h"
#include "fu-dell-dock2-struct.h"

#define FU_DELL_DOCK2_EC_INSTANCE_ID_K2 "USB\\VID_413C&PID_B06E&hub&k2_embedded"

#define FU_TYPE_DELL_DOCK2_EC (fu_dell_dock2_ec_get_type())
G_DECLARE_FINAL_TYPE(FuDellDock2Ec, fu_dell_dock2_ec, FU, DELL_DOCK2_EC, FuHidDevice)

FuDellDock2Ec *
fu_dell_dock2_ec_new(FuDevice *proxy);
void
fu_dell_dock2_ec_enable_tbt_passive(FuDevice *device);
gboolean
fu_dell_dock2_ec_modify_lock(FuDevice *device, gboolean lock, GError **error);
gboolean
fu_dell_dock2_ec_run_passive_update(FuDevice *device, GError **error);
guint32
fu_dell_dock2_ec_get_pd_version(FuDevice *device, guint8 sub_type, guint8 instance);
guint32
fu_dell_dock2_ec_get_ilan_version(FuDevice *device);
guint32
fu_dell_dock2_ec_get_wtpd_version(FuDevice *device);
guint32
fu_dell_dock2_ec_get_rmm_version(FuDevice *device);
guint32
fu_dell_dock2_ec_get_dpmux_version(FuDevice *device);
guint32
fu_dell_dock2_ec_get_package_version(FuDevice *device);
gboolean
fu_dell_dock2_ec_commit_package(FuDevice *device, GBytes *blob_fw, GError **error);
FuDellDock2BaseType
fu_dell_dock2_ec_get_dock_type(FuDevice *device);
guint8
fu_dell_dock2_ec_get_dock_sku(FuDevice *device);
const gchar *
fu_dell_dock2_ec_devicetype_to_str(guint8 device_type, guint8 sub_type, guint8 instance);
gboolean
fu_dell_dock2_ec_is_fwupdate_available_cmd(FuDevice *device, GError **error);
gboolean
fu_dell_dock2_ec_is_dock_ready4update(FuDevice *device, GError **error);
gboolean
fu_dell_dock2_ec_is_dev_present(FuDevice *device,
				guint8 dev_type,
				guint8 sub_type,
				guint8 instance);
