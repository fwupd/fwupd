/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_XY_KBMS_DEVICE (fu_xy_kbms_device_get_type())
G_DECLARE_FINAL_TYPE(FuXyKbmsDevice, fu_xy_kbms_device, FU, XY_KBMS_DEVICE, FuHidrawDevice)

GByteArray *
fu_xy_kbms_device_read_config(FuXyKbmsDevice *self,
			      FuProgress *progress,
			      GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
gboolean
fu_xy_kbms_device_write_config(FuXyKbmsDevice *self,
			       GByteArray *buf,
			       FuProgress *progress,
			       GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2, 3);
