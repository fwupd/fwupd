/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2020 Jimmy Yu <Jimmy_yu@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once


#include <glib.h>


#define CMD_FW_OTA_INIT 0x10
#define CMD_FW_WRITE    0x17
#define CMD_FW_UPGRADE  0x18
#define CMD_FW_MCU_RESET    0x22    /* Todo: remove this OP code handle after windows OTA app ready */
#define CMD_FW_GET_INFO     0x23

#define CMD_FW_OBJECT_CREATE 0x25
#define CMD_FW_OTA_INIT_NEW 0x27
#define CMD_FW_OTA_RETRANSMIT 0x28
#define CMD_FW_OTA_DISCONNECT 0x29

#define ERR_COMMAND_SUCCESS          0x00
#define ERR_COMMAND_UPDATE_FAIL      0xFF
#define EVT_COMMAND_COMPLETE         0x0E
#define CMD_COMPLETE_HDR_SZ     1

#define OTA_BUFFER_SIZE 256
#define MAX_OBJECT_SIZE 4096
#define PXI_OTA_PAYLOAD 20
#define PXI_OTA_BUF_SZ 32


/* brief Paremeter of CMD_FW_OTA_INIT command */
struct __attribute__((packed)) cmd_fw_set_address {
    guint16 sz;        /* OTA data size */
    guint32 addr;      /* OTA address */    
};

/* Paremeter of CMD_FW_UPGRADE command */
struct __attribute__((packed)) cmd_fw_upgrade {
    guint32 sz;            /* Firmware size */
    guint16 checksum;      /* Firmware checksum */
    guint8 version[10];    /* Firmware version */
};

/* Paremeter of CMD_FW_OTA_INIT_NEW command */
struct __attribute__((packed)) cmd_fw_ota_init_new {
    guint32 fw_length;         /* OTA firmware length */
    guint8 ota_setting;        /* OTA setting */
    guint8 fw_version[10];     /* Firmware version */
};

/* Paremeter of CMD_FW_OBJECT_CREATE command */
struct __attribute__((packed))  cmd_fw_object_create {
    guint32 fw_addr;       /* Firmware address to erase */
    guint32 object_size;   /* Object size */
};

/* Paremeter of CMD_FW_OTA_DISCONNECT command */
struct __attribute__((packed)) cmd_fw_ota_disconnect {
    guint8 reason;     /* Disconnect reason */
};

/* Command parameter */
union ota_cmd_parm
{
    struct cmd_fw_set_address fw_set_address;
    struct cmd_fw_upgrade fw_upgrade;
    struct cmd_fw_ota_init_new fw_ota_init_new;
    struct cmd_fw_object_create fw_object_create;
    struct cmd_fw_ota_disconnect fw_ota_disconnect;
};

/* Return paremeter of CMD_FW_OTA_INIT command */
struct __attribute__((packed)) ret_fw_ota_init_cmd {
    guint8 status;     /* Status */
};

/* Return paremeter of CMD_FW_OTA_INIT_NEW command */
struct __attribute__((packed)) ret_fw_ota_init_new_cmd {
    guint8 status;             /* Status */
    guint8 new_flow;           /* Inform OTA app to run new OTA flow */
    guint16 offset;            /* Current object offset already upgrade to flash */
    guint16 checksum;          /* Current checksum of data already upgrade to flash */
    guint32 max_object_size;   /* Max object size */
    guint16 mtu_size;          /* MTU size */
    guint16 prn_threshold;     /* Packet Receipt Notification(PRN) threshold */
    guint8 spec_check_result;  /* Spec check result */
};

/* Return paremeter of CMD_FW_UPGRADE command */
struct __attribute__((packed)) ret_fw_upgrade_cmd {
    guint8 status;     /* Status */
};

/* Return paremeter of CMD_FW_GET_INFO command */
struct __attribute__((packed)) ret_fw_info_get {
    guint8 status;         /* Status */
    guint8 version[5];     /* Firmware version */
    guint16 checksum;      /* Firmware checksum */
};

/* Return paremeter of OTA notify */
struct __attribute__((packed)) ret_fw_notify {
    guint8 opcode;     /* OP code */
    guint8 status;     /* Status */
    guint16 checksum;  /* Checksum */
};

/* Return paremeter */
union ota_cmd_ret_parm {
    struct ret_fw_upgrade_cmd fw_upgrade;
    struct ret_fw_info_get fw_info_get;
    struct ret_fw_ota_init_cmd fw_ota_init;
    struct ret_fw_ota_init_new_cmd fw_ota_init_new;
};

/* Return paremeter of HCI command */
struct __attribute__((packed)) evt_parameter {
    guint8 opcode;
    union ota_cmd_ret_parm ret_param;
};

/* HCI event */
struct __attribute__((packed)) hci_evt {
    guint8 evtcode;            /* Event code */
    guint8 evtlen;             /* Event length */
    struct evt_parameter evt_param;     /* Event parameter */
};

/* OTA target selection */
enum ota_process_setting {
    OTA_MAIN_FW,                /* Main firmware */
    OTA_HELPER_FW,              /* Helper firmware */
    OTA_EXTERNAL_RESOURCE,      /* External resource */
};

/* OTA spec check result */
enum ota_spec_check_result {
    OTA_SPEC_CHECK_OK = 1,              /* Spec check ok */
    OTA_FW_OUT_OF_BOUNDS = 2,           /* OTA firmware size out of bound */
    OTA_PROCESS_ILLEGAL = 3,            /* Illegal OTA process */
    OTA_RECONNECT = 4,                  /* Inform OTA app do reconnect */
    OTA_FW_IMG_VERSION_ERROR = 5,       /* FW image file version check error */
    OTA_SPEC_CHECK_MAX_NUM,             /* Max number of OTA driver defined error code */
};

/* OTA disconnect reason */
enum ota_disconnect_reason {
    OTA_CODE_JUMP = 1,          /* OTA code jump */
    OTA_UPDATE_DONE = 2,        /* OTA update done */
    OTA_RESET,                  /* OTA reset */
};

/* OTA firmware information */
struct __attribute__((packed)) ota_fw_info {
    guint8     fw_desc[32];        /* Firmware description */
    guint8     fw_version[8];      /* Firmware version */
    guint32    fw_size;            /* Firmware size */
    guint16    fw_checksum;        /* Firmware checksum */
};

enum ota_fw_upgrade_option {
    OTA_FW_UPGRADE_SWITCH_BANK,
    OTA_FW_UPGRADE_WITHOUT_SWITCH,
};
