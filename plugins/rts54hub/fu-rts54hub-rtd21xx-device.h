/*
 * Copyright (C) 2021 Ricky Wu <ricky_wu@realtek.com> <spring1527@gmail.com>
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_RTS54HUB_RTD21XX_DEVICE (fu_rts54hub_rtd21xx_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuRts54hubRtd21xxDevice,
			 fu_rts54hub_rtd21xx_device,
			 FU,
			 RTS54HUB_RTD21XX_DEVICE,
			 FuDevice)

struct _FuRts54hubRtd21xxDeviceClass {
	FuDeviceClass parent_class;
};

#define I2C_DELAY_AFTER_SEND 5000 /* us */

#define UC_ISP_TARGET_ADDR	      0x3A
#define UC_FOREGROUND_STATUS	      0x31
#define UC_FOREGROUND_OPCODE	      0x33
#define UC_FOREGROUND_ISP_DATA_OPCODE 0x34
#define UC_BACKGROUND_OPCODE	      0x31
#define UC_BACKGROUND_ISP_DATA_OPCODE 0x32

typedef enum {
	ISP_STATUS_BUSY = 0xBB,		/* host must wait for device */
	ISP_STATUS_IDLE_SUCCESS = 0x11, /* previous command was OK */
	ISP_STATUS_IDLE_FAILURE = 0x12, /* previous command failed */
} IspStatus;

gboolean
fu_rts54hub_rtd21xx_device_read_status(FuRts54hubRtd21xxDevice *self,
				       guint8 *status,
				       GError **error);
gboolean
fu_rts54hub_rtd21xx_device_read_status_raw(FuRts54hubRtd21xxDevice *self,
					   guint8 *status,
					   GError **error);
gboolean
fu_rts54hub_rtd21xx_device_i2c_read(FuRts54hubRtd21xxDevice *self,
				    guint8 target_addr,
				    guint8 sub_addr,
				    guint8 *data,
				    gsize datasz,
				    GError **error);
gboolean
fu_rts54hub_rtd21xx_device_i2c_write(FuRts54hubRtd21xxDevice *self,
				     guint8 target_addr,
				     guint8 sub_addr,
				     const guint8 *data,
				     gsize datasz,
				     GError **error);
