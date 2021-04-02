/*
 * Copyright (C) 2021 Xiaotian Cui <xtcui@analogixsemi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

#include "fu-plugin.h"

#define ANX_BB_TRANSACTION_TIMEOUT                1000 /* ms */
#define BILLBOARD_CLASS 0x11
#define BILLBOARD_SUBCLASS 0x00
#define BILLBOARD_PROTOCOL 0x00
#define BILLBOARD_MAX_PACKET_SIZE 64
#define OCM_FLASH_SZIE 0x18000
#define SECURE_OCM_TX_SIZE 0x3000
#define SECURE_OCM_RX_SIZE 0x3000
#define CUSTOM_FLASH_SIZE 0x1000
#define MAX_FILE_SIZE (OCM_FLASH_SZIE+SECURE_OCM_TX_SIZE+ \
                        SECURE_OCM_RX_SIZE+CUSTOM_FLASH_SIZE+0x1000)

#define FLASH_OCM_ADDR                                        0x1000
#define FLASH_TXFW_ADDR                                        0x31000
#define FLASH_RXFW_ADDR                                        0x34000
#define FLASH_CUSTOM_ADDR                                0x38000
#define OCM_FW_VERSION_ADDR             0x4FF0

/*bRequest for Phoenix-Lite Billboard*/
typedef enum {
    ANX_BB_RQT_SEND_UPDATE_DATA = 0x01,
    ANX_BB_RQT_READ_UPDATE_DATA = 0x02,
    ANX_BB_RQT_GET_UPDATE_STATUS =  0x10,
    ANX_BB_RQT_READ_FW_VER = 0x12,
    ANX_BB_RQT_READ_CUS_VER = 0x13,
    ANX_BB_RQT_READ_FW_RVER = 0x19
}AnxBbRqtCode;

/*wValue low byte*/
typedef enum{
    ANX_BB_WVAL_UPDATE_OCM = 0x06,
    ANX_BB_WVAL_UPDATE_CUSTOM_DEF = 0x07,
    ANX_BB_WVAL_UPDATE_SECURE_TX = 0x08,
    ANX_BB_WVAL_UPDATE_SECURE_RX = 0x09
}AnxwValCode;

typedef enum {
    UPDATE_STATUS_INVALID = 0,
    UPDATE_STATUS_START,
    UPDATE_STATUS_FINISH,
    UPDATE_STATUS_ERROR = 0xFF
}AnxUpdateStatus;

#define HEX_LINE_HEADER_SIZE 9

typedef struct __attribute__ ((packed)){
    guint32 fw_start_addr;
    guint32 fw_end_addr;
    guint32 fw_payload_len;
    guint32 custom_start_addr;
    guint32 custom_payload_len;
    guint32 secure_tx_start_addr;
    guint32 secure_tx_payload_len;
    guint32 secure_rx_start_addr;
    guint32 secure_rx_payload_len;
    guint32 total_len;
    guint16 custom_ver;
    guint16 fw_ver;
}AnxImgHeader;

guint64 hex_str_to_dec (const gchar* str, guint8 len);
gboolean parse_fw_hex_file (const guint8* fw_src, guint32 src_fw_size, 
                            AnxImgHeader* out_header, guint8 *out_binary);
