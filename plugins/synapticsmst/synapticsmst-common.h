/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef __SYNAPTICSMST_COMMON_H
#define __SYNAPTICSMST_COMMON_H

#include <glib.h>
#include <gio/gio.h>

#define ADDR_CUSTOMER_ID		0X10E
#define ADDR_BOARD_ID			0x10F

#define REG_RC_CAP			0x4B0
#define REG_RC_STATE			0X4B1
#define REG_RC_CMD			0x4B2
#define REG_RC_RESULT			0x4B3
#define REG_RC_LEN			0x4B8
#define REG_RC_OFFSET			0x4BC
#define REG_RC_DATA			0x4C0

#define REG_VENDOR_ID			0x500
#define REG_CHIP_ID			0x507
#define REG_FIRMWARE_VERSIOIN		0x50A

typedef enum {
	UPDC_COMMAND_SUCCESS = 0,
	UPDC_COMMAND_INVALID,
	UPDC_COMMAND_UNSUPPORT,
	UPDC_COMMAND_FAILED,
	UPDC_COMMAND_DISABLED,
}RC_STATUS;

typedef enum {
	UPDC_ENABLE_RC = 1,
	UPDC_DISABLE_RC,
	UPDC_GET_ID,
	UPDC_GET_VERSION,
	UPDC_ENABLE_FLASH_CHIP_ERASE = 8,
	UPDC_CAL_EEPROM_CHECKSUM = 0X11,
	UPDC_FLASH_ERASE = 0X14,
	UPDC_CAL_EEPROM_CHECK_CRC8 = 0X16,
	UPDC_CAL_EEPROM_CHECK_CRC16,
	UPDC_WRITE_TO_EEPROM = 0X20,
	UPDC_READ_FROM_EEPROM = 0X30,
}RC_COMMAND;

guchar		 synapticsmst_common_read_dpcd			(gint offset,
								 gint *buf,
								 gint length);
guchar		 synapticsmst_common_write_dpcd			(gint offset,
								 gint *buf,
								 gint length);
guchar		 synapticsmst_common_open_aux_node		(const gchar *filename);
void		 synapticsmst_common_close_aux_node		(void);
guchar		 synapticsmst_common_rc_set_command		(gint rc_cmd,
								 gint length,
								 gint offset,
								 guchar *buf);
guchar		 synapticsmst_common_rc_get_command		(gint rc_cmd,
								 gint length,
								 gint offset,
								 guchar *buf);
guchar		 synapticsmst_common_rc_special_get_command	(gint rc_cmd,
								 gint cmd_length,
								 gint cmd_offset,
								 guchar *cmd_data,
								 gint length,
								 guchar *buf);
gboolean 	 synapticsmst_common_check_supported_system	(GError **error);

#endif /* __SYNAPTICSMST_COMMON_H */
