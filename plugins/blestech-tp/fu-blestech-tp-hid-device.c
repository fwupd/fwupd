/*
 * Copyright 2026 SHENZHEN Betterlife Electronic CO.LTD <xiaomaoting@blestech.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-blestech-tp-firmware.h"
#include "fu-blestech-tp-hid-device.h"
#include "fu-blestech-tp-struct.h"

struct _FuBlestechTpHidDevice {
	FuHidrawDevice parent_instance;
};

G_DEFINE_TYPE(FuBlestechTpHidDevice, fu_blestech_tp_hid_device, FU_TYPE_HIDRAW_DEVICE)

#define FU_BLESTECH_TP_DEVICE_REPORT_ID 0x0E

#define FU_BLESTECH_TP_DEVICE_PACK_FIX_SIZE 0x06

/* fixed 96K fw size */
#define FU_BLESTECH_TP_DEVICE_PAGE_SIZE 0x200

/* 16K BOOT FW size*/
#define FU_BLESTECH_TP_DEVICE_BOOT_SIZE 0x4000

#define FU_BLESTECH_TP_DEVICE_APP_CONFIG_PAGE 96

static gboolean
fu_blestech_tp_hid_device_write(FuBlestechTpHidDevice *self,
				GByteArray *wbuf,
				gsize rbufsz,
				GError **error)
{
	/* SetReport */
	g_autoptr(FuBlestechTpSetHdr) st_hdr = fu_blestech_tp_set_hdr_new();
	guint8 checksum = 0;

	fu_blestech_tp_set_hdr_set_report_id(st_hdr, FU_BLESTECH_TP_DEVICE_REPORT_ID);
	fu_blestech_tp_set_hdr_set_pack_len(st_hdr,
					    wbuf->len + FU_BLESTECH_TP_DEVICE_PACK_FIX_SIZE);
	fu_blestech_tp_set_hdr_set_write_len(st_hdr, wbuf->len);
	fu_blestech_tp_set_hdr_set_read_len(st_hdr, rbufsz);
	if (!fu_blestech_tp_set_hdr_set_data(st_hdr, wbuf->data, wbuf->len, error))
		return FALSE;

	/* checksum */
	if (!fu_xor8_safe(st_hdr->buf->data,
			  st_hdr->buf->len,
			  FU_BLESTECH_TP_SET_HDR_OFFSET_FRAME_FLAG,
			  st_hdr->buf->data[FU_BLESTECH_TP_SET_HDR_OFFSET_PACK_LEN] - 1,
			  &checksum,
			  error))
		return FALSE;
	fu_blestech_tp_set_hdr_set_checksum(st_hdr, checksum + 1);
	return fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					    st_hdr->buf->data,
					    st_hdr->buf->len,
					    FU_IOCTL_FLAG_NONE,
					    error);
}

static gboolean
fu_blestech_tp_hid_device_read(FuBlestechTpHidDevice *self,
			       guint8 *rbuf,
			       gsize rbufsz,
			       GError **error)
{
	guint8 read_buf[34] = {FU_BLESTECH_TP_DEVICE_REPORT_ID};
	if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(self),
					  read_buf,
					  sizeof(read_buf),
					  FU_IOCTL_FLAG_NONE,
					  error))
		return FALSE;
	return fu_memcpy_safe(rbuf, rbufsz, 0x0, read_buf, sizeof(read_buf), 0x04, rbufsz, error);
}

static gboolean
fu_blestech_tp_hid_device_get_version(FuBlestechTpHidDevice *self, guint16 *fw_ver, GError **error)
{
	guint8 buf[FU_BLESTECH_TP_GET_FW_VER_RES_SIZE] = {0};
	g_autoptr(FuBlestechTpGetFwVerReq) st_req = fu_blestech_tp_get_fw_ver_req_new();
	g_autoptr(FuBlestechTpGetFwVerRes) st_res = NULL;

	if (!fu_blestech_tp_hid_device_write(self, st_req->buf, sizeof(buf), error)) {
		g_prefix_error_literal(error, "failed to request version: ");
		return FALSE;
	}
	if (!fu_blestech_tp_hid_device_read(self, buf, sizeof(buf), error)) {
		g_prefix_error_literal(error, "failed to read version: ");
		return FALSE;
	}
	st_res = fu_blestech_tp_get_fw_ver_res_parse(buf, sizeof(buf), 0x0, error);
	if (st_res == NULL)
		return FALSE;
	*fw_ver = fu_blestech_tp_get_fw_ver_res_get_val(st_res);

	/* success */
	return TRUE;
}

static gboolean
fu_blestech_tp_hid_device_update_start(FuBlestechTpHidDevice *self, GError **error)
{
	guint8 buf[2] = {0};
	g_autoptr(FuBlestechTpUpdateStartReq) st = fu_blestech_tp_update_start_req_new();

	if (!fu_blestech_tp_hid_device_write(self, st->buf, sizeof(buf), error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), 10);

	/* unknown purpose */
	if (!fu_blestech_tp_hid_device_read(self, buf, sizeof(buf), error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_blestech_tp_hid_device_switch_boot(FuBlestechTpHidDevice *self, GError **error)
{
	guint16 fw_ver = 0;
	g_autoptr(FuBlestechTpSwitchBootReq) st = fu_blestech_tp_switch_boot_req_new();

	if (!fu_blestech_tp_hid_device_write(self, st->buf, 0, error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), 50);

	/* check whether switch boot success */
	if (!fu_blestech_tp_hid_device_get_version(self, &fw_ver, error))
		return FALSE;
	if ((fw_ver & 0xFF) < 0xC0 || (fw_ver & 0xFF) > 0xD0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "not expected boot ver: 0x%x",
			    fw_ver);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_blestech_tp_hid_device_program_page_end(FuBlestechTpHidDevice *self,
					   guint16 page,
					   guint8 checksum,
					   GError **error)
{
	guint8 buf[FU_BLESTECH_TP_PROGRAM_PAGE_END_RES_SIZE] = {0};
	guint8 checksum_act;
	g_autoptr(FuBlestechTpProgramPageEndReq) st_req = fu_blestech_tp_program_page_end_req_new();
	g_autoptr(FuBlestechTpProgramPageEndRes) st_res = NULL;

	fu_blestech_tp_program_page_end_req_set_page(st_req, page);
	if (!fu_blestech_tp_hid_device_write(self, st_req->buf, sizeof(buf), error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), 30);
	if (!fu_blestech_tp_hid_device_read(self, buf, sizeof(buf), error))
		return FALSE;

	/* check whether checksum matched */
	st_res = fu_blestech_tp_program_page_end_res_parse(buf, sizeof(buf), 0x0, error);
	if (st_res == NULL)
		return FALSE;
	checksum_act = fu_blestech_tp_program_page_end_res_get_checksum(st_res);
	if (checksum_act != checksum) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed checksum: actual 0x%02x, expected 0x%02x",
			    checksum_act,
			    checksum);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_blestech_tp_hid_device_program_page(FuBlestechTpHidDevice *self, FuChunk *page, GError **error)
{
	g_autoptr(FuBlestechTpProgramPageReq) st_req = fu_blestech_tp_program_page_req_new();

	if (!fu_blestech_tp_program_page_req_set_data(st_req,
						      fu_chunk_get_data(page),
						      fu_chunk_get_data_sz(page),
						      error))
		return FALSE;
	g_byte_array_set_size(st_req->buf, fu_chunk_get_data_sz(page) + 1);
	return fu_blestech_tp_hid_device_write(self, st_req->buf, 0, error);
}

static gboolean
fu_blestech_tp_hid_device_program_page_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuBlestechTpHidDevice *self = FU_BLESTECH_TP_HID_DEVICE(device);
	FuChunk *chk = (FuChunk *)user_data;
	guint8 checksum;
	g_autoptr(GPtrArray) pages = NULL;

	/* program 512 bytes */
	pages = fu_chunk_array_new(fu_chunk_get_data(chk),
				   fu_chunk_get_data_sz(chk),
				   0x0,
				   0x0,
				   FU_BLESTECH_TP_PROGRAM_PAGE_REQ_N_ELEMENTS_DATA);
	for (guint i = 0; i < pages->len; i++) {
		FuChunk *chk_page = g_ptr_array_index(pages, i);
		if (!fu_blestech_tp_hid_device_program_page(self, chk_page, error)) {
			g_prefix_error(error,
				       "program failed @0x%08x: ",
				       (guint)fu_chunk_get_address(chk_page));
			return FALSE;
		}
		fu_device_sleep(FU_DEVICE(self), 1);
	}

	/* page-end */
	checksum = fu_xor8(fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk)) + 1;
	return fu_blestech_tp_hid_device_program_page_end(self,
							  fu_chunk_get_idx(chk),
							  checksum,
							  error);
}

static gboolean
fu_blestech_tp_hid_device_program_chunk(FuBlestechTpHidDevice *self,
					FuChunkArray *chunks,
					guint chk_idx,
					GError **error)
{
	g_autoptr(FuChunk) chk = NULL;

	chk = fu_chunk_array_index(chunks, chk_idx, error);
	if (chk == NULL)
		return FALSE;
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_blestech_tp_hid_device_program_page_cb,
				    5,
				    30,
				    chk,
				    error);
}

static gboolean
fu_blestech_tp_hid_device_program(FuBlestechTpHidDevice *self,
				  FuFirmware *firmware,
				  FuProgress *progress,
				  GError **error)
{
	guint boot_pack_nums = 0;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;
	chunks = fu_chunk_array_new_from_stream(stream,
						fu_firmware_get_addr(firmware),
						FU_CHUNK_PAGESZ_NONE,
						FU_BLESTECH_TP_DEVICE_PAGE_SIZE,
						error);
	if (chunks == NULL)
		return FALSE;

	boot_pack_nums = FU_BLESTECH_TP_DEVICE_BOOT_SIZE / FU_BLESTECH_TP_DEVICE_PAGE_SIZE;
	if (boot_pack_nums > fu_chunk_array_length(chunks)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "boot pack size is not possible");
		return FALSE;
	}

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks) - boot_pack_nums);
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		/* skip boot parts and config part */
		if (i < boot_pack_nums || i == FU_BLESTECH_TP_DEVICE_APP_CONFIG_PAGE)
			continue;
		if (!fu_blestech_tp_hid_device_program_chunk(self, chunks, i, error)) {
			g_prefix_error(error, "program page %u failed: ", i);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	if (!fu_blestech_tp_hid_device_program_chunk(self,
						     chunks,
						     FU_BLESTECH_TP_DEVICE_APP_CONFIG_PAGE,
						     error)) {
		g_prefix_error(error,
			       "program page %d failed: ",
			       FU_BLESTECH_TP_DEVICE_APP_CONFIG_PAGE);
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_blestech_tp_hid_device_program_checksum(FuBlestechTpHidDevice *self,
					   guint16 checksum,
					   GError **error)
{
	guint16 calc_checksum;
	guint8 buf[FU_BLESTECH_TP_PROGRAM_CHECKSUM_RES_SIZE] = {0};
	g_autoptr(FuBlestechTpProgramChecksumReq) st_req =
	    fu_blestech_tp_program_checksum_req_new();
	g_autoptr(FuBlestechTpProgramChecksumRes) st_res = NULL;

	/* get */
	fu_blestech_tp_program_checksum_req_set_val(st_req, checksum);
	if (!fu_blestech_tp_hid_device_write(self, st_req->buf, sizeof(buf), error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), 60);
	if (!fu_blestech_tp_hid_device_read(self, buf, sizeof(buf), error))
		return FALSE;

	/* verify */
	st_res = fu_blestech_tp_program_checksum_res_parse(buf, sizeof(buf), 0x0, error);
	if (st_res == NULL)
		return FALSE;
	calc_checksum = fu_blestech_tp_program_checksum_res_get_val(st_res);
	if (calc_checksum != checksum) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed checksum: expected 0x%04x, actual 0x%04x",
			    checksum,
			    calc_checksum);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_blestech_tp_hid_device_program_end(FuBlestechTpHidDevice *self, GError **error)
{
	g_autoptr(FuBlestechTpProgramEndReq) st = fu_blestech_tp_program_end_req_new();

	if (!fu_blestech_tp_hid_device_write(self, st->buf, 0, error))
		return FALSE;

	/* need about 80ms to start */
	fu_device_sleep(FU_DEVICE(self), 80);
	return TRUE;
}

static gboolean
fu_blestech_tp_hid_device_write_firmware(FuDevice *device,
					 FuFirmware *firmware,
					 FuProgress *progress,
					 FwupdInstallFlags flags,
					 GError **error)
{
	FuBlestechTpHidDevice *self = FU_BLESTECH_TP_HID_DEVICE(device);
	guint16 checksum = fu_blestech_tp_firmware_get_checksum(FU_BLESTECH_TP_FIRMWARE(firmware));

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, "switch");
	fu_progress_add_step(progress, FWUPD_STATUS_DOWNLOADING, 5, "start");
	fu_progress_add_step(progress, FWUPD_STATUS_DOWNLOADING, 80, "program");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 10, "reset");

	/* switch to boot */
	if (!fu_blestech_tp_hid_device_switch_boot(self, error)) {
		g_prefix_error_literal(error, "failed to switch boot: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* update start */
	if (!fu_blestech_tp_hid_device_update_start(self, error)) {
		g_prefix_error_literal(error, "failed to update start: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* write image */
	if (!fu_blestech_tp_hid_device_program(self,
					       firmware,
					       fu_progress_get_child(progress),
					       error)) {
		g_prefix_error_literal(error, "failed to write image: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* finish */
	if (!fu_blestech_tp_hid_device_program_checksum(self, checksum, error)) {
		g_prefix_error_literal(error, "failed to program checksum: ");
		return FALSE;
	}
	if (!fu_blestech_tp_hid_device_program_end(self, error)) {
		g_prefix_error_literal(error, "failed to program end: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static void
fu_blestech_tp_hid_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 3, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 90, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 3, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 4, "reload");
}

static gchar *
fu_blestech_tp_hid_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint16(version_raw, fu_device_get_version_format(device));
}

static gboolean
fu_blestech_tp_hid_device_setup(FuDevice *device, GError **error)
{
	FuBlestechTpHidDevice *self = FU_BLESTECH_TP_HID_DEVICE(device);
	guint16 version_raw = 0;

	if (!fu_blestech_tp_hid_device_get_version(self, &version_raw, error))
		return FALSE;
	fu_device_set_version_raw(device, version_raw);

	/* success */
	return TRUE;
}

static void
fu_blestech_tp_hid_device_init(FuBlestechTpHidDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_firmware_size(FU_DEVICE(self), 0x18000);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_BLESTECH_TP_FIRMWARE);
	fu_device_set_summary(FU_DEVICE(self), "Touchpad");
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_INPUT_TOUCHPAD);
	fu_device_add_protocol(FU_DEVICE(self), "com.blestech.tp");
	fu_device_set_vendor(FU_DEVICE(self), "Blestech");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_BCD);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_NONBLOCK);
}

static void
fu_blestech_tp_hid_device_class_init(FuBlestechTpHidDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_blestech_tp_hid_device_setup;
	device_class->reload = fu_blestech_tp_hid_device_setup;
	device_class->set_progress = fu_blestech_tp_hid_device_set_progress;
	device_class->convert_version = fu_blestech_tp_hid_device_convert_version;
	device_class->write_firmware = fu_blestech_tp_hid_device_write_firmware;
}
