/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-dell-kestrel-ec-hid.h"

gboolean
fu_dell_kestrel_ec_hid_write(FuDellKestrelEc *self, GByteArray *buf, GError **error)
{
	return fu_hid_device_set_report(FU_HID_DEVICE(self),
					0x0,
					buf->data,
					buf->len,
					FU_DELL_KESTREL_EC_HID_TIMEOUT,
					FU_HID_DEVICE_FLAG_RETRY_FAILURE,
					error);
}

GBytes *
fu_dell_kestrel_ec_hid_fwup_pkg_new(FuChunk *chk,
				    gsize fw_size,
				    FuDellKestrelEcDevType dev_type,
				    guint8 dev_identifier)
{
	FuStructEcHidFwUpdatePkg *fwbuf = fu_struct_ec_hid_fw_update_pkg_new();
	gsize chk_datasz = fu_chunk_get_data_sz(chk);

	/* header */
	fu_struct_ec_hid_fw_update_pkg_set_cmd(fwbuf, FU_DELL_KESTREL_EC_HID_CMD_FWUPDATE);
	fu_struct_ec_hid_fw_update_pkg_set_ext(fwbuf, FU_DELL_KESTREL_EC_HID_EXT_FWUPDATE);
	fu_struct_ec_hid_fw_update_pkg_set_chunk_sz(fwbuf, 7 + chk_datasz); // 7 = sizeof(command)

	/* command */
	fu_struct_ec_hid_fw_update_pkg_set_sub_cmd(fwbuf, FU_DELL_KESTREL_EC_HID_SUBCMD_FWUPDATE);
	fu_struct_ec_hid_fw_update_pkg_set_dev_type(fwbuf, dev_type);
	fu_struct_ec_hid_fw_update_pkg_set_dev_identifier(fwbuf, dev_identifier);
	fu_struct_ec_hid_fw_update_pkg_set_fw_sz(fwbuf, fw_size);

	/* data */
	fu_byte_array_append_bytes(fwbuf, fu_chunk_get_bytes(chk));

	return g_bytes_new(fwbuf->data, fwbuf->len);
}

static gboolean
fu_dell_kestrel_ec_hid_set_report_cb(FuDevice *self, gpointer user_data, GError **error)
{
	guint8 *outbuffer = (guint8 *)user_data;
	return fu_hid_device_set_report(FU_HID_DEVICE(self),
					0x0,
					outbuffer,
					192,
					FU_DELL_KESTREL_EC_HID_TIMEOUT * 3,
					FU_HID_DEVICE_FLAG_NONE,
					error);
}

static gboolean
fu_dell_kestrel_ec_hid_set_report(FuDellKestrelEc *self, guint8 *outbuffer, GError **error)
{
	return fu_device_retry(FU_DEVICE(self),
			       fu_dell_kestrel_ec_hid_set_report_cb,
			       FU_DELL_KESTREL_EC_HID_MAX_RETRIES,
			       outbuffer,
			       error);
}

static gboolean
fu_dell_kestrel_ec_hid_get_report_cb(FuDevice *self, gpointer user_data, GError **error)
{
	guint8 *inbuffer = (guint8 *)user_data;
	return fu_hid_device_get_report(FU_HID_DEVICE(self),
					0x0,
					inbuffer,
					192,
					FU_DELL_KESTREL_EC_HID_TIMEOUT,
					FU_HID_DEVICE_FLAG_NONE,
					error);
}

static gboolean
fu_dell_kestrel_ec_hid_get_report(FuDellKestrelEc *self, guint8 *inbuffer, GError **error)
{
	return fu_device_retry(FU_DEVICE(self),
			       fu_dell_kestrel_ec_hid_get_report_cb,
			       FU_DELL_KESTREL_EC_HID_MAX_RETRIES,
			       inbuffer,
			       error);
}

gboolean
fu_dell_kestrel_ec_hid_i2c_write(FuDellKestrelEc *self, GByteArray *cmd_buf, GError **error)
{
	FuStructEcHidCmdBuffer *buf = fu_struct_ec_hid_cmd_buffer_new();
	g_return_val_if_fail(cmd_buf->len <= FU_DELL_KESTREL_HIDI2C_MAX_WRITE, FALSE);

	fu_struct_ec_hid_cmd_buffer_set_cmd(buf, FU_DELL_KESTREL_EC_USB_HID_CMD_WRITE_DATA);
	fu_struct_ec_hid_cmd_buffer_set_ext(buf, FU_DELL_KESTREL_EC_USB_HID_CMD_EXT_I2C_WRITE);
	fu_struct_ec_hid_cmd_buffer_set_dwregaddr(buf, 0x00);
	fu_struct_ec_hid_cmd_buffer_set_bufferlen(buf, GUINT16_TO_LE(cmd_buf->len));
	if (!fu_struct_ec_hid_cmd_buffer_set_databytes(buf, cmd_buf->data, cmd_buf->len, error))
		return FALSE;
	return fu_dell_kestrel_ec_hid_set_report(self, buf->data, error);
}

gboolean
fu_dell_kestrel_ec_hid_i2c_read(FuDellKestrelEc *self,
				FuDellKestrelEcHidCmd cmd,
				GByteArray *res,
				guint delayms,
				GError **error)
{
	FuStructEcHidCmdBuffer *buf = fu_struct_ec_hid_cmd_buffer_new();
	guint8 inbuf[FU_DELL_KESTREL_HIDI2C_MAX_READ] = {0xff};

	fu_struct_ec_hid_cmd_buffer_set_cmd(buf, FU_DELL_KESTREL_EC_USB_HID_CMD_WRITE_DATA);
	fu_struct_ec_hid_cmd_buffer_set_ext(buf, FU_DELL_KESTREL_EC_USB_HID_CMD_EXT_I2C_READ);
	fu_struct_ec_hid_cmd_buffer_set_dwregaddr(buf, GUINT32_TO_LE(cmd));
	fu_struct_ec_hid_cmd_buffer_set_bufferlen(buf, GUINT16_TO_LE(res->len + 1));
	if (!fu_dell_kestrel_ec_hid_set_report(self, buf->data, error))
		return FALSE;

	if (delayms > 0)
		fu_device_sleep(FU_DEVICE(self), delayms);

	if (!fu_dell_kestrel_ec_hid_get_report(self, inbuf, error))
		return FALSE;

	return fu_memcpy_safe(res->data, res->len, 0, inbuf, sizeof(inbuf), 1, res->len, error);
}
