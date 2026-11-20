/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-realtek-rts54hub-struct.h"

#define FU_TYPE_REALTEK_RTS54HUB_DEVICE (fu_realtek_rts54hub_device_get_type())

G_DECLARE_FINAL_TYPE(FuRealtekRts54hubDevice,
		     fu_realtek_rts54hub_device,
		     FU,
		     REALTEK_RTS54HUB_DEVICE,
		     FuUsbDevice)

gboolean
fu_realtek_rts54hub_device_vendor_cmd(FuRealtekRts54hubDevice *self,
				      FuRealtekRts54hubVendorCmd vendor_cmd,
				      GError **error);
gboolean
fu_realtek_rts54hub_device_i2c_config(FuRealtekRts54hubDevice *self,
				      guint8 target_addr,
				      guint8 sub_length,
				      FuRealtekRts54hubI2cSpeed speed,
				      GError **error);
gboolean
fu_realtek_rts54hub_device_i2c_write(FuRealtekRts54hubDevice *self,
				     guint32 sub_addr,
				     const guint8 *data,
				     gsize datasz,
				     GError **error);
gboolean
fu_realtek_rts54hub_device_i2c_read(FuRealtekRts54hubDevice *self,
				    guint32 sub_addr,
				    guint8 *data,
				    gsize datasz,
				    GError **error);
