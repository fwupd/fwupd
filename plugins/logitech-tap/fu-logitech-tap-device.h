/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LOGITECH_TAP_DEVICE (fu_logitech_tap_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuLogitechTapDevice, fu_logitech_tap_device, FU, LOGITECH_TAP_DEVICE, FuUdevDevice)

struct _FuLogitechTapDeviceClass {
	FuUdevDeviceClass parent_class;
	gboolean (*write_firmware)(FuDevice *self,
				   GPtrArray *chunks,
				   FuProgress *progress,
				   GError **error);
	gboolean (*reboot_device)(FuDevice *self,
				   FuProgress *progress,
				   GError **error);
};
