/*
 * Copyright 2024 Dell Technologies
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-dell-kestrel-hid-device.h"
#include "fu-dell-kestrel-hid-struct.h"

G_DEFINE_TYPE(FuDellKestrelHidDevice, fu_dell_kestrel_hid_device, FU_TYPE_HID_DEVICE)

/* Used for EC HID communication */
#define FU_DELL_KESTREL_HID_TIMEOUT	     300
#define FU_DELL_KESTREL_HID_CMD_FWUPDATE     0xAB
#define FU_DELL_KESTREL_HID_EXT_FWUPDATE     0x80
#define FU_DELL_KESTREL_HID_SUBCMD_FWUPDATE  0x00
#define FU_DELL_KESTREL_HID_DEV_EC_CHUNK_SZ  160000
#define FU_DELL_KESTREL_HID_DEV_PD_CHUNK_SZ  190000
#define FU_DELL_KESTREL_HID_DEV_ANY_CHUNK_SZ 180000
#define FU_DELL_KESTREL_HID_DEV_NO_CHUNK_SZ  G_MAXSIZE
#define FU_DELL_KESTREL_HID_DATA_PAGE_SZ     192
#define FU_DELL_KESTREL_HID_RESPONSE_LENGTH  0x03
#define FU_DELL_KESTREL_HID_I2C_ADDRESS	     0xec
#define FU_DELL_KESTREL_HID_MAX_RETRIES	     8

#define FU_DELL_KESTREL_HID_I2C_MAX_READ  192
#define FU_DELL_KESTREL_HID_I2C_MAX_WRITE 128

static gboolean
fu_dell_kestrel_hid_device_write(FuDellKestrelHidDevice *self,
				 GByteArray *buf,
				 guint delay_ms,
				 GError **error)
{
	return fu_hid_device_set_report(FU_HID_DEVICE(self),
					0x0,
					buf->data,
					buf->len,
					delay_ms,
					FU_HID_DEVICE_FLAG_RETRY_FAILURE,
					error);
}

static GBytes *
fu_dell_kestrel_hid_device_fwup_pkg_new(FuChunk *chk,
					gsize fw_size,
					FuDellKestrelEcDevType dev_type,
					guint8 dev_identifier)
{
	g_autoptr(FuStructDellKestrelHidFwUpdatePkg) fwbuf =
	    fu_struct_dell_kestrel_hid_fw_update_pkg_new();
	gsize chk_datasz = fu_chunk_get_data_sz(chk);

	/* header */
	fu_struct_dell_kestrel_hid_fw_update_pkg_set_cmd(fwbuf, FU_DELL_KESTREL_HID_CMD_FWUPDATE);
	fu_struct_dell_kestrel_hid_fw_update_pkg_set_ext(fwbuf, FU_DELL_KESTREL_HID_EXT_FWUPDATE);
	fu_struct_dell_kestrel_hid_fw_update_pkg_set_chunk_sz(
	    fwbuf,
	    7 + chk_datasz); // 7 = sizeof(command)

	/* command */
	fu_struct_dell_kestrel_hid_fw_update_pkg_set_sub_cmd(fwbuf,
							     FU_DELL_KESTREL_HID_SUBCMD_FWUPDATE);
	fu_struct_dell_kestrel_hid_fw_update_pkg_set_dev_type(fwbuf, dev_type);
	fu_struct_dell_kestrel_hid_fw_update_pkg_set_dev_identifier(fwbuf, dev_identifier);
	fu_struct_dell_kestrel_hid_fw_update_pkg_set_fw_sz(fwbuf, fw_size);

	/* data */
	fu_byte_array_append_bytes(fwbuf, fu_chunk_get_bytes(chk));

	return g_bytes_new(fwbuf->data, fwbuf->len);
}

static gboolean
fu_dell_kestrel_hid_device_hid_set_report_cb(FuDevice *self, gpointer user_data, GError **error)
{
	guint8 *outbuffer = (guint8 *)user_data;
	return fu_hid_device_set_report(FU_HID_DEVICE(self),
					0x0,
					outbuffer,
					192,
					FU_DELL_KESTREL_HID_TIMEOUT * 3,
					FU_HID_DEVICE_FLAG_NONE,
					error);
}

static gboolean
fu_dell_kestrel_hid_device_hid_set_report(FuDellKestrelHidDevice *self,
					  guint8 *outbuffer,
					  GError **error)
{
	return fu_device_retry(FU_DEVICE(self),
			       fu_dell_kestrel_hid_device_hid_set_report_cb,
			       FU_DELL_KESTREL_HID_MAX_RETRIES,
			       outbuffer,
			       error);
}

static gboolean
fu_dell_kestrel_hid_device_get_report_cb(FuDevice *self, gpointer user_data, GError **error)
{
	guint8 *inbuffer = (guint8 *)user_data;
	return fu_hid_device_get_report(FU_HID_DEVICE(self),
					0x0,
					inbuffer,
					192,
					FU_DELL_KESTREL_HID_TIMEOUT,
					FU_HID_DEVICE_FLAG_NONE,
					error);
}

static gboolean
fu_dell_kestrel_hid_device_get_report(FuDellKestrelHidDevice *self,
				      guint8 *inbuffer,
				      GError **error)
{
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_dell_kestrel_hid_device_get_report_cb,
				    FU_DELL_KESTREL_HID_MAX_RETRIES,
				    2000,
				    inbuffer,
				    error);
}

gboolean
fu_dell_kestrel_hid_device_i2c_write(FuDellKestrelHidDevice *self,
				     GByteArray *cmd_buf,
				     GError **error)
{
	g_autoptr(FuStructDellKestrelHidCmdBuffer) buf =
	    fu_struct_dell_kestrel_hid_cmd_buffer_new();

	g_return_val_if_fail(cmd_buf->len <= FU_DELL_KESTREL_HID_I2C_MAX_WRITE, FALSE);

	fu_struct_dell_kestrel_hid_cmd_buffer_set_cmd(buf, FU_DELL_KESTREL_HID_CMD_WRITE_DATA);
	fu_struct_dell_kestrel_hid_cmd_buffer_set_ext(buf, FU_DELL_KESTREL_HID_CMD_EXT_I2C_WRITE);
	fu_struct_dell_kestrel_hid_cmd_buffer_set_dwregaddr(buf, 0x00);
	fu_struct_dell_kestrel_hid_cmd_buffer_set_bufferlen(buf, cmd_buf->len);
	if (!fu_struct_dell_kestrel_hid_cmd_buffer_set_databytes(buf,
								 cmd_buf->data,
								 cmd_buf->len,
								 error))
		return FALSE;
	return fu_dell_kestrel_hid_device_hid_set_report(self, buf->data, error);
}

gboolean
fu_dell_kestrel_hid_device_i2c_read(FuDellKestrelHidDevice *self,
				    FuDellKestrelEcCmd cmd,
				    GByteArray *res,
				    guint delayms,
				    GError **error)
{
	g_autoptr(FuStructDellKestrelHidCmdBuffer) buf =
	    fu_struct_dell_kestrel_hid_cmd_buffer_new();
	guint8 inbuf[FU_DELL_KESTREL_HID_I2C_MAX_READ] = {0xff};

	fu_struct_dell_kestrel_hid_cmd_buffer_set_cmd(buf, FU_DELL_KESTREL_HID_CMD_WRITE_DATA);
	fu_struct_dell_kestrel_hid_cmd_buffer_set_ext(buf, FU_DELL_KESTREL_HID_CMD_EXT_I2C_READ);
	fu_struct_dell_kestrel_hid_cmd_buffer_set_dwregaddr(buf, cmd);
	fu_struct_dell_kestrel_hid_cmd_buffer_set_bufferlen(buf, res->len + 1);
	if (!fu_dell_kestrel_hid_device_hid_set_report(self, buf->data, error))
		return FALSE;

	if (delayms > 0)
		fu_device_sleep(FU_DEVICE(self), delayms);

	if (!fu_dell_kestrel_hid_device_get_report(self, inbuf, error))
		return FALSE;

	return fu_memcpy_safe(res->data, res->len, 0, inbuf, sizeof(inbuf), 1, res->len, error);
}

static guint
fu_dell_kestrel_hid_device_get_chunk_delaytime(FuDellKestrelEcDevType dev_type)
{
	switch (dev_type) {
	case FU_DELL_KESTREL_EC_DEV_TYPE_MAIN_EC:
		return 3 * 1000;
	case FU_DELL_KESTREL_EC_DEV_TYPE_RMM:
		return 60 * 1000;
	case FU_DELL_KESTREL_EC_DEV_TYPE_PD:
		return 15 * 1000;
	case FU_DELL_KESTREL_EC_DEV_TYPE_LAN:
		return 70 * 1000;
	default:
		return 30 * 1000;
	}
}

static gsize
fu_dell_kestrel_hid_device_get_chunk_size(FuDellKestrelEcDevType dev_type)
{
	/* return the max chunk size in bytes */
	switch (dev_type) {
	case FU_DELL_KESTREL_EC_DEV_TYPE_MAIN_EC:
		return FU_DELL_KESTREL_HID_DEV_EC_CHUNK_SZ;
	case FU_DELL_KESTREL_EC_DEV_TYPE_PD:
		return FU_DELL_KESTREL_HID_DEV_PD_CHUNK_SZ;
	case FU_DELL_KESTREL_EC_DEV_TYPE_RMM:
		return FU_DELL_KESTREL_HID_DEV_NO_CHUNK_SZ;
	default:
		return FU_DELL_KESTREL_HID_DEV_ANY_CHUNK_SZ;
	}
}

static gboolean
fu_dell_kestrel_hid_device_write_firmware_pages(FuDellKestrelHidDevice *self,
						FuChunkArray *pages,
						FuProgress *progress,
						FuDellKestrelEcDevType dev_type,
						guint chunk_idx,
						GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(pages));

	for (guint j = 0; j < fu_chunk_array_length(pages); j++) {
		g_autoptr(GByteArray) page_aligned = g_byte_array_new();
		g_autoptr(FuChunk) page = NULL;
		g_autoptr(GError) error_local = NULL;
		guint page_ack_time = FU_DELL_KESTREL_HID_TIMEOUT;

		page = fu_chunk_array_index(pages, j, error);
		if (page == NULL)
			return FALSE;

		g_debug("sending chunk: %u, page: %u/%u.",
			chunk_idx,
			j,
			fu_chunk_array_length(pages) - 1);

		/* strictly align the page size with 0x00 as packet */
		g_byte_array_append(page_aligned,
				    fu_chunk_get_data(page),
				    fu_chunk_get_data_sz(page));
		fu_byte_array_set_size(page_aligned, FU_DELL_KESTREL_HID_DATA_PAGE_SZ, 0xFF);

		/* rmm needs extra time to ack the first page */
		if (j == 0 && dev_type == FU_DELL_KESTREL_EC_DEV_TYPE_RMM)
			page_ack_time = 75 * 1000;

		/* send to ec */
		if (!fu_dell_kestrel_hid_device_write(self,
						      page_aligned,
						      page_ack_time,
						      &error_local)) {
			/* A buggy device may fail to send an acknowledgment receipt
			   after the last page write, resulting in a timeout error.

			   This is a known issue so waive it for now.
			*/
			if (dev_type == FU_DELL_KESTREL_EC_DEV_TYPE_LAN &&
			    j == fu_chunk_array_length(pages) - 1 &&
			    g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_TIMED_OUT)) {
				g_debug("ignored error: %s", error_local->message);
				fu_progress_step_done(progress);
				continue;
			}
			g_propagate_prefixed_error(error,
						   g_steal_pointer(&error_local),
						   "%s failed to write page: ",
						   fu_device_get_name(FU_DEVICE(self)));
			return FALSE;
		}

		/* rmm needs extra time to accept incoming pages */
		if (j == 0 && dev_type == FU_DELL_KESTREL_EC_DEV_TYPE_RMM) {
			FuDevice *dev = FU_DEVICE(self);
			if (fu_version_compare(fu_device_get_version(dev),
					       "1.8.6.0",
					       fu_device_get_version_format(dev)) < 0) {
				guint delay_ms = 75 * 1000;
				g_debug("wait %u ms before the next page", delay_ms);
				fu_device_sleep(FU_DEVICE(self), delay_ms);
			}
		}
		fu_progress_step_done(progress);
	}
	return TRUE;
}

static gboolean
fu_dell_kestrel_hid_device_verify_chunk_result(FuDellKestrelHidDevice *self,
					       FuDellKestrelHidEcChunkResponse *resp,
					       GError **error)
{
	guint8 buf[FU_DELL_KESTREL_HID_DATA_PAGE_SZ] = {0xff};

	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      0x0,
				      buf,
				      sizeof(buf),
				      FU_DELL_KESTREL_HID_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error))
		return FALSE;

	return fu_memread_uint8_safe(buf, sizeof(buf), 1, (guint8 *)resp, error);
}

gboolean
fu_dell_kestrel_hid_device_write_firmware(FuDellKestrelHidDevice *self,
					  FuFirmware *firmware,
					  FuProgress *progress,
					  FuDellKestrelEcDevType dev_type,
					  guint8 dev_identifier,
					  GError **error)
{
	gsize fw_sz = 0;
	gsize chunk_sz = fu_dell_kestrel_hid_device_get_chunk_size(dev_type);
	guint chunk_delay = fu_dell_kestrel_hid_device_get_chunk_delaytime(dev_type);
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* payload size */
	fw_sz = g_bytes_get_size(fw);

	if (fu_firmware_get_version(firmware) != 0x0) {
		g_debug("writing %s firmware %s -> %s",
			fu_device_get_name(FU_DEVICE(self)),
			fu_device_get_version(FU_DEVICE(self)),
			fu_firmware_get_version(firmware));
	}

	/* maximum buffer size */
	chunks = fu_chunk_array_new_from_bytes(fw,
					       FU_CHUNK_ADDR_OFFSET_NONE,
					       FU_CHUNK_PAGESZ_NONE,
					       chunk_sz);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));

	/* iterate the chunks */
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		FuDellKestrelHidEcChunkResponse resp =
		    FU_DELL_KESTREL_HID_EC_CHUNK_RESPONSE_UNKNOWN;
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(FuChunkArray) pages = NULL;
		g_autoptr(GBytes) buf = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		/* prepend header and command to the chunk data */
		buf = fu_dell_kestrel_hid_device_fwup_pkg_new(chk, fw_sz, dev_type, dev_identifier);

		/* slice the chunk into pages */
		pages = fu_chunk_array_new_from_bytes(buf,
						      FU_CHUNK_ADDR_OFFSET_NONE,
						      FU_CHUNK_PAGESZ_NONE,
						      FU_DELL_KESTREL_HID_DATA_PAGE_SZ);

		/* write pages */
		if (!fu_dell_kestrel_hid_device_write_firmware_pages(
			self,
			pages,
			fu_progress_get_child(progress),
			dev_type,
			i,
			error))
			return FALSE;

		/* delay time */
		g_debug("wait %u ms for dock to finish the chunk", chunk_delay);
		fu_device_sleep(FU_DEVICE(self), chunk_delay);

		/* get device response for the chunk in transaction */
		if (!fu_dell_kestrel_hid_device_verify_chunk_result(self, &resp, error))
			return FALSE;

		/* verify the device response */
		g_debug("dock response %u to chunk[%u]: %s",
			resp,
			i,
			fu_dell_kestrel_hid_ec_chunk_response_to_string(resp));

		switch (resp) {
		case FU_DELL_KESTREL_HID_EC_CHUNK_RESPONSE_UPDATE_COMPLETE:
			fu_progress_finished(progress);
			return TRUE;
		case FU_DELL_KESTREL_HID_EC_CHUNK_RESPONSE_SEND_NEXT_CHUNK:
			fu_progress_step_done(progress);
			break;
		default:
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "%s failed to write chunk[%u]: %s",
				    fu_device_get_name(FU_DEVICE(self)),
				    i,
				    fu_dell_kestrel_hid_ec_chunk_response_to_string(resp));
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static void
fu_dell_kestrel_hid_device_init(FuDellKestrelHidDevice *self)
{
}

static void
fu_dell_kestrel_hid_device_class_init(FuDellKestrelHidDeviceClass *klass)
{
}
