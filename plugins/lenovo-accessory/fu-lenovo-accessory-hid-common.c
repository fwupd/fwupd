/*
 * Copyright 2026 Yuchao Li <liyc44@lenovo.com>
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-lenovo-accessory-hid-common.h"

#define FU_LENOVO_ACCESSORY_HID_BUFSZ	  65
#define FU_LENOVO_ACCESSORY_HID_REPORT_ID 0x00

GByteArray *
fu_lenovo_accessory_hid_read(FuLenovoAccessoryImpl *impl, GError **error)
{
	guint8 buf[FU_LENOVO_ACCESSORY_HID_BUFSZ] = {0x0};
	g_autoptr(GByteArray) buf_res = g_byte_array_new();

	if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(impl),
					  buf,
					  sizeof(buf),
					  FU_IOCTL_FLAG_NONE,
					  error))
		return NULL;
	g_byte_array_append(buf_res, buf, sizeof(buf));
	return g_steal_pointer(&buf_res);
}

gboolean
fu_lenovo_accessory_hid_write(FuLenovoAccessoryImpl *impl, GByteArray *buf, GError **error)
{
	g_autoptr(GByteArray) buf_req = g_byte_array_new();

	fu_byte_array_append_uint8(buf_req, FU_LENOVO_ACCESSORY_HID_REPORT_ID);
	g_byte_array_append(buf_req, buf->data, buf->len);
	fu_byte_array_set_size(buf_req, FU_LENOVO_ACCESSORY_HID_BUFSZ, 0x00);
	return fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(impl),
					    buf_req->data,
					    buf_req->len,
					    FU_IOCTL_FLAG_RETRY,
					    error);
}

static gboolean
fu_lenovo_accessory_hid_poll_cb(FuDevice *device, gpointer user_data, GError **error)
{
	GByteArray *buf_rsp = (GByteArray *)user_data;
	FuLenovoStatus status;
	gsize offset = 0x0;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = NULL;

	buf = fu_lenovo_accessory_hid_read(FU_LENOVO_ACCESSORY_IMPL(device), error);
	if (buf == NULL)
		return FALSE;
	offset += 1;
	st_cmd = fu_struct_lenovo_accessory_cmd_parse(buf->data, buf->len, offset, error);
	if (st_cmd == NULL)
		return FALSE;
	status = fu_struct_lenovo_accessory_cmd_get_target_status(st_cmd) & 0x0F;
	if (status == FU_LENOVO_STATUS_COMMAND_BUSY) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "command busy");
		return FALSE;
	}
	if (status != FU_LENOVO_STATUS_COMMAND_SUCCESSFUL) {
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
