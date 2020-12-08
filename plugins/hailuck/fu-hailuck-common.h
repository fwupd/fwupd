/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#define FU_HAILUCK_REPORT_ID_SHORT			0x05
#define FU_HAILUCK_REPORT_ID_LONG			0x06

#define FU_HAILUCK_CMD_ERASE				0x45
#define FU_HAILUCK_CMD_READ_BLOCK_START			0x52
#define FU_HAILUCK_CMD_ATTACH				0x55	/* guessed */
#define FU_HAILUCK_CMD_WRITE_BLOCK_START		0x57
#define FU_HAILUCK_CMD_READ_BLOCK			0x72
#define FU_HAILUCK_CMD_DETACH				0x75	/* guessed */
#define FU_HAILUCK_CMD_WRITE_BLOCK			0x77
#define FU_HAILUCK_CMD_GET_STATUS			0xA1
#define FU_HAILUCK_CMD_WRITE_TP				0xD0	/* guessed */
#define FU_HAILUCK_CMD_I2C_CHECK_CHECKSUM		0xF0
#define FU_HAILUCK_CMD_I2C_ENTER_BL			0xF1
#define FU_HAILUCK_CMD_I2C_ERASE			0xF2
#define FU_HAILUCK_CMD_I2C_PROGRAM			0xF3
#define FU_HAILUCK_CMD_I2C_VERIFY_BLOCK			0xF4
#define FU_HAILUCK_CMD_I2C_VERIFY_CHECKSUM		0xF5
#define FU_HAILUCK_CMD_I2C_PROGRAMPASS			0xF6
#define FU_HAILUCK_CMD_I2C_END_PROGRAM			0xF7

const gchar	*fu_hailuck_cmd_to_string		(guint8	 cmd);
