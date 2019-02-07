/*
 * Copyright (C) 2018-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <gio/gio.h>

#include "fu-wacom-common.h"

gboolean
fu_wacom_common_check_reply (const FuWacomRawRequest *req,
			     const FuWacomRawResponse *rsp,
			     GError **error)
{
	if (rsp->report_id != FU_WACOM_RAW_BL_REPORT_ID_GET) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "report ID failed, expected 0x%02x, got 0x%02x",
			     (guint) FU_WACOM_RAW_BL_REPORT_ID_GET,
			     req->report_id);
		return FALSE;
	}
	if (req->cmd != rsp->cmd) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "cmd failed, expected 0x%02x, got 0x%02x",
			     req->cmd, rsp->cmd);
		return FALSE;
	}
	if (req->echo != rsp->echo) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "echo failed, expected 0x%02x, got 0x%02x",
			     req->echo, rsp->echo);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_wacom_common_rc_set_error (const FuWacomRawResponse *rsp, GError **error)
{
	if (rsp->resp == FU_WACOM_RAW_RC_OK)
		return TRUE;
	if (rsp->resp == FU_WACOM_RAW_RC_BUSY) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_BUSY,
			     "device is busy");
		return FALSE;
	}
	if (rsp->resp == FU_WACOM_RAW_RC_MCUTYPE) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "MCU type does not match");
		return FALSE;
	}
	if (rsp->resp == FU_WACOM_RAW_RC_PID) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "PID does not match");
		return FALSE;
	}
	if (rsp->resp == FU_WACOM_RAW_RC_CHECKSUM1) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "checksum1 does not match");
		return FALSE;
	}
	if (rsp->resp == FU_WACOM_RAW_RC_CHECKSUM2) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "checksum2 does not match");
		return FALSE;
	}
	if (rsp->resp == FU_WACOM_RAW_RC_TIMEOUT) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_TIMED_OUT,
			     "command timed out");
		return FALSE;
	}
	g_set_error (error,
		     G_IO_ERROR,
		     G_IO_ERROR_FAILED,
		     "unknown error 0x%02x", rsp->resp);
	return FALSE;
}

gboolean
fu_wacom_common_block_is_empty (const guint8 *data, guint16 datasz)
{
	for (guint16 i = 0; i < datasz; i++) {
		if (data[i] != 0xff)
			return FALSE;
	}
	return TRUE;
}
