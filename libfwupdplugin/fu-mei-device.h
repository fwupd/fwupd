/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-udev-device.h"

#define FU_TYPE_MEI_DEVICE (fu_mei_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuMeiDevice, fu_mei_device, FU, MEI_DEVICE, FuUdevDevice)

struct _FuMeiDeviceClass {
	FuUdevDeviceClass parent_class;
};

gboolean
fu_mei_device_connect(FuMeiDevice *self,
		      const gchar *uuid,
		      guint8 req_protocol_version,
		      GError **error) G_GNUC_NON_NULL(1, 2);
gboolean
fu_mei_device_read(FuMeiDevice *self,
		   guint8 *buf,
		   gsize bufsz,
		   gsize *bytes_read,
		   guint timeout_ms,
		   GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_mei_device_write(FuMeiDevice *self,
		    const guint8 *buf,
		    gsize bufsz,
		    guint timeout_ms,
		    GError **error) G_GNUC_NON_NULL(1);
guint
fu_mei_device_get_max_msg_length(FuMeiDevice *self) G_GNUC_NON_NULL(1);
guint8
fu_mei_device_get_protocol_version(FuMeiDevice *self) G_GNUC_NON_NULL(1);
gchar *
fu_mei_device_get_fw_ver(FuMeiDevice *self, guint idx, GError **error) G_GNUC_NON_NULL(1);
gchar *
fu_mei_device_get_fw_status(FuMeiDevice *self, guint idx, GError **error) G_GNUC_NON_NULL(1);
