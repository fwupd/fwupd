/*
 * Copyright 2026 Yuchao Li <liyc44@lenovo.com>
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-lenovo-accessory-hid-common.h"

GByteArray *
fu_lenovo_accessory_hid_read(FuLenovoAccessoryImpl *impl, GError **error)
{
	guint8 buf[FU_LENOVO_ACCESSORY_HID_BUFSZ] = {0x0};
	g_autoptr(GByteArray) buf_res = g_byte_array_new();

	/* GET_REPORT (Feature) over the interface-3 control endpoint; the
	 * report-id is carried in wValue, so the buffer is the raw 64-byte
	 * frame with no report-id prefix */
	if (!fu_hid_device_get_report(FU_HID_DEVICE(impl),
				      FU_LENOVO_ACCESSORY_HID_REPORT_ID,
				      buf,
				      sizeof(buf),
				      FU_LENOVO_ACCESSORY_HID_TIMEOUT,
				      FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error))
		return NULL;
	g_byte_array_append(buf_res, buf, sizeof(buf));
	return g_steal_pointer(&buf_res);
}

gboolean
fu_lenovo_accessory_hid_write(FuLenovoAccessoryImpl *impl, GByteArray *buf, GError **error)
{
	g_autoptr(GByteArray) buf_req = g_byte_array_new();

	/* SET_REPORT (Feature) over the interface-3 control endpoint; no
	 * report-id prefix, frame padded to the fixed 64-byte length */
	g_byte_array_append(buf_req, buf->data, buf->len);
	fu_byte_array_set_size(buf_req, FU_LENOVO_ACCESSORY_HID_BUFSZ, 0x00);
	return fu_hid_device_set_report(FU_HID_DEVICE(impl),
					FU_LENOVO_ACCESSORY_HID_REPORT_ID,
					buf_req->data,
					buf_req->len,
					FU_LENOVO_ACCESSORY_HID_TIMEOUT,
					FU_HID_DEVICE_FLAG_IS_FEATURE |
					    FU_HID_DEVICE_FLAG_RETRY_FAILURE,
					error);
}

static gboolean
fu_lenovo_accessory_hid_poll_cb(FuDevice *device, gpointer user_data, GError **error)
{
	GByteArray *buf_rsp = (GByteArray *)user_data;
	FuLenovoAccessoryStatus status;
	gsize offset = 0x0;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = NULL;

	buf = fu_lenovo_accessory_hid_read(FU_LENOVO_ACCESSORY_IMPL(device), error);
	if (buf == NULL)
		return FALSE;
	st_cmd = fu_struct_lenovo_accessory_cmd_parse(buf->data, buf->len, offset, error);
	if (st_cmd == NULL)
		return FALSE;
	status = fu_struct_lenovo_accessory_cmd_get_target_status(st_cmd) & 0x0F;
	if (status == FU_LENOVO_ACCESSORY_STATUS_COMMAND_BUSY) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "command busy");
		return FALSE;
	}
	if (status != FU_LENOVO_ACCESSORY_STATUS_COMMAND_SUCCESSFUL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "command failed with status 0x%02x",
			    status);
		return FALSE;
	}
	offset += FU_STRUCT_LENOVO_ACCESSORY_CMD_SIZE;

	/* success */
	g_byte_array_append(buf_rsp, buf->data + offset, buf->len - offset);
	return TRUE;
}

GByteArray *
fu_lenovo_accessory_hid_process(FuLenovoAccessoryImpl *impl, GByteArray *buf, GError **error)
{
	g_autoptr(GByteArray) buf_rsp = g_byte_array_new();

	if (!fu_lenovo_accessory_hid_write(impl, buf, error))
		return NULL;
	if (!fu_device_retry_full(FU_DEVICE(impl),
				  fu_lenovo_accessory_hid_poll_cb,
				  5,  /* count */
				  10, /* ms */
				  buf_rsp,
				  error))
		return NULL;
	return g_steal_pointer(&buf_rsp);
}
