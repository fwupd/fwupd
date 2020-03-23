/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-device.h"

#include "fwupd-error.h"

#include "fu-ccgx-common.h"

/* max Row data size of cyacd file */
#define CYACD_FLASH_ROW_MAX  256

/**
 * max ASCII buffer size of of cyacd file
 *  : (1) + array id (1*2) + row num (2*2) + row size (2*2) + row max*2
 *  + check sum(1*2) + \r\n (2)  + align margin (5)
*/
#define CYACD_ROW_ASCII_BUFFER_SIZE (CYACD_FLASH_ROW_MAX * 2 + 20)

/**
 *  max row buffer size of of cyacd file
 *  row num (2) +  row size(2) + row max + align margin (4)
*/
#define CYACD_ROW_BUFFER_SIZE  (4 + CYACD_FLASH_ROW_MAX + 4)

/* max number of cyacd handle */
#define CYACD_HANDLE_MAX_COUNT 2

/* cyacd file handle */
typedef struct __attribute__((packed)) {
	guint8  *buffer;	/* buffer */
	guint32 buffer_size;	/* size of buffer */
	guint32 pos;		/* data position in the buffer */
} CyacdFileHandle;

/* cyacd file information */
typedef struct  __attribute__((packed)) {
	guint16		silicon_id;	/* silicon ID */
	PDFWAppVersion	app_version;	/* firmware Application Version */
	FWMode		fw_mode;	/* frmware Mode */
	guint32		row_size;	/* row Size */
	CCGxMetaData	fw_metadata;	/* firmware metadata */
} CyacdFileInfo;

/* row number stored appication version for CCG2 */
#define CCG2_APP_VERSION_ROW_NUM	0x26

/* offset stored appication version for CCGx */
#define CCGX_APP_VERSION_OFFSET		228  /* 128+64+32+4 */

guint32		 fu_ccgx_cyacd_file_init_handle		(CyacdFileHandle *handle_array,
							 guint32	 num_of_array,
							 guint8		*buffer,
							 guint32	 buffer_size);
void		 fu_ccgx_cyacd_file_set_pos		(CyacdFileHandle *handle,
							 guint32	 pos);
guint32		 fu_ccgx_cyacd_file_get_pos		(CyacdFileHandle *handle);
gboolean	 fu_ccgx_cyacd_file_parse		(CyacdFileHandle *handle,
							 CyacdFileInfo	*info);
gboolean	 fu_ccgx_cyacd_file_read_row		(CyacdFileHandle *handle,
							 guint8		*data,
							 guint32	 size);
