/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#define FU_WAC_PACKET_LEN				512

#define FU_WAC_REPORT_ID_COMMAND			0x01
#define FU_WAC_REPORT_ID_STATUS				0x02
#define FU_WAC_REPORT_ID_CONTROL			0x03

#define FU_WAC_REPORT_ID_GET_FIRMWARE_VERSION_MAIN	0x07
#define FU_WAC_REPORT_ID_GET_FIRMWARE_VERSION_TOUCH	0x07
#define FU_WAC_REPORT_ID_GET_FIRMWARE_VERSION_BLUETOOTH	0x16

#define FU_WAC_REPORT_ID_FW_DESCRIPTOR			0xcb /* GET_FEATURE */
#define FU_WAC_REPORT_ID_SWITCH_TO_FLASH_LOADER		0xcc /* SET_FEATURE */
#define FU_WAC_REPORT_ID_QUIT_AND_RESET			0xcd /* SET_FEATURE */
#define FU_WAC_REPORT_ID_READ_BLOCK_DATA		0xd1 /* GET_FEATURE */
#define FU_WAC_REPORT_ID_WRITE_BLOCK			0xd2 /* SET_FEATURE */
#define FU_WAC_REPORT_ID_ERASE_BLOCK			0xd3 /* SET_FEATURE */
#define FU_WAC_REPORT_ID_SET_READ_ADDRESS		0xd4 /* GET_FEATURE */
#define FU_WAC_REPORT_ID_GET_STATUS			0xd5 /* GET_FEATURE */
#define FU_WAC_REPORT_ID_UPDATE_RESET			0xd6 /* SET_FEATURE */
#define FU_WAC_REPORT_ID_WRITE_WORD			0xd7 /* SET_FEATURE */
#define FU_WAC_REPORT_ID_GET_PARAMETERS			0xd8 /* GET_FEATURE */
#define FU_WAC_REPORT_ID_GET_FLASH_DESCRIPTOR		0xd9 /* GET_FEATURE */
#define FU_WAC_REPORT_ID_GET_CHECKSUMS			0xda /* GET_FEATURE */
#define FU_WAC_REPORT_ID_SET_CHECKSUM_FOR_BLOCK		0xdb /* SET_FEATURE */
#define FU_WAC_REPORT_ID_CALCULATE_CHECKSUM_FOR_BLOCK	0xdc /* SET_FEATURE */
#define FU_WAC_REPORT_ID_WRITE_CHECKSUM_TABLE		0xde /* SET_FEATURE */
#define FU_WAC_REPORT_ID_GET_CURRENT_FIRMWARE_IDX	0xe2 /* GET_FEATURE */
#define FU_WAC_REPORT_ID_MODULE				0xe4

guint32		 fu_wac_calculate_checksum32le		(const guint8	*data,
							 gsize		 len);
guint32		 fu_wac_calculate_checksum32le_bytes	(GBytes		*blob);
const gchar	*fu_wac_report_id_to_string		(guint8		 report_id);
void		 fu_wac_buffer_dump			(const gchar	*title,
							 guint8		 cmd,
							 const guint8	*buf,
							 gsize		 sz);
