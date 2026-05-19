/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-st-stm32-common.h"

FuStStm32Cmd
fu_st_stm32_cmd_base(FuStStm32Cmd cmd)
{
	struct {
		FuStStm32Cmd cmd;
		FuStStm32Cmd cmd_base;
	} cmd_map[] = {
	    {FU_ST_STM32_CMD_WRITE_MEMORY_NS, FU_ST_STM32_CMD_WRITE_MEMORY},
	    {FU_ST_STM32_CMD_ERASE_EXTENDED, FU_ST_STM32_CMD_ERASE},
	    {FU_ST_STM32_CMD_ERASE_EXTENDED_NS, FU_ST_STM32_CMD_ERASE},
	    {FU_ST_STM32_CMD_WRITE_UNPROTECT_NS, FU_ST_STM32_CMD_WRITE_UNPROTECT},
	    {FU_ST_STM32_CMD_WRITE_PROTECT_NS, FU_ST_STM32_CMD_WRITE_PROTECT},
	    {FU_ST_STM32_CMD_READ_PROTECT_NS, FU_ST_STM32_CMD_READ_PROTECT},
	    {FU_ST_STM32_CMD_READ_UNPROTECT_NS, FU_ST_STM32_CMD_READ_UNPROTECT},
	};
	for (guint i = 0; G_N_ELEMENTS(cmd_map); i++) {
		if (cmd == cmd_map[i].cmd)
			return cmd_map[i].cmd_base;
	}
	return cmd;
}

/* this is nonstandard CRC: STM32 computes the checksum on units of 32 bits word and swaps the
 * bytes of the word before the computation */
#define CRCPOLY_BE  0x04c11db7
#define CRC_MSBMASK 0x80000000

gboolean
fu_st_stm32_crc(guint32 *crc_inout, guint8 *buf, gsize len, GError **error)
{
	guint32 crc = *crc_inout;
	guint32 data;

	if (len % 4 > 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "buffer length must be multiple of 4 bytes");
		return FALSE;
	}
	while (len) {
		data = *buf++;
		data |= *buf++ << 8;
		data |= *buf++ << 16;
		data |= *buf++ << 24;
		len -= 4;
		crc ^= data;
		for (guint i = 0; i < 32; i++)
			if (crc & CRC_MSBMASK)
				crc = (crc << 1) ^ CRCPOLY_BE;
			else
				crc = (crc << 1);
	}

	/* success */
	*crc_inout = crc;
	return TRUE;
}
