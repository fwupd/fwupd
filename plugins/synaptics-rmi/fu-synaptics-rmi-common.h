/*
 * Copyright (C) 2012-2014 Andrew Duggan
 * Copyright (C) 2012-2014 Synaptics Inc.
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define FU_SYNAPTICS_RMI_DEVICE_VID			0x06cb

#define RMI_PRODUCT_ID_LENGTH				10

#define RMI_DEVICE_PDT_ENTRY_SIZE			6
#define RMI_DEVICE_PAGE_SELECT_REGISTER			0xff
#define RMI_DEVICE_MAX_PAGE				0xff
#define RMI_DEVICE_PAGE_SIZE				0x100
#define RMI_DEVICE_PAGE_SCAN_START			0x00e9
#define RMI_DEVICE_PAGE_SCAN_END			0x0005
#define RMI_DEVICE_F01_BASIC_QUERY_LEN			11
#define RMI_DEVICE_F01_QRY5_YEAR_MASK			0x1f
#define RMI_DEVICE_F01_QRY6_MONTH_MASK			0x0f
#define RMI_DEVICE_F01_QRY7_DAY_MASK			0x1f

#define RMI_DEVICE_F01_QRY1_HAS_LTS			(1 << 2)
#define RMI_DEVICE_F01_QRY1_HAS_SENSOR_ID		(1 << 3)
#define RMI_DEVICE_F01_QRY1_HAS_CHARGER_INP		(1 << 4)
#define RMI_DEVICE_F01_QRY1_HAS_PROPS_2			(1 << 7)

#define RMI_DEVICE_F01_LTS_RESERVED_SIZE		19

#define RMI_DEVICE_F01_QRY42_DS4_QUERIES		(1 << 0)
#define RMI_DEVICE_F01_QRY42_MULTI_PHYS			(1 << 1)

#define RMI_DEVICE_F01_QRY43_01_PACKAGE_ID		(1 << 0)
#define RMI_DEVICE_F01_QRY43_01_BUILD_ID		(1 << 1)

#define PACKAGE_ID_BYTES				4	/* bytes */
#define CONFIG_ID_BYTES					4	/* bytes */
#define BUILD_ID_BYTES					3	/* bytes */

#define RMI_BOOTLOADER_ID_SIZE				2	/* bytes */

#define RMI_F01_CMD_DEVICE_RESET			1
#define RMI_F01_DEFAULT_RESET_DELAY_MS			100

typedef enum {
	HID_RMI4_MODE_MOUSE				= 0,
	HID_RMI4_MODE_ATTN_REPORTS			= 1,
	HID_RMI4_MODE_NO_PACKED_ATTN_REPORTS		= 2,
} FuSynapticsRmiHidMode;

typedef enum {
	CMD_V7_IDLE					= 0x00,
	CMD_V7_ENTER_BL,
	CMD_V7_READ,
	CMD_V7_WRITE,
	CMD_V7_ERASE,
	CMD_V7_ERASE_AP,
	CMD_V7_SENSOR_ID,
} RmiV7FlashCommand;

typedef enum {
	NONE_PARTITION					= 0x00,
	BOOTLOADER_PARTITION				= 0x01,
	DEVICE_CONFIG_PARTITION,
	FLASH_CONFIG_PARTITION,
	MANUFACTURING_BLOCK_PARTITION,
	GUEST_SERIALIZATION_PARTITION,
	GLOBAL_PARAMETERS_PARTITION,
	CORE_CODE_PARTITION,
	CORE_CONFIG_PARTITION,
	GUEST_CODE_PARTITION,
	DISPLAY_CONFIG_PARTITION,
	EXTERNAL_TOUCH_AFE_CONFIG_PARTITION,
	UTILITY_PARAMETER_PARTITION,
} RmiV7PartitionId;

#define RMI_F34_QUERY_SIZE				7
#define RMI_F34_HAS_NEW_REG_MAP				(1 << 0)
#define RMI_F34_IS_UNLOCKED				(1 << 1)
#define RMI_F34_HAS_CONFIG_ID				(1 << 2)
#define RMI_F34_BLOCK_SIZE_OFFSET			1
#define RMI_F34_FW_BLOCKS_OFFSET			3
#define RMI_F34_CONFIG_BLOCKS_OFFSET			5

#define RMI_F34_BLOCK_SIZE_V1_OFFSET			0
#define RMI_F34_FW_BLOCKS_V1_OFFSET			0
#define RMI_F34_CONFIG_BLOCKS_V1_OFFSET			2

#define RMI_F34_BLOCK_DATA_OFFSET			2
#define RMI_F34_BLOCK_DATA_V1_OFFSET			1

#define RMI_F34_COMMAND_MASK				0x0f
#define RMI_F34_STATUS_MASK				0x07
#define RMI_F34_STATUS_SHIFT				4
#define RMI_F34_ENABLED_MASK				0x80

#define RMI_F34_COMMAND_V1_MASK				0x3f
#define RMI_F34_STATUS_V1_MASK				0x3f
#define RMI_F34_ENABLED_V1_MASK				0x80

#define RMI_F34_WRITE_FW_BLOCK				0x02
#define RMI_F34_ERASE_ALL				0x03
#define RMI_F34_WRITE_LOCKDOWN_BLOCK			0x04
#define RMI_F34_WRITE_CONFIG_BLOCK			0x06
#define RMI_F34_ENABLE_FLASH_PROG			0x0f

#define RMI_F34_ENABLE_WAIT_MS				300		/* ms */
#define RMI_F34_ERASE_WAIT_MS				(5 * 1000)	/* ms */
#define RMI_F34_ERASE_V8_WAIT_MS			(10000)		/* ms */
#define RMI_F34_IDLE_WAIT_MS				500		/* ms */

#define RMI_DEVICE_DEFAULT_TIMEOUT			2000

/*
 * msleep mode controls power management on the device and affects all
 * functions of the device.
 */
#define RMI_F01_CTRL0_SLEEP_MODE_MASK			0x03

#define RMI_SLEEP_MODE_NORMAL				0x00
#define RMI_SLEEP_MODE_SENSOR_SLEEP			0x01
#define RMI_SLEEP_MODE_RESERVED0			0x02
#define RMI_SLEEP_MODE_RESERVED1			0x03

/*
 * This bit disables whatever sleep mode may be selected by the sleep_mode
 * field and forces the device to run at full power without sleeping.
 */
#define RMI_F01_CRTL0_NOSLEEP_BIT			(1 << 2)

typedef struct {
	guint16		 query_base;
	guint16		 command_base;
	guint16		 control_base;
	guint16		 data_base;
	guint8		 interrupt_source_count;
	guint8		 function_number;
	guint8		 function_version;
	guint8		 interrupt_reg_num;
	guint8		 interrupt_mask;
} FuSynapticsRmiFunction;

guint32		 fu_synaptics_rmi_generate_checksum	(const guint8	*data,
							 gsize		 len);
FuSynapticsRmiFunction *fu_synaptics_rmi_function_parse	(GByteArray	*buf,
							 guint16	 page_base,
							 guint		 interrupt_count,
							 GError		**error);

G_END_DECLS
