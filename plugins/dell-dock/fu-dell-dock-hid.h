/*
 * Copyright (C) 2018 Realtek Semiconductor Corporation
 * Copyright (C) 2018 Dell Inc.
 * All rights reserved.
 *
 * This software and associated documentation (if any) is furnished
 * under a license and may only be used or copied in accordance
 * with the terms of the license.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * Dell Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1+ OR MIT
 */

#pragma once

#include "config.h"

#include <glib-object.h>

#include "fu-device.h"

typedef struct __attribute__ ((packed)) {
	guint8 i2cslaveaddr;
	guint8 regaddrlen;
	guint8 i2cspeed;
} FuHIDI2CParameters;

typedef enum {
	I2C_SPEED_250K,
	I2C_SPEED_400K,
	I2C_SPEED_800K,
	/* <private >*/
	I2C_SPEED_LAST,
} BridgedI2CSpeed;

#define 	HIDI2C_MAX_READ				192
#define 	HIDI2C_MAX_WRITE			128

gboolean	fu_dell_dock_hid_i2c_write		(FuDevice *self,
							 const guint8 *input,
							 gsize write_size,
							 const FuHIDI2CParameters *parameters,
							 GError **error);
gboolean	fu_dell_dock_hid_i2c_read		(FuDevice *self,
							 guint32 cmd,
							 gsize read_size,
							 GBytes **bytes,
							 const FuHIDI2CParameters *parameters,
							 GError **error);

gboolean	fu_dell_dock_hid_get_hub_version	(FuDevice *self,
							 GError **error);

gboolean	fu_dell_dock_hid_raise_mcu_clock	(FuDevice *self,
							 gboolean enable,
							 GError **error);

gboolean	fu_dell_dock_hid_get_ec_status		(FuDevice *self,
							 guint8 *status1,
							 guint8 *status2,
							 GError **error);

gboolean	fu_dell_dock_hid_erase_bank		(FuDevice *self,
							 guint8 idx,
							 GError **error);

gboolean	fu_dell_dock_hid_write_flash		(FuDevice *self,
							 guint32 addr,
							 const guint8 *input,
							 gsize write_size,
							 GError **error);

gboolean	fu_dell_dock_hid_verify_update		(FuDevice *self,
							 gboolean *result,
							 GError **error);

gboolean	fu_dell_dock_hid_tbt_wake		(FuDevice *self,
							 const FuHIDI2CParameters *parameters,
							 GError **error);

gboolean	fu_dell_dock_hid_tbt_write		(FuDevice *self,
							 guint32 start_addr,
							 const guint8 *input,
							 gsize write_size,
							 const FuHIDI2CParameters *parameters,
							 GError **error);

gboolean	fu_dell_dock_hid_tbt_authenticate	(FuDevice *self,
							 const FuHIDI2CParameters *parameters,
							 GError **error);
