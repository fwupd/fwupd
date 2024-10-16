/*
 * Copyright 2023 Goodix.inc <xulinkun@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-goodixtp-common.h"
#include "fu-goodixtp-gtx8-device.h"
#include "fu-goodixtp-gtx8-firmware.h"

struct _FuGoodixtpGtx8Device {
	FuGoodixtpHidDevice parent_instance;
};

G_DEFINE_TYPE(FuGoodixtpGtx8Device, fu_goodixtp_gtx8_device, FU_TYPE_GOODIXTP_HID_DEVICE)

#define CMD_ADDR 0x60CC

#define BL_STATE_ADDR	  0x5095
#define FLASH_RESULT_ADDR 0x5096
#define FLASH_BUFFER_ADDR 0xC000

static gboolean
fu_goodixtp_gtx8_device_read_pkg(FuGoodixtpGtx8Device *self,
				 gsize addr,
				 guint8 *buf,
				 gsize bufsz,
				 GError **error)
{
	guint8 hidbuf[PACKAGE_LEN] = {0};

	hidbuf[0] = REPORT_ID;
	hidbuf[1] = I2C_DIRECT_RW;
	hidbuf[2] = 0;
	hidbuf[3] = 0;
	hidbuf[4] = 5;
	hidbuf[5] = I2C_READ_FLAG;
	fu_memwrite_uint16(hidbuf + 6, addr, G_BIG_ENDIAN);
	fu_memwrite_uint16(hidbuf + 8, bufsz, G_BIG_ENDIAN);
	if (!fu_goodixtp_hid_device_set_report(FU_GOODIXTP_HID_DEVICE(self), hidbuf, 10, error))
		return FALSE;

	if (!fu_goodixtp_hid_device_get_report(FU_GOODIXTP_HID_DEVICE(self),
					       hidbuf,
					       sizeof(hidbuf),
					       error))
		return FALSE;

	if (hidbuf[3] != 0 || hidbuf[4] != bufsz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "Failed to read_pkg, hidbuf[3]:%d hidbuf[4]:%d",
			    hidbuf[3],
			    hidbuf[4]);
		return FALSE;
	}
	if (!fu_memcpy_safe(buf, bufsz, 0, hidbuf, sizeof(hidbuf), 5, hidbuf[4], error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_goodixtp_gtx8_device_hid_read(FuGoodixtpGtx8Device *self,
				 gsize addr,
				 guint8 *buf,
				 gsize bufsz,
				 GError **error)
{
	g_autoptr(GPtrArray) chunks =
	    fu_chunk_array_mutable_new(buf, bufsz, addr, 0, PACKAGE_LEN - 10);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		if (!fu_goodixtp_gtx8_device_read_pkg(self,
						      fu_chunk_get_address(chk),
						      fu_chunk_get_data_out(chk),
						      fu_chunk_get_data_sz(chk),
						      error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_goodixtp_gtx8_device_hid_write(FuGoodixtpGtx8Device *self,
				  gsize addr,
				  const guint8 *buf,
				  gsize bufsz,
				  GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GBytes) fw = g_bytes_new(buf, bufsz);

	chunks = fu_chunk_array_new_from_bytes(fw, addr, FU_CHUNK_PAGESZ_NONE, PACKAGE_LEN - 10);
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		guint8 hidbuf[PACKAGE_LEN] = {0};
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GBytes) blob = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		blob = fu_chunk_get_bytes(chk, error);
		if (blob == NULL)
			return FALSE;
		hidbuf[0] = REPORT_ID;
		hidbuf[1] = I2C_DIRECT_RW;
		if (i == fu_chunk_array_length(chunks) - 1)
			hidbuf[2] = 0x00;
		else
			hidbuf[2] = 0x01;
		hidbuf[3] = i;
		hidbuf[4] = g_bytes_get_size(blob) + 5;
		hidbuf[5] = I2C_WRITE_FLAG;
		fu_memwrite_uint16(hidbuf + 6, fu_chunk_get_address(chk), G_BIG_ENDIAN);
		fu_memwrite_uint16(hidbuf + 8, g_bytes_get_size(blob), G_BIG_ENDIAN);
		if (!fu_memcpy_safe(hidbuf,
				    sizeof(hidbuf),
				    10,
				    g_bytes_get_data(blob, NULL),
				    g_bytes_get_size(blob),
				    0,
				    g_bytes_get_size(blob),
				    error))
			return FALSE;

		if (!fu_goodixtp_hid_device_set_report(FU_GOODIXTP_HID_DEVICE(self),
						       hidbuf,
						       g_bytes_get_size(blob) + 10,
						       error)) {
			g_prefix_error(error,
				       "failed write data to addr=0x%x, len=%d: ",
				       (guint)fu_chunk_get_address(chk),
				       (gint)g_bytes_get_size(blob));
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_goodixtp_gtx8_device_send_cmd(FuGoodixtpGtx8Device *self,
				 guint8 *buf,
				 gsize bufsz,
				 GError **error)
{
	guint8 hidbuf[PACKAGE_LEN] = {0};

	if (!fu_memcpy_safe(hidbuf, sizeof(hidbuf), 0, buf, bufsz, 0, bufsz, error))
		return FALSE;
	hidbuf[0] = REPORT_ID;
	if (!fu_goodixtp_hid_device_set_report(FU_GOODIXTP_HID_DEVICE(self),
					       hidbuf,
					       bufsz,
					       error)) {
		g_prefix_error(error, "failed to send cmd: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_goodixtp_gtx8_device_ensure_version(FuGoodixtpGtx8Device *self, GError **error)
{
	guint8 fw_info[72] = {0};
	guint8 vice_ver;
	guint8 inter_ver;
	guint8 cfg_ver = 0x0;
	guint8 chksum;
	guint32 patch_vid_raw;
	g_autofree gchar *patch_pid = NULL;

	if (!fu_goodixtp_gtx8_device_hid_read(self, 0x60DC, &cfg_ver, 1, error)) {
		g_prefix_error(error, "failed to read cfg version: ");
		return FALSE;
	}
	if (!fu_goodixtp_gtx8_device_hid_read(self, 0x452C, fw_info, sizeof(fw_info), error)) {
		g_prefix_error(error, "failed to read firmware version: ");
		return FALSE;
	}

	/*check fw version*/
	chksum = fu_sum8(fw_info, sizeof(fw_info));
	if (chksum != 0x0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "fw version check sum error: %d",
			    chksum);
		return FALSE;
	}

	patch_pid = fu_memstrsafe(fw_info, sizeof(fw_info), 0x9, 5, NULL);
	if (patch_pid != NULL)
		fu_goodixtp_hid_device_set_patch_pid(FU_GOODIXTP_HID_DEVICE(self), patch_pid);
	patch_vid_raw = fu_memread_uint32(fw_info + 17, G_BIG_ENDIAN);
	if (patch_vid_raw != 0) {
		g_autofree gchar *patch_vid = g_strdup_printf("%04X", patch_vid_raw);
		fu_goodixtp_hid_device_set_patch_vid(FU_GOODIXTP_HID_DEVICE(self), patch_vid);
	}

	fu_goodixtp_hid_device_set_sensor_id(FU_GOODIXTP_HID_DEVICE(self), fw_info[21] & 0x0F);
	fu_goodixtp_hid_device_set_config_ver(FU_GOODIXTP_HID_DEVICE(self), cfg_ver);
	vice_ver = fw_info[19];
	inter_ver = fw_info[20];
	fu_device_set_version_raw(FU_DEVICE(self), (vice_ver << 16) | (inter_ver << 8) | cfg_ver);
	return TRUE;
}

static gboolean
fu_goodixtp_gtx8_device_disable_report(FuGoodixtpGtx8Device *self, GError **error)
{
	guint8 buf_disable[] = {0x33, 0x00, 0xCD};
	guint8 buf_confirm[] = {0x35, 0x00, 0xCB};
	guint8 buf[3] = {0};

	for (gint i = 0; i < 3; i++) {
		if (!fu_goodixtp_gtx8_device_hid_write(self,
						       CMD_ADDR,
						       buf_disable,
						       sizeof(buf_disable),
						       error)) {
			g_prefix_error(error, "send close report cmd failed: ");
			return FALSE;
		}
		fu_device_sleep(FU_DEVICE(self), 10);
	}

	if (!fu_goodixtp_gtx8_device_hid_write(self,
					       CMD_ADDR,
					       buf_confirm,
					       sizeof(buf_confirm),
					       error)) {
		g_prefix_error(error, "send confirm cmd failed: ");
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), 30);
	if (!fu_goodixtp_gtx8_device_hid_read(self, CMD_ADDR, buf, sizeof(buf), error)) {
		g_prefix_error(error, "read confirm flag failed: ");
		return FALSE;
	}
	if (buf[1] != 1) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "close report failed, flag[0x%02X]",
			    buf[1]);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_goodixtp_gtx8_device_wait_bl_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuGoodixtpGtx8Device *self = FU_GOODIXTP_GTX8_DEVICE(device);
	guint8 hidbuf[1] = {0};

	if (!fu_goodixtp_gtx8_device_hid_read(self, BL_STATE_ADDR, hidbuf, 1, error))
		return FALSE;
	if (hidbuf[0] != 0xDD) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_READ, "ack=0x%02x", hidbuf[0]);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_goodixtp_gtx8_device_update_prepare(FuGoodixtpGtx8Device *self, GError **error)
{
	guint8 cmd_switch_to_patch[] = {0x00, 0x10, 0x00, 0x00, 0x01, 0x01};
	guint8 cmd_start_update[] = {0x00, 0x11, 0x00, 0x00, 0x01, 0x01};

	/* close report */
	if (!fu_goodixtp_gtx8_device_disable_report(self, error)) {
		g_prefix_error(error, "disable report failed: ");
		return FALSE;
	}

	if (!fu_goodixtp_gtx8_device_send_cmd(self,
					      cmd_switch_to_patch,
					      sizeof(cmd_switch_to_patch),
					      error)) {
		g_prefix_error(error, "failed switch to patch: ");
		return FALSE;
	}

	fu_device_sleep(FU_DEVICE(self), 100);

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_goodixtp_gtx8_device_wait_bl_cb,
				  5,
				  30,
				  NULL,
				  error)) {
		g_prefix_error(error, "wait gtx8 BL status failed: ");
		return FALSE;
	}

	if (!fu_goodixtp_gtx8_device_disable_report(self, error)) {
		g_prefix_error(error, "disable report failed: ");
		return FALSE;
	}

	/* start update */
	if (!fu_goodixtp_gtx8_device_send_cmd(self,
					      cmd_start_update,
					      sizeof(cmd_start_update),
					      error)) {
		g_prefix_error(error, "failed to start update: ");
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), 100);

	return TRUE;
}

static gboolean
fu_goodixtp_gtx8_device_soft_reset_ic(FuGoodixtpGtx8Device *self, GError **error)
{
	guint8 cmd_reset[] = {0x0E, 0x13, 0x00, 0x00, 0x01, 0x01};
	guint8 cmd_switch_ptp_mode[] = {0x03, 0x03, 0x00, 0x00, 0x01, 0x01};

	if (!fu_goodixtp_gtx8_device_send_cmd(self, cmd_reset, sizeof(cmd_reset), error)) {
		g_prefix_error(error, "failed write reset command: ");
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), 100);
	if (!fu_goodixtp_gtx8_device_send_cmd(self,
					      cmd_switch_ptp_mode,
					      sizeof(cmd_switch_ptp_mode),
					      error)) {
		g_prefix_error(error, "failed switch to ptp mode: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_goodixtp_gtx8_device_wait_flash_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuGoodixtpGtx8Device *self = FU_GOODIXTP_GTX8_DEVICE(device);
	guint8 hidbuf[1] = {0};

	if (!fu_goodixtp_gtx8_device_hid_read(self, FLASH_RESULT_ADDR, hidbuf, 1, error))
		return FALSE;
	if (hidbuf[0] != 0xAA) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_READ, "ack=0x%02x", hidbuf[0]);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_goodixtp_gtx8_device_load_sub_firmware_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuGoodixtpGtx8Device *self = FU_GOODIXTP_GTX8_DEVICE(device);
	guint16 check_sum;
	guint8 buf_align4k[RAM_BUFFER_SIZE] = {0};
	guint8 buf_load_flash[15] = {0x0e, 0x12, 0x00, 0x00, 0x06};
	guint8 dummy = 0;
	FuChunk *chk = (FuChunk *)user_data;
	g_autoptr(GBytes) blob = NULL;

	blob = fu_chunk_get_bytes(chk, error);
	if (blob == NULL)
		return FALSE;
	if (!fu_memcpy_safe(buf_align4k,
			    sizeof(buf_align4k),
			    0,
			    g_bytes_get_data(blob, NULL),
			    g_bytes_get_size(blob),
			    0,
			    g_bytes_get_size(blob),
			    error))
		return FALSE;

	if (!fu_goodixtp_gtx8_device_hid_write(self,
					       FLASH_BUFFER_ADDR,
					       buf_align4k,
					       sizeof(buf_align4k),
					       error)) {
		g_prefix_error(error,
			       "failed to load fw bufsz=0x%x, addr=0x%x: ",
			       (guint)sizeof(buf_align4k),
			       (guint)fu_chunk_get_address(chk));
		return FALSE;
	}

	/* inform IC to load 4K data to flash */
	check_sum = fu_sum16w(buf_align4k, sizeof(buf_align4k), G_BIG_ENDIAN);
	fu_memwrite_uint16(buf_load_flash + 5, sizeof(buf_align4k), G_BIG_ENDIAN);
	fu_memwrite_uint16(buf_load_flash + 7, fu_chunk_get_address(chk) >> 8, G_BIG_ENDIAN);
	fu_memwrite_uint16(buf_load_flash + 9, check_sum, G_BIG_ENDIAN);
	if (!fu_goodixtp_gtx8_device_send_cmd(self, buf_load_flash, 11, error)) {
		g_prefix_error(error, "failed write load flash command: ");
		return FALSE;
	}

	fu_device_sleep(device, 80);

	if (!fu_device_retry_full(device,
				  fu_goodixtp_gtx8_device_wait_flash_cb,
				  10,
				  20,
				  NULL,
				  error)) {
		g_prefix_error(error, "wait flash status failed: ");
		return FALSE;
	}

	if (!fu_goodixtp_gtx8_device_hid_write(self,
					       FLASH_RESULT_ADDR,
					       &dummy,
					       sizeof(dummy),
					       error))
		return FALSE;
	fu_device_sleep(device, 5);
	return TRUE;
}

static gboolean
fu_goodixtp_gtx8_device_update_process(FuGoodixtpGtx8Device *self, FuChunk *chk, GError **error)
{
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_goodixtp_gtx8_device_load_sub_firmware_cb,
				  3,
				  10,
				  chk,
				  error)) {
		g_prefix_error(error,
			       "load sub firmware failed, addr=0x%04x: ",
			       (guint)fu_chunk_get_address(chk));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_goodixtp_gtx8_device_setup(FuDevice *device, GError **error)
{
	FuGoodixtpGtx8Device *self = FU_GOODIXTP_GTX8_DEVICE(device);
	if (!fu_goodixtp_gtx8_device_ensure_version(self, error)) {
		g_prefix_error(error, "gtx8 read version failed: ");
		return FALSE;
	}
	return TRUE;
}

static FuFirmware *
fu_goodixtp_gtx8_device_prepare_firmware(FuDevice *device,
					 GInputStream *stream,
					 FuProgress *progress,
					 FwupdInstallFlags flags,
					 GError **error)
{
	FuGoodixtpGtx8Device *self = FU_GOODIXTP_GTX8_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_goodixtp_gtx8_firmware_new();
	if (!fu_goodixtp_gtx8_firmware_parse(
		FU_GOODIXTP_FIRMWARE(firmware),
		stream,
		fu_goodixtp_hid_device_get_sensor_id(FU_GOODIXTP_HID_DEVICE(self)),
		error))
		return NULL;
	return g_steal_pointer(&firmware);
}

static gboolean
fu_goodixtp_gtx8_device_write_image(FuGoodixtpGtx8Device *self,
				    FuFirmware *img,
				    FuProgress *progress,
				    GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GInputStream) stream = NULL;

	stream = fu_firmware_get_stream(img, error);
	if (stream == NULL)
		return FALSE;
	chunks = fu_chunk_array_new_from_stream(stream,
						fu_firmware_get_addr(img),
						FU_CHUNK_PAGESZ_NONE,
						RAM_BUFFER_SIZE,
						error);
	if (chunks == NULL)
		return FALSE;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_goodixtp_gtx8_device_update_process(self, chk, error))
			return FALSE;
		fu_device_sleep(FU_DEVICE(self), 20);
		fu_progress_step_done(progress);
	}
	return TRUE;
}

static gboolean
fu_goodixtp_gtx8_device_write_images(FuGoodixtpGtx8Device *self,
				     GPtrArray *imgs,
				     FuProgress *progress,
				     GError **error)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, imgs->len);
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		if (!fu_goodixtp_gtx8_device_write_image(self,
							 img,
							 fu_progress_get_child(progress),
							 error))
			return FALSE;
		fu_progress_step_done(progress);
	}
	return TRUE;
}

static gboolean
fu_goodixtp_gtx8_device_write_firmware(FuDevice *device,
				       FuFirmware *firmware,
				       FuProgress *progress,
				       FwupdInstallFlags flags,
				       GError **error)
{
	FuGoodixtpGtx8Device *self = FU_GOODIXTP_GTX8_DEVICE(device);
	FuGoodixtpFirmware *firmware_goodixtp = FU_GOODIXTP_FIRMWARE(firmware);
	guint32 fw_ver = fu_goodixtp_firmware_get_version(firmware_goodixtp);
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 10, "prepare");
	fu_progress_add_step(progress, FWUPD_STATUS_DOWNLOADING, 85, "download");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, "reload");

	if (!fu_goodixtp_gtx8_device_update_prepare(self, error))
		return FALSE;
	fu_progress_step_done(progress);
	if (!fu_goodixtp_gtx8_device_write_images(self,
						  imgs,
						  fu_progress_get_child(progress),
						  error))
		return FALSE;
	fu_progress_step_done(progress);

	/* reset IC */
	if (!fu_goodixtp_gtx8_device_soft_reset_ic(self, error))
		return FALSE;
	if (!fu_goodixtp_gtx8_device_ensure_version(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	if (fu_device_get_version_raw(device) != fw_ver) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "update failed chip_ver:%x != bin_ver:%x",
			    (guint)fu_device_get_version_raw(device),
			    (guint)fw_ver);
		return FALSE;
	}
	return TRUE;
}

static void
fu_goodixtp_gtx8_device_init(FuGoodixtpGtx8Device *self)
{
}

static void
fu_goodixtp_gtx8_device_class_init(FuGoodixtpGtx8DeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

	device_class->setup = fu_goodixtp_gtx8_device_setup;
	device_class->reload = fu_goodixtp_gtx8_device_setup;
	device_class->prepare_firmware = fu_goodixtp_gtx8_device_prepare_firmware;
	device_class->write_firmware = fu_goodixtp_gtx8_device_write_firmware;
}
