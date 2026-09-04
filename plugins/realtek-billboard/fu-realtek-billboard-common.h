/*
 * Copyright 2026 Realtek Corporation
 * Copyright 2026 Shadow Zhang <shadow_zhang@realsil.com.cn>
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#pragma once

#include <fwupdplugin.h>

/* control transfer timeout */
#define FU_REALTEK_BILLBOARD_TRANSACTION_TIMEOUT 5000 /* ms */

/* max data payload per control transfer */
#define FU_REALTEK_BILLBOARD_MAX_PACKET_SIZE 256 /* bytes */

/* vendor bRequest codes */
typedef enum {
	FU_REALTEK_BILLBOARD_RQT_SET_REGISTER = 0x01, /* write MCU register */
	FU_REALTEK_BILLBOARD_RQT_GET_REGISTER = 0x02, /* read MCU register */
	FU_REALTEK_BILLBOARD_RQT_SEND_DATA = 0x03,    /* send firmware data */
	FU_REALTEK_BILLBOARD_RQT_WRITE_FLASH = 0x40,
	FU_REALTEK_BILLBOARD_RQT_READ_FLASH = 0x41,
	FU_REALTEK_BILLBOARD_RQT_SECTOR_ERASE = 0x42,
	FU_REALTEK_BILLBOARD_RQT_BANK_ERASE = 0x43,
	FU_REALTEK_BILLBOARD_RQT_ISP_ENABLE = 0x44,
	FU_REALTEK_BILLBOARD_RQT_DUAL_BANK = 0x45,
	FU_REALTEK_BILLBOARD_RQT_HANDSHAKE = 0x50,
} FuRealtekBillboardRqt;

/* MCU register access helpers */
#define FU_REALTEK_BILLBOARD_MCU_REG_ADDR(val) ((0xFF00) | ((val) & 0xFF))

/* MCU register addresses */
typedef enum {
	FU_REALTEK_BILLBOARD_MCU_REG_FW_FLASH_PORT_ACC = 0x6D, /* FW flash port access */
	FU_REALTEK_BILLBOARD_MCU_REG_USB = 0xEE,	       /* USB control register */
} FuRealtekBillboardMcuReg;

/* MCU register bit definitions (for reg 0xEE) */
#define FU_REALTEK_BILLBOARD_MCU_REG_USB_ATTACH 0x02

/* FW flash port access method isp opcode (reg 0x6D) */
#define FU_REALTEK_BILLBOARD_FW_FLASH_PORT_ACC_ISP 0x02

/* direct register addresses for firmware version */
typedef enum {
	FU_REALTEK_BILLBOARD_REG_FW_VERSION = 0x0004,	  /* [5:0] = FW version */
	FU_REALTEK_BILLBOARD_REG_FW_SUB_VERSION = 0x0007, /* [7:0] = FW sub version */
} FuRealtekBillboardReg;

/* opcodes for dual bank requests (used as wValue) */
typedef enum {
	FU_REALTEK_BILLBOARD_DUAL_BANK_OP_GET_START_ADDR = 0x02,
	FU_REALTEK_BILLBOARD_DUAL_BANK_OP_GET_FLAG_ADDR = 0x04,
} FuRealtekBillboardDualBankOp;

/* USB Billboard device class code (USB spec table 4-1) */
#define FU_REALTEK_BILLBOARD_USB_CLASS 0x11

/* flash geometry */
#define FU_REALTEK_BILLBOARD_SECTOR_SIZE 0x1000					 /* 4 KB */
#define FU_REALTEK_BILLBOARD_BANK_SIZE	 (16 * FU_REALTEK_BILLBOARD_SECTOR_SIZE) /* 64 KB */
#define FU_REALTEK_BILLBOARD_FLAG_SIZE	 5 /* user flag bytes */
