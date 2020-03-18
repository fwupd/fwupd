/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "config.h"

#include <glib-object.h>

#include "fu-device.h"

/* size of the firmware information report */
#define HID_CY_FW_INFO_SIZE	64

/* size of the firmware information report */
#define HID_RQT_CMD_SIZE	8

/**
* vendor request / response report IDs.
*
* The reports are aligned to accomodate the report ID as the first byte.
* The report size does not include this first byte added as part of the
* protocol. The byte information for each report ID includes this first
* byte.
*/
typedef enum
{
	HID_REPORT_ID_CY_FW_INFO = 0xE0,
				/**
				*CY_FW_INFO data report. The report returns
				* information about the device and firmware.
				* Report direction: IN, report size: 63.
				* BYTE[0]    : 0xE0
				* BYTE[1]    : Reserved
				* BYTE[3:2]  :Signature "CY"
				* BYTE[4]    : Current operating mode.
				*      BIT(1:0) - 0 = Bootloader
				*		 1 = FW image 1
				*		 2 = FW image 2
				* BYTE[5]    : Bootloader information.
				*      BIT(0)  - This bit is set if the boot-loader
				*      supports security (SHA2 checksum at boot).
				*      BIT(1)  - This bit is set if the boot-loader
				*      has no flashing interface.
				*      BIT(2)  - This bit is set if the boot-loader
				*      supports application priority feature.
				*      BIT(4:3) - Flash row size information
				*	      0 = Row size of 128 bytes
				*	      1 = Row size of 256 bytes
				* BYTE[6]    : Boot mode reason
				*      BIT(0)  - This bit is set if the firmware
				*      requested a jump to boot-loader
				*      BIT(1)  - Reserved
				*      BIT(2)  - FW image 1 status. Set if invalid.
				*      BIT(3)  - FW image 2 status. Set if invalid.
				*      BIT(5:4) - Application priority setting
				*	      0 = Default priority - most recent image.
				*	      1 = Image1 higher priority.
				*	      2 = Image2 higher priority.
				* BYTE[7]    : Reserved
				* BYTE[11:8] : Silicon ID
				* BYTE[19:12]: Bootloader version
				* BYTE[27:20]: FW image 1 version
				* BYTE[35:28]: FW image 2 version
				* BYTE[39:36]: FW image 1 start address
				* BYTE[43:40]: FW image 2 start address
				* BYTE[51:44]: Device UID
				* BYTE[63:52]: Reserved */
	HID_REPORT_ID_RQT,
				/**
				* HID vendor command report.
				* Report direction: OUT, report size: 7.
				* BYTE[0]    : 0xE1
				* BYTE[1]    : Request CMD
				* BYTE[7:2]  : Command parameters. */
	HID_REPORT_ID_FLASH_WRITE,
				/**
				 *Flash write command report.
				* Report direction: OUT, report size: 131.
				* BYTE[0]    : 0xE2
				* BYTE[1]    : "F"
				* BYTE[3:2]  : Row ID to write data to.
				* BYTE[131:4]: Data to write. */
	HID_REPORT_ID_FLASH_READ,
				/**
				* Flash read command report.
				* Report direction: IN, report size: 131.
				* BYTE[0]    : 0xE3
				* BYTE[1]    : "F"
				* BYTE[3:2]  : Row ID of the data.
				* BYTE[131:4]: Data read from flash. */
	HID_REPORT_ID_CUSTOMER_INFO
				/**
				*Customer information data report.
				* Report direction: IN, report size: 32.
				* BYTE[0]    : 0xE4
				* BYTE[32:1] : Customer information data. */

} HidReportId;

/* hid vendor request commands for HID_REPORT_ID_RQT_CMD report id */
typedef enum
{
	HID_RQT_CMD_RESERVED = 0,	/* reserved command id */
	HID_RQT_CMD_JUMP,
					/**
					 * Jump	request.
					 * PARAM[0]  : Signature
					 * 'J' = Jump to boot-loader.
					 * 'R' = Reset device.
					 * 'A' = Jump to alternate image.
					 * PARAM[5:1]: Reserved. */
	HID_RQT_CMD_ENTER_FLASHING,
					 /**
					 * Enter flashing mode request.
					 * PARAM[0]  : Signature
					 * 'P' = Enable flashing mode.
					 * Others = Disable flashing mode.
					 * PARAM[5:1]: Reserved. */
	HID_RQT_CMD_SET_READ_ROW,
					/**
					 * Set flash read row request.
					 * PARAM[1:0]: Row ID
					 * PARAM[5:2]: Reserved. */
	HID_RQT_CMD_VALIDATE_FW,
					/**
					 * Validate firmware request.
					 * PARAM[0]  : Firmware	image number to	validate.
					 * PARAM[5:1]: Reserved. */
	HID_RQT_CMD_SET_APP_PRIORITY,
					/**
					 * Set application priority setting.
					 * PARAM[0]  : Signature 'F'
					 * PARAM[1]  : Priorty setting (0, 1 or	2).
					 * PARAM[5:2]: Reserved. */
	HID_RQT_CMD_I2C_BRIDGE_CTRL,
					/**
					 * The request enables/disables	USB-HID	based USB-I2C
					 * master bridge interface
					 * PARAM[0]  : Signature
					 * 'B' = Enable	USB-I2C	bridge mode.
					 * Others = Disable USB-I2C bridge mode	if already enabled.
					 * PARAM[5:1]: Reserved	*/
	HID_RQT_CMD_DP_HUB_CTRL,
	HID_RQT_CMD_DP_LP_CTRL

} HidRqtCmd;

gboolean	 fu_ccgx_hid_enable_mfg_mode		(FuDevice	*self,
							 gint		 inf_num,
							 GError		**error);
gboolean	 fu_ccgx_hid_enable_usb_bridge_mode	(FuDevice	*self,
							 gint		 inf_num,
							 GError		**error);
