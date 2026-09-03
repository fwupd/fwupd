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

/* MCU register access helpers */
#define FU_REALTEK_BILLBOARD_MCU_REG_ADDR(val) ((0xFF00) | ((val) & 0xFF))

/* MCU register bit definitions (for reg 0xEE) */
#define FU_REALTEK_BILLBOARD_MCU_REG_USB_ATTACH 0x02

/* FW flash port access method isp opcode (reg 0x6D) */
#define FU_REALTEK_BILLBOARD_FW_FLASH_PORT_ACC_ISP 0x02

/* USB Billboard device class code (USB spec table 4-1) */
#define FU_REALTEK_BILLBOARD_USB_CLASS 0x11

/* flash geometry */
#define FU_REALTEK_BILLBOARD_SECTOR_SIZE 0x1000					 /* 4 KB */
#define FU_REALTEK_BILLBOARD_BANK_SIZE	 (16 * FU_REALTEK_BILLBOARD_SECTOR_SIZE) /* 64 KB */
#define FU_REALTEK_BILLBOARD_FLAG_SIZE	 5 /* user flag bytes */
