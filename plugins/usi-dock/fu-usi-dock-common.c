/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-usi-dock-common.h"

const gchar *
fu_usi_dock_spi_state_to_string(guint8 val)
{
	if (val == SPI_STATE_NONE)
		return "none";
	if (val == SPI_STATE_SWITCH_SUCCESS)
		return "switch-success";
	if (val == SPI_STATE_SWITCH_FAIL)
		return "switch-fail";
	if (val == SPI_STATE_CMD_SUCCESS)
		return "cmd-success";
	if (val == SPI_STATE_CMD_FAIL)
		return "cmd-fail";
	if (val == SPI_STATE_RW_SUCCESS)
		return "rw-success";
	if (val == SPI_STATE_RW_FAIL)
		return "rw-fail";
	if (val == SPI_STATE_READY)
		return "ready";
	if (val == SPI_STATE_BUSY)
		return "busy";
	if (val == SPI_STATE_TIMEOUT)
		return "timeout";
	if (val == SPI_STATE_FLASH_FOUND)
		return "flash-found";
	if (val == SPI_STATE_FLASH_NOT_FOUND)
		return "flash-not-found";
	return NULL;
}
