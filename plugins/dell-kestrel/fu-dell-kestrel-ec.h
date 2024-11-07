/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-dell-kestrel-common.h"

#define FU_TYPE_DELL_KESTREL_EC (fu_dell_kestrel_ec_get_type())
G_DECLARE_FINAL_TYPE(FuDellKestrelEc, fu_dell_kestrel_ec, FU, DELL_KESTREL_EC, FuHidDevice)

FuDellKestrelEc *
fu_dell_kestrel_ec_new(FuDevice *device, gboolean uod);
void
fu_dell_kestrel_ec_enable_tbt_passive(FuDevice *device);
gboolean
fu_dell_kestrel_ec_own_dock(FuDevice *device, gboolean lock, GError **error);
gboolean
fu_dell_kestrel_ec_run_passive_update(FuDevice *device, GError **error);
guint32
fu_dell_kestrel_ec_get_pd_version(FuDevice *device,
				  FuDellKestrelEcDevSubtype subtype,
				  FuDellKestrelEcDevInstance instance);
guint32
fu_dell_kestrel_ec_get_ilan_version(FuDevice *device);
guint32
fu_dell_kestrel_ec_get_wtpd_version(FuDevice *device);
guint32
fu_dell_kestrel_ec_get_rmm_version(FuDevice *device);
guint32
fu_dell_kestrel_ec_get_dpmux_version(FuDevice *device);
guint32
fu_dell_kestrel_ec_get_package_version(FuDevice *device);
gboolean
fu_dell_kestrel_ec_commit_package(FuDevice *device, GBytes *blob_fw, GError **error);
FuDellDockBaseType
fu_dell_kestrel_ec_get_dock_type(FuDevice *device);
FuDellKestrelDockSku
fu_dell_kestrel_ec_get_dock_sku(FuDevice *device);
const gchar *
fu_dell_kestrel_ec_devicetype_to_str(FuDellKestrelEcDevType dev_type,
				     FuDellKestrelEcDevSubtype subtype,
				     FuDellKestrelEcDevInstance instance);
gboolean
fu_dell_kestrel_ec_is_dock_ready4update(FuDevice *device, GError **error);
gboolean
fu_dell_kestrel_ec_is_dev_present(FuDevice *device,
				  FuDellKestrelEcDevType dev_type,
				  FuDellKestrelEcDevSubtype subtype,
				  FuDellKestrelEcDevInstance instance);
gboolean
fu_dell_kestrel_ec_write_firmware_helper(FuDevice *device,
					 FuFirmware *firmware,
					 FuProgress *progress,
					 FuDellKestrelEcDevType dev_type,
					 guint8 dev_identifier,
					 GError **error);
