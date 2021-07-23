/*
 * Copyright (C) 2021 Jimmy Yu <Jimmy_yu@pixart.com>
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define PXI_HID_WIRELESS_DEV_OTA_REPORT_ID		0x03

#define FU_PXI_DEVICE_CMD_FW_OTA_INIT			0x10u
#define FU_PXI_DEVICE_CMD_FW_WRITE			0x17u
#define FU_PXI_DEVICE_CMD_FW_UPGRADE			0x18u
#define FU_PXI_DEVICE_CMD_FW_MCU_RESET			0x22u
#define FU_PXI_DEVICE_CMD_FW_GET_INFO			0x23u
#define FU_PXI_DEVICE_CMD_FW_OBJECT_CREATE		0x25u
#define FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW		0x27u
#define FU_PXI_DEVICE_CMD_FW_OTA_RETRANSMIT		0x28u
#define FU_PXI_DEVICE_CMD_FW_OTA_DISCONNECT		0x29u
#define FU_PXI_DEVICE_CMD_FW_OTA_GET_NUM_OF_MODELS	0x2au
#define FU_PXI_DEVICE_CMD_FW_OTA_GET_MODEL		0x2bu
#define FU_PXI_DEVICE_CMD_FW_OTA_PAYLOAD_CONTENT	0x40u
#define FU_PXI_DEVICE_CMD_FW_OTA_CHECK_CRC		0x41u
#define FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW_CHECK		0x42u

#define FU_PXI_BLE_DEVICE_RF_CMD_CODE			0x65u
#define FU_PXI_BLE_DEVICE_RF_CMD_HID_SN			0x0
#define FU_PXI_BLE_DEVICE_RF_CMD_HID_SN			0x0

#define FU_PXI_WIRELESS_DEVICE_TARGET_RECEIVER		0
#define FU_PXI_RECEIVER_DEVICE_OTA_BUF_SZ		32	/* bytes */
#define FU_PXI_DEVICE_MODEL_NAME_LEN			12	/* bytes */

#define FU_PXI_DEVICE_OBJECT_SIZE_MAX			4096	/* bytes */

#define FU_PXI_WIRELESS_DEVICE_RETRY_MAXIMUM		1000

/* OTA spec check result */
enum ota_spec_check_result {
	OTA_SPEC_CHECK_OK		= 1,	/* Spec check ok */
	OTA_FW_OUT_OF_BOUNDS		= 2,	/* OTA firmware size out of bound */
	OTA_PROCESS_ILLEGAL		= 3,	/* Illegal OTA process */
	OTA_RECONNECT			= 4,	/* Inform OTA app do reconnect */
	OTA_FW_IMG_VERSION_ERROR	= 5,	/* FW image file version check error */
	OTA_DEVICE_LOW_BATTERY		= 6,	/* Device is under low battery */
	OTA_SPEC_CHECK_MAX_NUM,			/* Max number of OTA driver defined error code */
};

/* pixart device model structure */
struct ota_fw_dev_model {
	guint8		 status;
	guint8		 name[FU_PXI_DEVICE_MODEL_NAME_LEN];
	guint8		 type;
	guint8		 target;
	guint8		 version[5];
	guint16		 checksum;
};

/* pixart fw info structure */
struct ota_fw_info {
	guint8		 status;
	guint8		 version[5];
	guint16		 checksum;
};

/* ota disconnect reason */
enum ota_disconnect_reason {
	OTA_CODE_JUMP			= 1,	/* OTA code jump */
	OTA_UPDATE_DONE			= 2,	/* OTA update done */
	OTA_RESET,				/* OTA reset */
};

/* ota rsp code for wireless module */
enum wireless_module_type {
	OTA_WIRELESS_MODULE_TYPE_MOUSE,
	OTA_WIRELESS_MODULE_TYPE_KEYBOARD,
	OTA_WIRELESS_MODULE_TYPE_RECEIVER,
};

/* ota rsp code for wireless module */
enum wireless_module_ota_rsp_code {
	OTA_RSP_OK,
	OTA_RSP_FINISH,
	OTA_RSP_FAIL,
	OTA_RSP_CODE_ERROR,
	OTA_RSP_WRITE_PKT_LEN_ERROR,
	OTA_RSP_WRITE_PKT_TOTAL_SIZE_ERROR,
	OTA_RSP_READ_PKT_LEN_ERROR,
	OTA_RSP_NOT_READY,
};

struct ota_fw_state {
	guint8		 status;
	guint8		 new_flow;
	guint16		 offset;
	guint16		 checksum;
	guint32		 max_object_size;
	guint16		 mtu_size;
	guint16		 prn_threshold;
	guint8		 spec_check_result;
};

guint8		 fu_pxi_common_sum8		(const guint8	*buf,
						 gsize		 bufsz);
guint16		 fu_pxi_common_sum16		(const guint8	*buf,
						 gsize		 bufsz);
gboolean	 fu_pxi_composite_receiver_cmd	(guint8		 opcode,
						 guint8		 sn,
						 guint8		 target,
						 GByteArray	*wireless_mod_cmd,
						 GByteArray	*ota_cmd,
						 GError		**error);
const gchar	*fu_pxi_receiver_cmd_result_to_string (guint8	 result);
const gchar	*fu_pxi_spec_check_result_to_string (guint8	 spec_check_result);

void		 fu_pxi_ota_fw_state_to_string	(struct ota_fw_state *fwstate,
						 guint		 idt,
						 GString	*str);
gboolean	 fu_pxi_ota_fw_state_parse	(struct ota_fw_state *fwstate,
						 const guint8	*buf,
						 gsize		 bufsz,
						 gsize		 offset,
						 GError		**error);
