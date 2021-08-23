/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#define FU_EP963_FIRMWARE_SIZE 0x1f000

#define FU_EP963_TRANSFER_BLOCK_SIZE 0x200 /* 512 */
#define FU_EP963_TRANSFER_CHUNK_SIZE 0x04
#define FU_EP963_FEATURE_ID1_SIZE    0x08

#define FU_EP963_USB_CONTROL_ID 0x01

#define FU_EP963_ICP_ENTER	0x40
#define FU_EP963_ICP_EXIT	0x82
#define FU_EP963_ICP_BANK	0x83
#define FU_EP963_ICP_ADDRESS	0x84
#define FU_EP963_ICP_READBLOCK	0x85
#define FU_EP963_ICP_WRITEBLOCK 0x86
#define FU_EP963_ICP_MCUID	0x87
#define FU_EP963_ICP_DONE	0x5A

#define FU_EP963_OPCODE_SMBUS_READ		0x01
#define FU_EP963_OPCODE_ERASE_SPI		0x02
#define FU_EP963_OPCODE_RESET_BLOCK_INDEX	0x03
#define FU_EP963_OPCODE_WRITE_BLOCK_DATA	0x04
#define FU_EP963_OPCODE_PROGRAM_SPI_BLOCK	0x05
#define FU_EP963_OPCODE_PROGRAM_SPI_FINISH	0x06
#define FU_EP963_OPCODE_GET_SPI_CHECKSUM	0x07
#define FU_EP963_OPCODE_PROGRAM_EP_FLASH	0x08
#define FU_EP963_OPCODE_GET_EP_CHECKSUM		0x09
#define FU_EP963_OPCODE_START_THROW_PAGE	0x0B
#define FU_EP963_OPCODE_GET_EP_SITE_TYPE	0x0C
#define FU_EP963_OPCODE_COMMAND_VERSION		0x10
#define FU_EP963_OPCODE_COMMAND_STATUS		0x20
#define FU_EP963_OPCODE_SUBMCU_ENTER_ICP	0x30
#define FU_EP963_OPCODE_SUBMCU_RESET_BLOCK_IDX	0x31
#define FU_EP963_OPCODE_SUBMCU_WRITE_BLOCK_DATA 0x32
#define FU_EP963_OPCODE_SUBMCU_PROGRAM_BLOCK	0x33
#define FU_EP963_OPCODE_SUBMCU_PROGRAM_FINISHED 0x34

#define FU_EP963_UF_CMD_VERSION	 0x00
#define FU_EP963_UF_CMD_ENTERISP 0x01
#define FU_EP963_UF_CMD_PROGRAM	 0x02
#define FU_EP963_UF_CMD_READ	 0x03
#define FU_EP963_UF_CMD_MODE	 0x04

/* byte 0x02 */
#define FU_EP963_USB_STATE_READY   0x00
#define FU_EP963_USB_STATE_BUSY	   0x01
#define FU_EP963_USB_STATE_FAIL	   0x02
#define FU_EP963_USB_STATE_UNKNOWN 0xff

/* byte 0x07 */
#define FU_EP963_SMBUS_ERROR_NONE	 0x00
#define FU_EP963_SMBUS_ERROR_ADDRESS	 0x01
#define FU_EP963_SMBUS_ERROR_NO_ACK	 0x02
#define FU_EP963_SMBUS_ERROR_ARBITRATION 0x04
#define FU_EP963_SMBUS_ERROR_COMMAND	 0x08
#define FU_EP963_SMBUS_ERROR_TIMEOUT	 0x10
#define FU_EP963_SMBUS_ERROR_BUSY	 0x20

const gchar *
fu_ep963x_smbus_strerror(guint8 val);
