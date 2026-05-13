/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-rts54hub-struct.h"

#define FU_TYPE_RTS54HUB_DEVICE (fu_rts54hub_device_get_type())

G_DECLARE_FINAL_TYPE(FuRts54hubDevice, fu_rts54hub_device, FU, RTS54HUB_DEVICE, FuUsbDevice)

gboolean
fu_rts54hub_device_vendor_cmd(FuRts54hubDevice *self, FuRts54hubVendorCmd value, GError **error);
gboolean
fu_rts54hub_device_i2c_config(FuRts54hubDevice *self,
			      guint8 target_addr,
			      guint8 sub_length,
			      FuRts54hubI2cSpeed speed,
			      GError **error);
gboolean
fu_rts54hub_device_i2c_write(FuRts54hubDevice *self,
			     guint32 sub_addr,
			     const guint8 *data,
			     gsize datasz,
			     GError **error);
gboolean
fu_rts54hub_device_i2c_read(FuRts54hubDevice *self,
			    guint32 sub_addr,
			    guint8 *data,
			    gsize datasz,
			    GError **error);
