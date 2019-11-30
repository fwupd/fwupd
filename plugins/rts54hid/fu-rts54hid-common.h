/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2018 Realtek Semiconductor Corporation
 * Copyright (C) 2018 Dell Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#define FU_RTS54HID_TRANSFER_BLOCK_SIZE			0x80
#define FU_RTS54FU_HID_REPORT_LENGTH			0xc0

/* [vendor-cmd:64] [data-payload:128] */
#define FU_RTS54HID_CMD_BUFFER_OFFSET_DATA		0x40

typedef struct __attribute__ ((packed)) {
	guint8 slave_addr;
	guint8 data_sz;
	guint8 speed;
} FuRts54HidI2cParameters;

typedef struct __attribute__ ((packed)) {
	guint8 cmd;
	guint8 ext;
	union {
		guint32 dwregaddr;
		struct {
			guint8 cmd_data0;
			guint8 cmd_data1;
			guint8 cmd_data2;
			guint8 cmd_data3;
		};
	};
	guint16 bufferlen;
	union {
		FuRts54HidI2cParameters parameters_i2c;
		guint32 parameters;
	};
} FuRts54HidCmdBuffer;

typedef enum {
	FU_RTS54HID_I2C_SPEED_250K,
	FU_RTS54HID_I2C_SPEED_400K,
	FU_RTS54HID_I2C_SPEED_800K,
	/* <private >*/
	FU_RTS54HID_I2C_SPEED_LAST,
} FuRts54HidI2cSpeed;

typedef enum {
	FU_RTS54HID_CMD_READ_DATA			= 0xc0,
	FU_RTS54HID_CMD_WRITE_DATA			= 0x40,
	/* <private >*/
	FU_RTS54HID_CMD_LAST,
} FuRts54HidCmd;

typedef enum {
	FU_RTS54HID_EXT_MCUMODIFYCLOCK			= 0x06,
	FU_RTS54HID_EXT_READ_STATUS			= 0x09,
	FU_RTS54HID_EXT_I2C_WRITE			= 0xc6,
	FU_RTS54HID_EXT_WRITEFLASH			= 0xc8,
	FU_RTS54HID_EXT_I2C_READ			= 0xd6,
	FU_RTS54HID_EXT_READFLASH			= 0xd8,
	FU_RTS54HID_EXT_VERIFYUPDATE			= 0xd9,
	FU_RTS54HID_EXT_ERASEBANK			= 0xe8,
	FU_RTS54HID_EXT_RESET2FLASH			= 0xe9,
	/* <private >*/
	FU_RTS54HID_EXT_LAST,
} FuRts54HidExt;
