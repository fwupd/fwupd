/*
 * Copyright 2021 Ricardo Cañuelo <ricardo.canuelo@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-device.h"
#include "fu-io-channel.h"

G_BEGIN_DECLS

#define FU_TYPE_BLUETOOTH_DEVICE (fu_bluetooth_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuBluetoothDevice, fu_bluetooth_device, FU, BLUETOOTH_DEVICE, FuDevice)

struct _FuBluetoothDeviceClass {
	FuDeviceClass parent_class;
};

/* Device Information service attributes */
#define FU_BLUETOOTH_DEVICE_UUID_DI_SYSTEM_ID	      "00002a23-0000-1000-8000-00805f9b34fb"
#define FU_BLUETOOTH_DEVICE_UUID_DI_MODEL_NUMBER      "00002a24-0000-1000-8000-00805f9b34fb"
#define FU_BLUETOOTH_DEVICE_UUID_DI_SERIAL_NUMBER     "00002a25-0000-1000-8000-00805f9b34fb"
#define FU_BLUETOOTH_DEVICE_UUID_DI_FIRMWARE_REVISION "00002a26-0000-1000-8000-00805f9b34fb"
#define FU_BLUETOOTH_DEVICE_UUID_DI_HARDWARE_REVISION "00002a27-0000-1000-8000-00805f9b34fb"
#define FU_BLUETOOTH_DEVICE_UUID_DI_SOFTWARE_REVISION "00002a28-0000-1000-8000-00805f9b34fb"
#define FU_BLUETOOTH_DEVICE_UUID_DI_MANUFACTURER_NAME "00002a29-0000-1000-8000-00805f9b34fb"
#define FU_BLUETOOTH_DEVICE_UUID_DI_PNP_UID	      "00002a50-0000-1000-8000-00805f9b34fb"

void
fu_bluetooth_device_set_modalias(FuBluetoothDevice *self, const gchar *modalias)
    G_GNUC_NON_NULL(1, 2);
void
fu_bluetooth_device_emit_changed(FuBluetoothDevice *self, const gchar *uuid) G_GNUC_NON_NULL(1, 2);

GByteArray *
fu_bluetooth_device_read(FuBluetoothDevice *self, const gchar *uuid, GError **error)
    G_GNUC_NON_NULL(1, 2);
gchar *
fu_bluetooth_device_read_string(FuBluetoothDevice *self, const gchar *uuid, GError **error)
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_bluetooth_device_write(FuBluetoothDevice *self,
			  const gchar *uuid,
			  GByteArray *buf,
			  GError **error) G_GNUC_NON_NULL(1, 2, 3);
gboolean
fu_bluetooth_device_notify_start(FuBluetoothDevice *self, const gchar *uuid, GError **error)
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_bluetooth_device_notify_stop(FuBluetoothDevice *self, const gchar *uuid, GError **error)
    G_GNUC_NON_NULL(1, 2);
FuIOChannel *
fu_bluetooth_device_notify_acquire(FuBluetoothDevice *self,
				   const gchar *uuid,
				   gint32 *mtu,
				   GError **error) G_GNUC_NON_NULL(1, 2);
FuIOChannel *
fu_bluetooth_device_write_acquire(FuBluetoothDevice *self,
				  const gchar *uuid,
				  gint32 *mtu,
				  GError **error) G_GNUC_NON_NULL(1, 2);

G_END_DECLS
