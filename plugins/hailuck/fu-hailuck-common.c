/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-hailuck-common.h"

const gchar *
fu_hailuck_cmd_to_string (guint8 cmd)
{
	if (cmd == FU_HAILUCK_CMD_ERASE)
		return "erase";
	if (cmd == FU_HAILUCK_CMD_READ_BLOCK_START)
		return "read-block-start";
	if (cmd == FU_HAILUCK_CMD_WRITE_BLOCK_START)
		return "write-block-start";
	if (cmd == FU_HAILUCK_CMD_READ_BLOCK)
		return "read-block";
	if (cmd == FU_HAILUCK_CMD_WRITE_BLOCK)
		return "write-block";
	if (cmd == FU_HAILUCK_CMD_GET_STATUS)
		return "get-status";
	if (cmd == FU_HAILUCK_CMD_DETACH)
		return "detach";
	if (cmd == FU_HAILUCK_CMD_ATTACH)
		return "attach";
	if (cmd == FU_HAILUCK_CMD_WRITE_TP)
		return "write-tp";
	if (cmd == FU_HAILUCK_CMD_I2C_CHECK_CHECKSUM)
		return "i2c-check-checksum";
	if (cmd == FU_HAILUCK_CMD_I2C_ENTER_BL)
		return "i2c-enter-bl";
	if (cmd == FU_HAILUCK_CMD_I2C_ERASE)
		return "i2c-erase";
	if (cmd == FU_HAILUCK_CMD_I2C_PROGRAM)
		return "i2c-program";
	if (cmd == FU_HAILUCK_CMD_I2C_VERIFY_BLOCK)
		return "i2c-verify-block";
	if (cmd == FU_HAILUCK_CMD_I2C_VERIFY_CHECKSUM)
		return "i2c-verify-checksum";
	if (cmd == FU_HAILUCK_CMD_I2C_PROGRAMPASS)
		return "i2c-programpass";
	if (cmd == FU_HAILUCK_CMD_I2C_END_PROGRAM)
		return "i2c-end-program";
	return NULL;
}
