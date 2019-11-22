/*
 * Copyright (C) 2018-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#define FU_WACOM_RAW_CMD_RETRIES		1000

#define FU_WACOM_RAW_STATUS_REPORT_ID		0x04
#define FU_WACOM_RAW_STATUS_REPORT_SZ		16

#define FU_WACOM_RAW_FW_REPORT_ID		0x02
#define FU_WACOM_RAW_FW_CMD_QUERY_MODE		0x00
#define FU_WACOM_RAW_FW_CMD_DETACH		0x02
#define FU_WACOM_RAW_FW_REPORT_SZ		2

#define FU_WACOM_RAW_BL_START_ADDR		(0x11FF8)
#define FU_WACOM_RAW_BL_BYTES_CHECK		8

#define FU_WACOM_RAW_BL_REPORT_ID_SET		0x07
#define FU_WACOM_RAW_BL_REPORT_ID_GET		0x08

#define FU_WACOM_RAW_BL_CMD_ERASE_FLASH		0x00
#define FU_WACOM_RAW_BL_CMD_WRITE_FLASH		0x01
#define FU_WACOM_RAW_BL_CMD_VERIFY_FLASH	0x02
#define FU_WACOM_RAW_BL_CMD_ATTACH		0x03
#define FU_WACOM_RAW_BL_CMD_GET_BLVER		0x04
#define FU_WACOM_RAW_BL_CMD_GET_MPUTYPE		0x05
#define FU_WACOM_RAW_BL_CMD_CHECK_MODE		0x07
#define FU_WACOM_RAW_BL_CMD_ERASE_DATAMEM	0x0e
#define FU_WACOM_RAW_BL_CMD_ALL_ERASE		0x90

#define FU_WACOM_RAW_RC_OK			0x00
#define FU_WACOM_RAW_RC_BUSY			0x80
#define FU_WACOM_RAW_RC_MCUTYPE			0x0c
#define FU_WACOM_RAW_RC_PID			0x0d
#define FU_WACOM_RAW_RC_CHECKSUM1		0x81
#define FU_WACOM_RAW_RC_CHECKSUM2		0x82
#define FU_WACOM_RAW_RC_TIMEOUT			0x87
#define FU_WACOM_RAW_RC_IN_PROGRESS		0xff

#define FU_WACOM_RAW_ECHO_DEFAULT		g_random_int_range(0xa0,0xfe)

typedef struct __attribute__((packed)) {
	guint8	 report_id;
	guint8	 cmd;
	guint8	 echo;
	guint32	 addr;
	guint8	 size8;
	guint8	 data[128];
	guint8	 data_unused[121];
} FuWacomRawRequest;

typedef struct __attribute__((packed)) {
	guint8	 report_id;
	guint8	 cmd;
	guint8	 echo;
	guint8	 resp;
	guint8	 data_unused[132];
} FuWacomRawResponse;

gboolean	 fu_wacom_common_rc_set_error	(const FuWacomRawResponse *rsp,
						 GError		**error);
gboolean	 fu_wacom_common_check_reply	(const FuWacomRawRequest *req,
						 const FuWacomRawResponse *rsp,
						 GError		**error);
gboolean	 fu_wacom_common_block_is_empty	(const guint8	*data,
						 guint16	 datasz);
