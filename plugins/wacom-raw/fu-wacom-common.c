/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupd.h>

#include "fu-wacom-common.h"

gboolean
fu_wacom_common_check_reply(const FuStructWacomRawRequest *st_req,
			    const FuStructWacomRawResponse *st_rsp,
			    GError **error)
{
	if (fu_struct_wacom_raw_response_get_report_id(st_rsp) != FU_WACOM_RAW_BL_REPORT_ID_GET) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "report ID failed, expected 0x%02x, got 0x%02x",
			    (guint)FU_WACOM_RAW_BL_REPORT_ID_GET,
			    fu_struct_wacom_raw_request_get_report_id(st_req));
		return FALSE;
	}
	if (fu_struct_wacom_raw_request_get_cmd(st_req) !=
	    fu_struct_wacom_raw_response_get_cmd(st_rsp)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "cmd failed, expected 0x%02x, got 0x%02x",
			    fu_struct_wacom_raw_request_get_cmd(st_req),
			    fu_struct_wacom_raw_response_get_cmd(st_rsp));
		return FALSE;
	}
	if (fu_struct_wacom_raw_request_get_echo(st_req) !=
	    fu_struct_wacom_raw_response_get_echo(st_rsp)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "echo failed, expected 0x%02x, got 0x%02x",
			    fu_struct_wacom_raw_request_get_echo(st_req),
			    fu_struct_wacom_raw_response_get_echo(st_rsp));
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_wacom_common_rc_set_error(const FuStructWacomRawResponse *st_rsp, GError **error)
{
	FuWacomRawRc rc = fu_struct_wacom_raw_response_get_resp(st_rsp);
	if (rc == FU_WACOM_RAW_RC_OK)
		return TRUE;
	if (rc == FU_WACOM_RAW_RC_BUSY) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "device is busy");
		return FALSE;
	}
	if (rc == FU_WACOM_RAW_RC_MCUTYPE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "MCU type does not match");
		return FALSE;
	}
	if (rc == FU_WACOM_RAW_RC_PID) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "PID does not match");
		return FALSE;
	}
	if (rc == FU_WACOM_RAW_RC_CHECKSUM1) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "checksum1 does not match");
		return FALSE;
	}
	if (rc == FU_WACOM_RAW_RC_CHECKSUM2) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "checksum2 does not match");
		return FALSE;
	}
	if (rc == FU_WACOM_RAW_RC_TIMEOUT) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_TIMED_OUT, "command timed out");
		return FALSE;
	}
	g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "unknown error 0x%02x", rc);
	return FALSE;
}

gboolean
fu_wacom_common_block_is_empty(const guint8 *data, guint16 datasz)
{
	for (guint16 i = 0; i < datasz; i++) {
		if (data[i] != 0xff)
			return FALSE;
	}
	return TRUE;
}
