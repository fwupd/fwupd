/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021 Denis Pynkin <denis.pynkin@collabora.com>
 * Copyright (C) 2022 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

typedef enum {
	FU_STEELSERIES_DEVICE_UNKNOWN = 0,
	FU_STEELSERIES_DEVICE_GAMEPAD,
	FU_STEELSERIES_DEVICE_GAMEPAD_DONGLE,
	FU_STEELSERIES_DEVICE_SONIC,
	FU_STEELSERIES_DEVICE_FIZZ,
	FU_STEELSERIES_DEVICE_FIZZ_DONGLE,
} FuSteelseriesDeviceKind;

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

FuSteelseriesDeviceKind
fu_steelseries_device_get_kind(FuSteelseriesDevice *self);
void
fu_steelseries_device_set_kind(FuSteelseriesDevice *self, FuSteelseriesDeviceKind kind);
void
fu_steelseries_device_set_iface_idx_offset(FuSteelseriesDevice *self, gint iface_idx_offset);
gsize
fu_steelseries_device_get_transfer_size(FuSteelseriesDevice *self);
gboolean
fu_steelseries_device_cmd(FuSteelseriesDevice *self, guint8 *data, gboolean answer, GError **error);
