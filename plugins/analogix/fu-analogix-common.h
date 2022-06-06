/*
 * Copyright (C) 2021 Xiaotian Cui <xtcui@analogixsemi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define ANX_BB_TRANSACTION_TIMEOUT 5000 /* ms */
#define BILLBOARD_CLASS		   0x11
#define BILLBOARD_SUBCLASS	   0x00
#define BILLBOARD_PROTOCOL	   0x00
#define BILLBOARD_MAX_PACKET_SIZE  64
#define OCM_FLASH_SIZE		   0x18000
#define SECURE_OCM_TX_SIZE	   0x3000
#define SECURE_OCM_RX_SIZE	   0x3000
#define CUSTOM_FLASH_SIZE	   0x1000

#define FLASH_OCM_ADDR	    0x1000
#define FLASH_TXFW_ADDR	    0x31000
#define FLASH_RXFW_ADDR	    0x34000
#define FLASH_CUSTOM_ADDR   0x38000
#define OCM_FW_VERSION_ADDR 0x14FF0

/* bRequest for Phoenix-Lite Billboard */
typedef enum {
	ANX_BB_RQT_SEND_UPDATE_DATA = 0x01,
	ANX_BB_RQT_READ_UPDATE_DATA = 0x02,
	ANX_BB_RQT_GET_UPDATE_STATUS = 0x10,
	ANX_BB_RQT_READ_FW_VER = 0x12,
	ANX_BB_RQT_READ_CUS_VER = 0x13,
	ANX_BB_RQT_READ_FW_RVER = 0x19,
	ANX_BB_RQT_READ_CUS_RVER = 0x1c,
} AnxBbRqtCode;

/* wValue low byte */
typedef enum {
	ANX_BB_WVAL_UPDATE_OCM = 0x06,
	ANX_BB_WVAL_UPDATE_CUSTOM_DEF = 0x07,
	ANX_BB_WVAL_UPDATE_SECURE_TX = 0x08,
	ANX_BB_WVAL_UPDATE_SECURE_RX = 0x09,
} AnxwValCode;

typedef enum {
	UPDATE_STATUS_INVALID,
	UPDATE_STATUS_START,
	UPDATE_STATUS_FINISH,
	UPDATE_STATUS_ERROR = 0xFF,
} AnxUpdateStatus;

const gchar *
fu_analogix_update_status_to_string(AnxUpdateStatus status);
