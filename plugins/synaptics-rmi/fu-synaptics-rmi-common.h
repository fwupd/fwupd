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

#define PACKAGE_ID_BYTES				4
#define CONFIG_ID_BYTES					4
#define BUILD_ID_BYTES					3

#define RMI_F01_CMD_DEVICE_RESET			1
#define RMI_F01_DEFAULT_RESET_DELAY_MS			100

typedef enum {
	HID_RMI4_MODE_MOUSE				= 0,
	HID_RMI4_MODE_ATTN_REPORTS			= 1,
	HID_RMI4_MODE_NO_PACKED_ATTN_REPORTS		= 2,
} FuSynapticsRmiHidMode;

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
