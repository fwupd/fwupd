/*
 * Copyright (C) 2017 VIA Corporation
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

typedef enum {
	FU_VLI_USBHUB_I2C_STATUS_OK		= 0x00,
	FU_VLI_USBHUB_I2C_STATUS_HEADER		= 0x51,
	FU_VLI_USBHUB_I2C_STATUS_COMMAND	= 0x52,
	FU_VLI_USBHUB_I2C_STATUS_ADDRESS	= 0x53,
	FU_VLI_USBHUB_I2C_STATUS_PACKETSIZE	= 0x54,
	FU_VLI_USBHUB_I2C_STATUS_CHECKSUM	= 0x55,
} FuVliUsbhubI2cStatus;

gboolean	 fu_vli_usbhub_i2c_check_status		(FuVliUsbhubI2cStatus	 status,
							 GError			**error);
