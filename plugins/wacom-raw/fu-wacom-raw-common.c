/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-wacom-raw-common.h"

gboolean
fu_wacom_raw_common_check_reply(const FuStructWacomRawRequest *st_req,
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
fu_wacom_raw_common_rc_set_error(const FuStructWacomRawResponse *st_rsp, GError **error)
{
	FuWacomRawRc rc = fu_struct_wacom_raw_response_get_resp(st_rsp);
	const FuErrorMapEntry entries[] = {
	    {FU_WACOM_RAW_RC_OK, FWUPD_ERROR_LAST, NULL},
	    {FU_WACOM_RAW_RC_BUSY, FWUPD_ERROR_BUSY, NULL},
	    {FU_WACOM_RAW_RC_MCUTYPE, FWUPD_ERROR_INVALID_DATA, "MCU type does not match"},
	    {FU_WACOM_RAW_RC_PID, FWUPD_ERROR_INVALID_DATA, "PID does not match"},
	    {FU_WACOM_RAW_RC_CHECKSUM1, FWUPD_ERROR_INVALID_DATA, "checksum1 does not match"},
	    {FU_WACOM_RAW_RC_CHECKSUM2, FWUPD_ERROR_INVALID_DATA, "checksum2 does not match"},
	    {FU_WACOM_RAW_RC_TIMEOUT, FWUPD_ERROR_TIMED_OUT, NULL},
	};
	return fu_error_map_entry_to_gerror(rc, entries, G_N_ELEMENTS(entries), error);
}

gboolean
fu_wacom_raw_common_block_is_empty(const guint8 *data, guint16 datasz)
{
	for (guint16 i = 0; i < datasz; i++) {
		if (data[i] != 0xff)
			return FALSE;
	}
	return TRUE;
}
