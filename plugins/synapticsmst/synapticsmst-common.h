/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __SYNAPTICSMST_COMMON_H
#define __SYNAPTICSMST_COMMON_H

#include <glib.h>
#include <gio/gio.h>

#define ADDR_CUSTOMER_ID	0X10E
#define ADDR_BOARD_ID		0x10F

#define REG_RC_CAP		0x4B0
#define REG_RC_STATE		0X4B1
#define REG_RC_CMD		0x4B2
#define REG_RC_RESULT		0x4B3
#define REG_RC_LEN		0x4B8
#define REG_RC_OFFSET		0x4BC
#define REG_RC_DATA		0x4C0

#define REG_VENDOR_ID		0x500
#define REG_CHIP_ID		0x507
#define REG_FIRMWARE_VERSION	0x50A

typedef enum {
	DPCD_SUCCESS			= 0,
	DPCD_SEEK_FAIL,
	DPCD_ACCESS_FAIL,
} SynapticsMstDpcdRc;

typedef enum {
	UPDC_COMMAND_SUCCESS		= 0,
	UPDC_COMMAND_INVALID,
	UPDC_COMMAND_UNSUPPORT,
	UPDC_COMMAND_FAILED,
	UPDC_COMMAND_DISABLED,
} SynapticsMstUpdcRc;

typedef enum {
	UPDC_ENABLE_RC			= 1,
	UPDC_DISABLE_RC,
	UPDC_GET_ID,
	UPDC_GET_VERSION,
	UPDC_ENABLE_FLASH_CHIP_ERASE	= 8,
	UPDC_CAL_EEPROM_CHECKSUM	= 0X11,
	UPDC_FLASH_ERASE		= 0X14,
	UPDC_CAL_EEPROM_CHECK_CRC8	= 0X16,
	UPDC_CAL_EEPROM_CHECK_CRC16,
	UPDC_WRITE_TO_EEPROM		= 0X20,
	UPDC_WRITE_TO_MEMORY		= 0x21,
	UPDC_WRITE_TO_TX_DPCD		= 0x22,
	UPDC_READ_FROM_EEPROM		= 0x30,
	UPDC_READ_FROM_TX_DPCD		= 0x32,
} SynapticsMstUpdcCmd;

typedef struct _SynapticsMSTConnection SynapticsMSTConnection;

void		 synapticsmst_common_free 			(SynapticsMSTConnection *connection);

SynapticsMSTConnection	*synapticsmst_common_new 		(gint	 fd,
								 guint8	 layer,
								 guint	 rad);

guint8		 synapticsmst_common_aux_node_read		(SynapticsMSTConnection *connection,
								 gint offset,
								 gint *buf,
								 gint length);

guint8		 synapticsmst_common_read_dpcd 			(SynapticsMSTConnection *connection,
								 gint	 offset,
								 gint	*buf,
								 gint	 length);

guint8		 synapticsmst_common_write_dpcd 		(SynapticsMSTConnection *connection,
								 gint	 offset,
								 const gint *buf,
								 gint	 length);

guint8		 synapticsmst_common_rc_set_command 		(SynapticsMSTConnection *connection,
								 gint	 rc_cmd,
								 gint	 length,
								 gint	 offset,
								 const guint8	*buf);

guint8		 synapticsmst_common_rc_get_command 		(SynapticsMSTConnection *connection,
								 gint	 rc_cmd,
								 gint	 length,
								 gint	 offset,
								 guint8	*buf);

guint8		 synapticsmst_common_rc_special_get_command	(SynapticsMSTConnection *connection,
								 gint	 rc_cmd,
								 gint	 cmd_length,
								 gint	 cmd_offset,
								 guint8	*cmd_data,
								 gint	 length,
								 guint8	*buf);

guint8		 synapticsmst_common_enable_remote_control	(SynapticsMSTConnection *connection);

guint8		 synapticsmst_common_disable_remote_control	(SynapticsMSTConnection *connection);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(SynapticsMSTConnection, synapticsmst_common_free)
#pragma clang diagnostic pop

#endif /* __SYNAPTICSMST_COMMON_H */
