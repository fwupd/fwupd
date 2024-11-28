/*
 * Copyright 2024 Dell Technologies
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-dell-kestrel-ec-struct.h"

#define FU_TYPE_DELL_KESTREL_HID_DEVICE (fu_dell_kestrel_hid_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuDellKestrelHidDevice,
			 fu_dell_kestrel_hid_device,
			 FU,
			 DELL_KESTREL_HID_DEVICE,
			 FuHidDevice)

struct _FuDellKestrelHidDeviceClass {
	FuHidDeviceClass parent_class;
};

gboolean
fu_dell_kestrel_hid_device_i2c_write(FuDellKestrelHidDevice *self,
				     GByteArray *cmd_buf,
				     GError **error);
gboolean
fu_dell_kestrel_hid_device_i2c_read(FuDellKestrelHidDevice *self,
				    FuDellKestrelEcHidCmd cmd,
				    GByteArray *res,
				    guint delayms,
				    GError **error);
gboolean
fu_dell_kestrel_hid_device_write_firmware(FuDellKestrelHidDevice *self,
					  FuFirmware *firmware,
					  FuProgress *progress,
					  FuDellKestrelEcDevType dev_type,
					  guint8 dev_identifier,
					  GError **error);
