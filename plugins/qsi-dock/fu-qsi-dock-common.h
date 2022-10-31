/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2022 Kevin Chen <hsinfu.chen@qsitw.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#define Report_ID 5

#define TX_ISP_LENGTH	       61
#define TX_ISP_LENGTH_MCU      60
#define EXTERN_FLASH_PAGE_SIZE 256

#define FIRMWARE_IDX_NONE   0x00
#define FIRMWARE_IDX_DMC_PD 0x01
#define FIRMWARE_IDX_DP	    0x02
#define FIRMWARE_IDX_TBT4   0x04
#define FIRMWARE_IDX_USB3   0x08
#define FIRMWARE_IDX_USB2   0x10
#define FIRMWARE_IDX_AUDIO  0x20
#define FIRMWARE_IDX_I225   0x40
#define FIRMWARE_IDX_MCU    0x80

typedef struct {
	guint8 DMC[5];
	guint8 PD[5];
	guint8 DP5x[5];
	guint8 DP6x[5];
	guint8 TBT4[5];
	guint8 USB3[5];
	guint8 USB2[5];
	guint8 AUDIO[5];
	guint8 I255[5];
	guint8 MCU[2];
	guint8 bcdVersion[2];
} IspVersionInMcu_t;

typedef enum {
	CmdPrimary_CMD_BOOT = 0x11,
	CmdPrimary_CMD_SYSTEM = 0x31,
	CmdPrimary_CMD_MCU = 0x51,
	CmdPrimary_CMD_SPI = 0x61,
	CmdPrimary_CMD_I2C_VMM = 0x71,
	CmdPrimary_CMD_I2C_CCG = 0x81,

	CmdPrimary_Mass_MCU = 0xC0,
	CmdPrimary_Mass_SPI,
	CmdPrimary_Mass_I2C_VMM,
	CmdPrimary_Mass_I2C_CY,
} Hid_CmdPrimary;

typedef enum {
	CmdSecond_CMD_DEVICE_STATUS,
	CmdSecond_CMD_SET_BOOT_MODE,
	CmdSecond_CMD_SET_AP_MODE,
	CmdSecond_CMD_ERASE_AP_PAGE,
	CmdSecond_CMD_CHECKSUM,
	CmdSecond_CMD_DEVICE_VERSION,
	CmdSecond_CMD_DEVICE_PCB_VERSION,
	CmdSecond_CMD_DEVICE_SN,
} Hid_CmdSecond_0x51;

typedef enum {
	CmdSecond_SPI_External_Flash_Ini,
	CmdSecond_SPI_External_Flash_Erase,
	CmdSecond_SPI_External_Flash_Checksum,

} Hid_CmdSecond_0x61;

const gchar *
fu_qsi_dock_idx_to_string(guint8 val);
const gchar *
fu_qsi_dock_spi_state_to_string(guint8 val);
