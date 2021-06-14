/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_RTS54HUB_DEVICE (fu_rts54hub_device_get_type ())

typedef enum {
	FU_RTS54HUB_I2C_SPEED_100K,
	FU_RTS54HUB_I2C_SPEED_200K,
	FU_RTS54HUB_I2C_SPEED_300K,
	FU_RTS54HUB_I2C_SPEED_400K,
	FU_RTS54HUB_I2C_SPEED_500K,
	FU_RTS54HUB_I2C_SPEED_600K,
	FU_RTS54HUB_I2C_SPEED_700K,
	FU_RTS54HUB_I2C_SPEED_800K,
	FU_RTS54HUB_I2C_SPEED_LAST
} FuRts54HubI2cSpeed;

G_DECLARE_FINAL_TYPE (FuRts54HubDevice, fu_rts54hub_device, FU, RTS54HUB_DEVICE, FuUsbDevice)

gboolean	fu_rts54hub_device_vendor_cmd	(FuRts54HubDevice	*self,
						 guint8			 value,
						 GError			**error);
gboolean	fu_rts54hub_device_i2c_config	(FuRts54HubDevice	*self,
						 guint8			 target_addr,
						 guint8			 sub_length,
						 FuRts54HubI2cSpeed	 speed,
						 GError			**error);
gboolean	fu_rts54hub_device_i2c_write	(FuRts54HubDevice	*self,
						 guint32		 sub_addr,
						 const guint8		*data,
						 gsize			 datasz,
						 GError			**error);
gboolean	fu_rts54hub_device_i2c_read	(FuRts54HubDevice	*self,
						 guint32		 sub_addr,
						 guint8			*data,
						 gsize			 datasz,
						 GError			**error);
