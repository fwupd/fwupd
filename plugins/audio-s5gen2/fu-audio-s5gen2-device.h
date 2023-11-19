/*
 * Copyright (C) 2023 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_QC_S5GEN2_DEVICE (fu_qc_s5gen2_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuQcS5gen2Device, fu_qc_s5gen2_device, FU, QC_S5GEN2_DEVICE, FuDevice)

struct _FuQcS5gen2DeviceClass {
	FuDeviceClass parent_class;

	gboolean (*msg_in)(FuDevice *self, guint8 *data_in, gsize data_len, GError **error);
	gboolean (*msg_out)(FuDevice *self, guint8 *data, gsize data_len, GError **error);
	gboolean (*msg_cmd)(FuDevice *self, guint8 *data, gsize data_len, GError **error);
};

guint32
fu_qc_s5gen2_device_get_file_id(FuQcS5gen2Device *self);

void
fu_qc_s5gen2_device_set_file_id(FuQcS5gen2Device *self, guint32 id);

guint8
fu_qc_s5gen2_device_get_file_version(FuQcS5gen2Device *self);

void
fu_qc_s5gen2_device_set_file_version(FuQcS5gen2Device *self, guint8 version);

guint16
fu_qc_s5gen2_device_get_battery_raw(FuQcS5gen2Device *self);

FuDevice *
fu_qc_s5gen2_device_new(FuDevice *proxy);
