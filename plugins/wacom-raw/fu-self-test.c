/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-wacom-raw-common.h"

static FuStructWacomRawResponse *
fu_wacom_raw_self_test_build_response(guint8 report_id, guint8 cmd, guint8 echo, guint8 resp)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	fu_byte_array_set_size(buf, FU_STRUCT_WACOM_RAW_RESPONSE_SIZE, 0x0);
	buf->data[0] = report_id;
	buf->data[1] = cmd;
	buf->data[2] = echo;
	buf->data[3] = resp;
	return fu_struct_wacom_raw_response_parse(buf->data, buf->len, 0x0, NULL);
}

static void
fu_wacom_raw_common_block_is_empty_func(void)
{
	const guint8 empty[] = {0xff, 0xff, 0xff, 0xff};
	const guint8 nonempty[] = {0xff, 0xff, 0x00, 0xff};
	g_assert_true(fu_wacom_raw_common_block_is_empty(empty, sizeof(empty)));
	g_assert_false(fu_wacom_raw_common_block_is_empty(nonempty, sizeof(nonempty)));
}

static void
fu_wacom_raw_common_check_reply_func(void)
{
	gboolean ret;
	g_autoptr(FuStructWacomRawRequest) st_req = fu_struct_wacom_raw_request_new();
	g_autoptr(FuStructWacomRawResponse) st_rsp_ok = NULL;
	g_autoptr(FuStructWacomRawResponse) st_rsp_badid = NULL;
	g_autoptr(FuStructWacomRawResponse) st_rsp_badcmd = NULL;
	g_autoptr(FuStructWacomRawResponse) st_rsp_badecho = NULL;
	g_autoptr(GError) error = NULL;

	fu_struct_wacom_raw_request_set_report_id(st_req, FU_WACOM_RAW_BL_REPORT_ID_GET);
	fu_struct_wacom_raw_request_set_cmd(st_req, 0x12);
	fu_struct_wacom_raw_request_set_echo(st_req, 0x34);

	/* success */
	st_rsp_ok =
	    fu_wacom_raw_self_test_build_response(FU_WACOM_RAW_BL_REPORT_ID_GET, 0x12, 0x34, 0x0);
	g_assert_nonnull(st_rsp_ok);
	ret = fu_wacom_raw_common_check_reply(st_req, st_rsp_ok, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* wrong report ID */
	st_rsp_badid =
	    fu_wacom_raw_self_test_build_response(FU_WACOM_RAW_BL_REPORT_ID_SET, 0x12, 0x34, 0x0);
	g_assert_nonnull(st_rsp_badid);
	ret = fu_wacom_raw_common_check_reply(st_req, st_rsp_badid, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);
	g_clear_error(&error);

	/* wrong cmd */
	st_rsp_badcmd =
	    fu_wacom_raw_self_test_build_response(FU_WACOM_RAW_BL_REPORT_ID_GET, 0x99, 0x34, 0x0);
	g_assert_nonnull(st_rsp_badcmd);
	ret = fu_wacom_raw_common_check_reply(st_req, st_rsp_badcmd, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);
	g_clear_error(&error);

	/* wrong echo */
	st_rsp_badecho =
	    fu_wacom_raw_self_test_build_response(FU_WACOM_RAW_BL_REPORT_ID_GET, 0x12, 0x99, 0x0);
	g_assert_nonnull(st_rsp_badecho);
	ret = fu_wacom_raw_common_check_reply(st_req, st_rsp_badecho, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);
}

static void
fu_wacom_raw_common_rc_set_error_func(void)
{
	gboolean ret;
	g_autoptr(FuStructWacomRawResponse) st_ok = NULL;
	g_autoptr(FuStructWacomRawResponse) st_busy = NULL;
	g_autoptr(FuStructWacomRawResponse) st_mcu = NULL;
	g_autoptr(GError) error = NULL;

	/* success */
	st_ok = fu_wacom_raw_self_test_build_response(FU_WACOM_RAW_BL_REPORT_ID_GET,
						      0x0,
						      0x0,
						      FU_WACOM_RAW_RC_OK);
	ret = fu_wacom_raw_common_rc_set_error(st_ok, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* busy */
	st_busy = fu_wacom_raw_self_test_build_response(FU_WACOM_RAW_BL_REPORT_ID_GET,
							0x0,
							0x0,
							FU_WACOM_RAW_RC_BUSY);
	ret = fu_wacom_raw_common_rc_set_error(st_busy, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_BUSY);
	g_assert_false(ret);
	g_clear_error(&error);

	/* MCU type mismatch */
	st_mcu = fu_wacom_raw_self_test_build_response(FU_WACOM_RAW_BL_REPORT_ID_GET,
						       0x0,
						       0x0,
						       FU_WACOM_RAW_RC_MCUTYPE);
	ret = fu_wacom_raw_common_rc_set_error(st_mcu, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/wacom-raw/common{block-is-empty}",
			fu_wacom_raw_common_block_is_empty_func);
	g_test_add_func("/wacom-raw/common{check-reply}", fu_wacom_raw_common_check_reply_func);
	g_test_add_func("/wacom-raw/common{rc-set-error}", fu_wacom_raw_common_rc_set_error_func);
	return g_test_run();
}
