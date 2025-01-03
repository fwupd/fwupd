/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include "fu-dell-kestrel-common.h"
#include "fu-dell-kestrel-ec-struct.h"
#include "fu-dell-kestrel-hid-device.h"

#define FU_TYPE_DELL_KESTREL_EC (fu_dell_kestrel_ec_get_type())
G_DECLARE_FINAL_TYPE(FuDellKestrelEc,
		     fu_dell_kestrel_ec,
		     FU,
		     DELL_KESTREL_EC,
		     FuDellKestrelHidDevice)

FuDellKestrelEc *
fu_dell_kestrel_ec_new(FuDevice *device, gboolean uod);
void
fu_dell_kestrel_ec_enable_tbt_passive(FuDellKestrelEc *self);
gboolean
fu_dell_kestrel_ec_own_dock(FuDellKestrelEc *self, gboolean lock, GError **error);
gboolean
fu_dell_kestrel_ec_run_passive_update(FuDellKestrelEc *self, GError **error);
guint32
fu_dell_kestrel_ec_get_pd_version(FuDellKestrelEc *self,
				  FuDellKestrelEcDevSubtype subtype,
				  FuDellKestrelEcDevInstance instance);
guint32
fu_dell_kestrel_ec_get_ilan_version(FuDellKestrelEc *self);
guint32
fu_dell_kestrel_ec_get_wtpd_version(FuDellKestrelEc *self);
guint32
fu_dell_kestrel_ec_get_rmm_version(FuDellKestrelEc *self);
guint32
fu_dell_kestrel_ec_get_dpmux_version(FuDellKestrelEc *self);
guint32
fu_dell_kestrel_ec_get_package_version(FuDellKestrelEc *self);
gboolean
fu_dell_kestrel_ec_commit_package(FuDellKestrelEc *self, GInputStream *stream, GError **error);
FuDellDockBaseType
fu_dell_kestrel_ec_get_dock_type(FuDellKestrelEc *self);
FuDellKestrelDockSku
fu_dell_kestrel_ec_get_dock_sku(FuDellKestrelEc *self);
const gchar *
fu_dell_kestrel_ec_devicetype_to_str(FuDellKestrelEcDevType dev_type,
				     FuDellKestrelEcDevSubtype subtype,
				     FuDellKestrelEcDevInstance instance);
gboolean
fu_dell_kestrel_ec_is_dock_ready4update(FuDevice *device, GError **error);
gboolean
fu_dell_kestrel_ec_is_dev_present(FuDellKestrelEc *self,
				  FuDellKestrelEcDevType dev_type,
				  FuDellKestrelEcDevSubtype subtype,
				  FuDellKestrelEcDevInstance instance);
