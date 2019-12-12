/*
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>
#include <gio/gio.h>

#define FU_TYPE_SYNAPTICS_MST_CONNECTION (fu_synaptics_mst_connection_get_type ())
G_DECLARE_FINAL_TYPE (FuSynapticsMstConnection, fu_synaptics_mst_connection, FU, SYNAPTICS_MST_CONNECTION, GObject)

#define ADDR_CUSTOMER_ID	0X10E
#define ADDR_BOARD_ID		0x10F

#define ADDR_MEMORY_CUSTOMER_ID	0x170E
#define ADDR_MEMORY_BOARD_ID	0x170F

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
	UPDC_COMMAND_SUCCESS		= 0,
	UPDC_COMMAND_INVALID,
	UPDC_COMMAND_UNSUPPORT,
	UPDC_COMMAND_FAILED,
	UPDC_COMMAND_DISABLED,
} SynapticsMstUpdcRc;

typedef enum {
	UPDC_ENABLE_RC			= 0x01,
	UPDC_DISABLE_RC			= 0x02,
	UPDC_GET_ID			= 0x03,
	UPDC_GET_VERSION		= 0x04,
	UPDC_ENABLE_FLASH_CHIP_ERASE	= 0x08,
	UPDC_CAL_EEPROM_CHECKSUM	= 0x11,
	UPDC_FLASH_ERASE		= 0x14,
	UPDC_CAL_EEPROM_CHECK_CRC8	= 0x16,
	UPDC_CAL_EEPROM_CHECK_CRC16	= 0x17,
	UPDC_WRITE_TO_EEPROM		= 0X20,
	UPDC_WRITE_TO_MEMORY		= 0x21,
	UPDC_WRITE_TO_TX_DPCD		= 0x22,
	UPDC_READ_FROM_EEPROM		= 0x30,
	UPDC_READ_FROM_MEMORY		= 0x31,
	UPDC_READ_FROM_TX_DPCD		= 0x32,
} SynapticsMstUpdcCmd;

FuSynapticsMstConnection	*fu_synaptics_mst_connection_new (gint	 fd,
								 guint8	 layer,
								 guint	 rad);

gboolean	 fu_synaptics_mst_connection_read		(FuSynapticsMstConnection *self,
								 guint32	 offset,
								 guint8		*buf,
								 guint32	 length,
								 GError		**error);

gboolean	 fu_synaptics_mst_connection_write 		(FuSynapticsMstConnection *self,
								 guint32	 offset,
								 const guint8 	*buf,
								 guint32	 length,
								 GError		**error);

gboolean	 fu_synaptics_mst_connection_rc_set_command 	(FuSynapticsMstConnection *self,
								 guint32	 rc_cmd,
								 guint32	 length,
								 guint32	 offset,
								 const guint8	*buf,
								 GError		**error);


gboolean	 fu_synaptics_mst_connection_rc_get_command 	(FuSynapticsMstConnection *self,
								 guint32	 rc_cmd,
								 guint32	 length,
								 guint32	 offset,
								 guint8		*buf,
								 GError		**error);

gboolean	 fu_synaptics_mst_connection_rc_special_get_command(FuSynapticsMstConnection *self,
								 guint32	 rc_cmd,
								 guint32	 cmd_length,
								 guint32	 cmd_offset,
								 guint8		*cmd_data,
								 guint32	 length,
								 guint8		*buf,
								 GError		**error);

gboolean	 fu_synaptics_mst_connection_enable_rc		(FuSynapticsMstConnection *self,
								 GError **error);

gboolean	 fu_synaptics_mst_connection_disable_rc		(FuSynapticsMstConnection *self,
								 GError **error);
