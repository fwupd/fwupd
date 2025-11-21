/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

#define ETP_I2C_IC13_IAPV5_PW 0x37CA

#define ETP_FW_FORCE_TYPE_ENABLE_BIT 0x1
#define ETP_FW_EEPROM_ENABLE_BIT     0x2

#define ETP_I2C_IAP_TYPE_REG 0x0040

#define ETP_I2C_ENABLE_REPORT 0x0800

#define ETP_I2C_IAP_RESET     0xF0F0
#define ETP_I2C_MAIN_MODE_ON  (1 << 9)
#define ETP_I2C_MAIN_MODE_ON2 (1 << 12)

#define ETP_I2C_IAP_REG_L 0x01
#define ETP_I2C_IAP_REG_H 0x06

#define ETP_FW_IAP_INTF_ERR (1 << 4)
#define ETP_FW_IAP_PAGE_ERR (1 << 5)
#define ETP_FW_IAP_CHECK_PW (1 << 7)
#define ETP_FW_IAP_LAST_FIT (1 << 9)

#define ELANTP_DELAY_COMPLETE	     1200 /* ms */
#define ELANTP_DELAY_RESET	     30	  /* ms */
#define ELANTP_EEPROM_READ_DELAY     100  /* ms */
#define ELANTP_DELAY_UNLOCK	     100  /* ms */
#define ELANTP_DELAY_WRITE_BLOCK     35	  /* ms */
#define ELANTP_DELAY_WRITE_BLOCK_512 50	  /* ms */
