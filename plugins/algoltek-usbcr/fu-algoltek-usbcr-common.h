/*
 * Copyright 2024 Algoltek, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib-object.h>

#define FU_AG_USBCR_MAX_CDB_LEN	      16
#define FU_AG_USBCR_MAX_BUFFER_SIZE   512
#define FU_AG_USBCR_SENSE_BUFFER_SIZE 18

#define FU_AG_USBCR_IOCTL_TIMEOUT_MS 20000

#define FU_AG_USBCR_RD_WR_RAM	0x84
#define FU_AG_USBCR_RD_WR_XDATA 0x03

#define FU_AG_SPIFLASH_VALID 0x40
#define FU_AG_SPECIFY_SPI_CMD_SIG_1 0x05
#define FU_AG_SPECIFY_SPI_CMD_SIG_2 0x8F
#define FU_AG_SPECIFY_EEPROM_TYPE_TAG 0xA5

#define FU_AG_BLOCK_MODE_DISEN 0x00
#define FU_AG_BLOCK_MODE_EN 0x01
