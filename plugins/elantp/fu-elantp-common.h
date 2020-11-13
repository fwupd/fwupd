/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

#define ETP_CMD_GET_HID_DESCRIPTOR	0x0001
#define ETP_CMD_GET_HARDWARE_ID		0x0100
#define ETP_CMD_GET_MODULE_ID		0x0101
#define ETP_CMD_I2C_FW_CHECKSUM		0x030F
#define ETP_CMD_I2C_FW_VERSION		0x0102
#define ETP_CMD_I2C_IAP			0x0311
#define ETP_CMD_I2C_IAP_CHECKSUM	0x0315
#define ETP_CMD_I2C_IAP_CTRL		0x0310
#define ETP_CMD_I2C_IAP_ICBODY		0x0110
#define ETP_CMD_I2C_IAP_RESET		0x0314
#define ETP_CMD_I2C_IAP_VERSION		0x0111
#define ETP_CMD_I2C_IAP_VERSION_2	0x0110
#define ETP_CMD_I2C_OSM_VERSION		0x0103
#define ETP_CMD_I2C_GET_HID_ID		0x0100
#define ETP_CMD_I2C_IAP_TYPE		0x0304

#define ETP_I2C_IAP_TYPE_REG		0x0040

#define ETP_I2C_ENABLE_REPORT		0x0800

#define ETP_I2C_IAP_RESET		0xF0F0
#define ETP_I2C_MAIN_MODE_ON		(1 << 9)

#define ETP_I2C_IAP_REG_L		0x01
#define ETP_I2C_IAP_REG_H		0x06

#define ETP_FW_IAP_INTF_ERR		(1 << 4)
#define ETP_FW_IAP_PAGE_ERR		(1 << 5)
#define ETP_FW_IAP_CHECK_PW		(1 << 7)
#define ETP_FW_IAP_LAST_FIT		(1 << 9)


#define ELANTP_DELAY_COMPLETE		1200	/* ms */
#define ELANTP_DELAY_RESET		30	/* ms */
#define ELANTP_DELAY_UNLOCK		100	/* ms */
#define ELANTP_DELAY_WRITE_BLOCK	35	/* ms */
#define ELANTP_DELAY_WRITE_BLOCK_512	50	/* ms */

guint16		 fu_elantp_calc_checksum	(const guint8	*data,
						 gsize		 length);
