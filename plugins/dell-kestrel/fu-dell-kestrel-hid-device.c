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
#define FU_DELL_KESTREL_HID_TIMEOUT	     2000
#define FU_DELL_KESTREL_HID_CMD_FWUPDATE     0xAB
#define FU_DELL_KESTREL_HID_EXT_FWUPDATE     0x80
#define FU_DELL_KESTREL_HID_SUBCMD_FWUPDATE  0x00
#define FU_DELL_KESTREL_HID_DEV_EC_CHUNK_SZ  160000
#define FU_DELL_KESTREL_HID_DEV_ANY_CHUNK_SZ 180000
#define FU_DELL_KESTREL_HID_DEV_NO_CHUNK_SZ  G_MAXSIZE
#define FU_DELL_KESTREL_HID_DATA_PAGE_SZ     192
#define FU_DELL_KESTREL_HID_RESPONSE_LENGTH  0x03
#define FU_DELL_KESTREL_HID_I2C_ADDRESS	     0xec
#define FU_DELL_KESTREL_HID_MAX_RETRIES	     8

#define FU_DELL_KESTREL_HID_I2C_MAX_READ  192
#define FU_DELL_KESTREL_HID_I2C_MAX_WRITE 128

static gboolean
fu_dell_kestrel_hid_device_write(FuDellKestrelHidDevice *self, GByteArray *buf, GError **error)
{
	return fu_hid_device_set_report(FU_HID_DEVICE(self),
					0x0,
					buf->data,
					buf->len,
					FU_DELL_KESTREL_HID_TIMEOUT,
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
	return fu_device_retry(FU_DEVICE(self),
			       fu_dell_kestrel_hid_device_get_report_cb,
			       FU_DELL_KESTREL_HID_MAX_RETRIES,
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
	fu_struct_dell_kestrel_hid_cmd_buffer_set_bufferlen(buf, GUINT16_TO_LE(cmd_buf->len));
	if (!fu_struct_dell_kestrel_hid_cmd_buffer_set_databytes(buf,
								 cmd_buf->data,
								 cmd_buf->len,
								 error))
		return FALSE;
	return fu_dell_kestrel_hid_device_hid_set_report(self, buf->data, error);
}

gboolean
fu_dell_kestrel_hid_device_i2c_read(FuDellKestrelHidDevice *self,
				    FuDellKestrelEcHidCmd cmd,
				    GByteArray *res,
				    guint delayms,
				    GError **error)
{
	g_autoptr(FuStructDellKestrelHidCmdBuffer) buf =
	    fu_struct_dell_kestrel_hid_cmd_buffer_new();
	guint8 inbuf[FU_DELL_KESTREL_HID_I2C_MAX_READ] = {0xff};

	fu_struct_dell_kestrel_hid_cmd_buffer_set_cmd(buf, FU_DELL_KESTREL_HID_CMD_WRITE_DATA);
	fu_struct_dell_kestrel_hid_cmd_buffer_set_ext(buf, FU_DELL_KESTREL_HID_CMD_EXT_I2C_READ);
	fu_struct_dell_kestrel_hid_cmd_buffer_set_dwregaddr(buf, GUINT32_TO_LE(cmd));
	fu_struct_dell_kestrel_hid_cmd_buffer_set_bufferlen(buf, GUINT16_TO_LE(res->len + 1));
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
	case FU_DELL_KESTREL_EC_DEV_TYPE_RMM:
		return FU_DELL_KESTREL_HID_DEV_NO_CHUNK_SZ;
	default:
		return FU_DELL_KESTREL_HID_DEV_ANY_CHUNK_SZ;
	}
}

static guint
fu_dell_kestrel_hid_device_get_first_page_delaytime(FuDellKestrelEcDevType dev_type)
{
	return (dev_type == FU_DELL_KESTREL_EC_DEV_TYPE_RMM) ? 75 * 1000 : 0;
}

static gboolean
fu_dell_kestrel_hid_device_write_firmware_pages(FuDellKestrelHidDevice *self,
						FuChunkArray *pages,
						FuProgress *progress,
						FuDellKestrelEcDevType dev_type,
						guint chunk_idx,
						GError **error)
{
	guint first_page_delay = fu_dell_kestrel_hid_device_get_first_page_delaytime(dev_type);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(pages));

	for (guint j = 0; j < fu_chunk_array_length(pages); j++) {
		g_autoptr(GByteArray) page_aligned = g_byte_array_new();
		g_autoptr(FuChunk) page = NULL;
		g_autoptr(GBytes) page_bytes = NULL;
		g_autoptr(GError) error_local = NULL;

		page = fu_chunk_array_index(pages, j);
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

		/* send to ec */
		if (!fu_dell_kestrel_hid_device_write(self, page_aligned, &error_local)) {
			/* A buggy device may fail to send an acknowledgment receipt
			   after the last page write, resulting in a timeout error.

			   This is a known issue so waive it for now.
			*/
			if (dev_type == FU_DELL_KESTREL_EC_DEV_TYPE_LAN &&
			    j == fu_chunk_array_length(pages) - 1 &&
			    g_error_matches(error_local,
					    G_USB_DEVICE_ERROR,
					    G_USB_DEVICE_ERROR_TIMED_OUT)) {
				g_debug("ignored error: %s", error_local->message);
				continue;
			}
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}

		/* device needs time to process incoming pages */
		if (j == 0) {
			g_debug("wait %u ms before the next page", first_page_delay);
			fu_device_sleep(FU_DEVICE(self), first_page_delay);
		}
		fu_progress_step_done(progress);
	}
	return TRUE;
}

static gboolean
fu_dell_kestrel_hid_device_verify_chunk_result(FuDellKestrelHidDevice *self,
					       guint chunk_idx,
					       GError **error)
{
	guint8 res[FU_DELL_KESTREL_HID_DATA_PAGE_SZ] = {0xff};

	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      0x0,
				      res,
				      sizeof(res),
				      FU_DELL_KESTREL_HID_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error))
		return FALSE;

	switch (res[1]) {
	case FU_DELL_KESTREL_EC_RESP_TO_CHUNK_UPDATE_COMPLETE:
		g_debug("dock response '%u' to chunk[%u]: firmware updated successfully.",
			res[1],
			chunk_idx);
		break;
	case FU_DELL_KESTREL_EC_RESP_TO_CHUNK_SEND_NEXT_CHUNK:
		g_debug("dock response '%u' to chunk[%u]: send next chunk.", res[1], chunk_idx);
		break;
	case FU_DELL_KESTREL_EC_RESP_TO_CHUNK_UPDATE_FAILED:
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "dock response '%u' to chunk[%u]: failed to write firmware.",
			    res[1],
			    chunk_idx);
		return FALSE;
	}

	return TRUE;
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
	chunks = fu_chunk_array_new_from_bytes(fw, 0, chunk_sz);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));

	/* iterate the chunks */
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(FuChunkArray) pages = NULL;
		g_autoptr(GBytes) buf = NULL;

		chk = fu_chunk_array_index(chunks, i);
		if (chk == NULL)
			return FALSE;

		/* prepend header and command to the chunk data */
		buf = fu_dell_kestrel_hid_device_fwup_pkg_new(chk, fw_sz, dev_type, dev_identifier);

		/* slice the chunk into pages */
		pages = fu_chunk_array_new_from_bytes(buf, 0, FU_DELL_KESTREL_HID_DATA_PAGE_SZ);

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

		/* ensure the chunk has been acknowledged */
		if (!fu_dell_kestrel_hid_device_verify_chunk_result(self, i, error))
			return FALSE;

		fu_progress_step_done(progress);
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
