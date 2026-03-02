/*
 * Copyright 2021 Andrii Dushko <andrii.dushko@developex.net>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-corsair-struct.h"

#define FU_TYPE_CORSAIR_DEVICE (fu_corsair_device_get_type())
G_DECLARE_FINAL_TYPE(FuCorsairDevice, fu_corsair_device, FU, CORSAIR_DEVICE, FuUsbDevice)

struct _FuCorsairDeviceClass {
	FuUsbDeviceClass parent_class;
};

#define FU_CORSAIR_DEVICE_FLAG_IS_RECEIVER		"is-receiver"
#define FU_CORSAIR_DEVICE_FLAG_LEGACY_ATTACH		"legacy-attach"

gboolean
fu_corsair_device_legacy_attach(FuCorsairDevice *self,
				FuCorsairDestination destination,
				GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_corsair_device_set_mode(FuCorsairDevice *self,
			   FuCorsairDestination destination,
			   FuCorsairDeviceMode mode,
			   GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_corsair_device_get_property(FuCorsairDevice *self,
			       FuCorsairDestination destination,
			       FuCorsairDeviceProperty property,
			       guint32 *value,
			       GError **error) G_GNUC_NON_NULL(1, 4);
gboolean
fu_corsair_device_write_firmware_full(FuCorsairDevice *self,
				      FuCorsairDestination destination,
				      FuFirmware *firmware,
				      FuProgress *progress,
				      GError **error) G_GNUC_NON_NULL(1, 3);
gboolean
fu_corsair_device_reconnect_subdevice(FuCorsairDevice *self, GError **error) G_GNUC_NON_NULL(1);
