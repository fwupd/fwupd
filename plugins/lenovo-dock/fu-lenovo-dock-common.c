/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-lenovo-dock-common.h"

const gchar *
fu_lenovo_dock_idx_to_string(guint8 val)
{
	return NULL;
}

const gchar *
fu_lenovo_dock_spi_state_to_string(guint8 val)
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

/* returned TRUE - got a expected report*/
gboolean
fu_lenovo_dock_rx_filter(guint8 cmd, const guint8 *buf)
{
	if (cmd != buf[1])
		return TRUE;

	switch (cmd) {
	case USBUID_ISP_DEVICE_CMD_MCU_JUMP2BOOT:
		g_debug("got correct jump");
		break;
	case USBUID_ISP_INTERNAL_FW_CMD_TARGET_CHECKSUM:
		if (buf[6] == FIRMWARE_IDX_AUDIO) {
			g_debug("got a quick jump at audio updates");
		} else {
			g_debug("got a ignored report");
			return FALSE;
		}
		break;
	default:
		break;
	}

	return TRUE;
}
