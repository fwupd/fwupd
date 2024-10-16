/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 * Copyright 2021 Denis Pynkin <denis.pynkin@collabora.com>
 * Copyright 2022 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_STEELSERIES_DEVICE (fu_steelseries_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuSteelseriesDevice,
			 fu_steelseries_device,
			 FU,
			 STEELSERIES_DEVICE,
			 FuUsbDevice)

struct _FuSteelseriesDeviceClass {
	FuUsbDeviceClass parent_class;
};

#define STEELSERIES_BUFFER_CONTROL_SIZE 64
#define STEELSERIES_TRANSACTION_TIMEOUT 5000

#define FU_STEELSERIES_DEVICE_FLAG_IS_RECEIVER "is-receiver"
/* device needs bootloader mode for flashing */
#define FU_STEELSERIES_DEVICE_FLAG_DETACH_BOOTLOADER "detach-bootloader"

void
fu_steelseries_device_set_iface_idx_offset(FuSteelseriesDevice *self, gint iface_idx_offset);
gboolean
fu_steelseries_device_cmd(FuSteelseriesDevice *self,
			  guint8 *data,
			  gsize datasz,
			  gboolean answer,
			  GError **error);
