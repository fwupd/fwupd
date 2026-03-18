/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

/* Device IDs */
#define FU_LXSTOUCH_VID_NORMAL		   0x1FD2
#define FU_LXSTOUCH_PID_NORMAL_B011	   0xB011
#define FU_LXSTOUCH_VID_DFUP		   0x29BD
#define FU_LXSTOUCH_PID_DFUP		   0x5357

/* Communication Constants */
#define FU_LXSTOUCH_BUFFER_SIZE		   64
#define FU_LXSTOUCH_REPORT_ID		   0x09

/* SWIP Protocol Register Addresses */
#define FU_LXSTOUCH_REG_INFO_PANEL	   0x0110
#define FU_LXSTOUCH_REG_INFO_VERSION	   0x0120
#define FU_LXSTOUCH_REG_INFO_INTEGRITY	   0x0140
#define FU_LXSTOUCH_REG_INFO_INTERFACE	   0x0150
#define FU_LXSTOUCH_REG_CTRL_GETTER	   0x0600
#define FU_LXSTOUCH_REG_CTRL_SETTER	   0x0610
#define FU_LXSTOUCH_REG_CTRL_DFUP_FLAG	   0x0623
#define FU_LXSTOUCH_REG_FLASH_IAP_CTRL_CMD 0x1400
#define FU_LXSTOUCH_REG_PARAMETER_BUFFER   0x6000

/* Flash IAP Commands */
#define FU_LXSTOUCH_CMD_FLASH_WRITE	      0x03
#define FU_LXSTOUCH_CMD_FLASH_4KB_UPDATE_MODE 0x04
#define FU_LXSTOUCH_CMD_FLASH_GET_VERIFY      0x05
#define FU_LXSTOUCH_CMD_WATCHDOG_RESET	      0x11

/* Protocol Modes */
#define FU_LXSTOUCH_MODE_NORMAL 0x00
#define FU_LXSTOUCH_MODE_DIAG	0x01
#define FU_LXSTOUCH_MODE_DFUP	0x02

/* Ready Status Values */
#define FU_LXSTOUCH_READY_STATUS_READY 0xA0
#define FU_LXSTOUCH_READY_STATUS_NONE  0x05
#define FU_LXSTOUCH_READY_STATUS_LOG   0x77
#define FU_LXSTOUCH_READY_STATUS_IMAGE 0xAA

/* Protocol Names - 8 bytes */
#define FU_LXSTOUCH_PROTOCOL_NAME_SWIP "SWIP"
#define FU_LXSTOUCH_PROTOCOL_NAME_DFUP "DFUP"

/* Write/Read Command Flags */
#define FU_LXSTOUCH_FLAG_WRITE 0x68
#define FU_LXSTOUCH_FLAG_READ  0x69

/* Firmware Sizes */
#define FU_LXSTOUCH_FW_SIZE_APP_ONLY   (112 * 1024) /* 0x1C000 */
#define FU_LXSTOUCH_FW_SIZE_BOOT_APP   (128 * 1024) /* 0x20000 */
#define FU_LXSTOUCH_FW_OFFSET_APP_ONLY 0x4000

/* Download Configuration */
#define FU_LXSTOUCH_DOWNLOAD_CHUNK_SIZE_NORMAL 128
#define FU_LXSTOUCH_DOWNLOAD_CHUNK_SIZE_4K     4096
#define FU_LXSTOUCH_TRANSMIT_UNIT_NORMAL       16
#define FU_LXSTOUCH_TRANSMIT_UNIT_4K	       48

/* Timeouts */
#define FU_LXSTOUCH_TIMEOUT_READY_MS	 5000
#define FU_LXSTOUCH_TIMEOUT_RECONNECT_MS 5000

/* ChromeOS Fast Version Check Requirement */
#define FU_LXSTOUCH_VERSION_FAST_MAX_MS 40
