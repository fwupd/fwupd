/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-udev-device.h"

#define FU_TYPE_MEI_DEVICE (fu_mei_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuMeiDevice, fu_mei_device, FU, MEI_DEVICE, FuUdevDevice)

struct _FuMeiDeviceClass {
	FuUdevDeviceClass parent_class;
	gpointer __reserved[31];
};

gboolean
fu_mei_device_connect(FuMeiDevice *self,
		      const gchar *guid,
		      guchar req_protocol_version,
		      GError **error);
gboolean
fu_mei_device_read(FuMeiDevice *self,
		   guint8 *buf,
		   gsize bufsz,
		   gsize *bytes_read,
		   guint timeout_ms,
		   GError **error);
gboolean
fu_mei_device_write(FuMeiDevice *self,
		    const guint8 *buf,
		    gsize bufsz,
		    guint timeout_ms,
		    GError **error);
guint
fu_mei_device_get_max_msg_length(FuMeiDevice *self);
guint8
fu_mei_device_get_protocol_version(FuMeiDevice *self);
