/*
 * Copyright (C) 2023 Goodix.inc <xulinkun@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-goodixtp-brlb-device.h"
#include "fu-goodixtp-brlb-firmware.h"
#include "fu-goodixtp-common.h"

struct _FuGoodixtpBrlbDevice {
	FuGoodixtpHidDevice parent_instance;
};

G_DEFINE_TYPE(FuGoodixtpBrlbDevice, fu_goodixtp_brlb_device, FU_TYPE_GOODIXTP_HID_DEVICE)

static gboolean
fu_goodixtp_brlb_device_read_pkg(FuGoodixtpBrlbDevice *self,
				 guint32 addr,
				 guint8 *buf,
				 guint32 bufsz,
				 GError **error)
{
	guint8 hidbuf[PACKAGE_LEN] = {0};

	hidbuf[0] = REPORT_ID;
	hidbuf[1] = I2C_DIRECT_RW;
	hidbuf[2] = 0;
	hidbuf[3] = 0;
	hidbuf[4] = 7;
	hidbuf[5] = I2C_READ_FLAG;
	fu_memwrite_uint32(hidbuf + 6, addr, G_BIG_ENDIAN);
	fu_memwrite_uint16(hidbuf + 10, bufsz, G_BIG_ENDIAN);
	if (!fu_goodixtp_hid_device_set_report(FU_GOODIXTP_HID_DEVICE(self), hidbuf, 12, error))
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
fu_goodixtp_brlb_device_hid_read(FuGoodixtpBrlbDevice *self,
				 guint32 addr,
				 guint8 *buf,
				 guint32 bufsz,
				 GError **error)
{
	g_autoptr(GPtrArray) chunks = fu_chunk_array_new(buf, bufsz, addr, 0, PACKAGE_LEN - 12);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		if (!fu_goodixtp_brlb_device_read_pkg(self,
						      fu_chunk_get_address(chk),
						      (guint8 *)fu_chunk_get_data(chk),
						      fu_chunk_get_data_sz(chk),
						      error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_goodixtp_brlb_device_hid_write(FuGoodixtpBrlbDevice *self,
				  guint32 addr,
				  guint8 *buf,
				  guint32 bufsz,
				  GError **error)
{
	g_autoptr(GPtrArray) chunks = fu_chunk_array_new(buf, bufsz, addr, 0, PACKAGE_LEN - 12);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		guint8 hidbuf[PACKAGE_LEN] = {0};
		hidbuf[0] = REPORT_ID;
		hidbuf[1] = I2C_DIRECT_RW;
		if (i == chunks->len - 1)
			hidbuf[2] = 0x00;
		else
			hidbuf[2] = 0x01;
		hidbuf[3] = i;
		hidbuf[4] = fu_chunk_get_data_sz(chk) + 7;
		hidbuf[5] = I2C_WRITE_FLAG;
		fu_memwrite_uint32(hidbuf + 6, fu_chunk_get_address(chk), G_BIG_ENDIAN);
		fu_memwrite_uint16(hidbuf + 10, fu_chunk_get_data_sz(chk), G_BIG_ENDIAN);
		if (!fu_memcpy_safe(hidbuf,
				    sizeof(hidbuf),
				    12,
				    fu_chunk_get_data(chk),
				    fu_chunk_get_data_sz(chk),
				    0,
				    fu_chunk_get_data_sz(chk),
				    error))
			return FALSE;
		if (!fu_goodixtp_hid_device_set_report(FU_GOODIXTP_HID_DEVICE(self),
						       hidbuf,
						       fu_chunk_get_data_sz(chk) + 12,
						       error)) {
			g_prefix_error(error,
				       "failed write data to addr=0x%x, len=%d: ",
				       fu_chunk_get_address(chk),
				       (gint)fu_chunk_get_data_sz(chk));
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_goodixtp_brlb_device_send_cmd(FuGoodixtpBrlbDevice *self,
				 guint8 cmd,
				 guint8 *buf,
				 gsize bufsz,
				 GError **error)
{
	guint8 hidbuf[PACKAGE_LEN] = {0};

	hidbuf[0] = REPORT_ID;
	hidbuf[1] = cmd;
	hidbuf[2] = 0x00;
	hidbuf[3] = 0x00;
	hidbuf[4] = (guint8)bufsz;
	if (!fu_memcpy_safe(hidbuf, sizeof(hidbuf), 5, buf, bufsz, 0, bufsz, error))
		return FALSE;
	if (!fu_goodixtp_hid_device_set_report(FU_GOODIXTP_HID_DEVICE(self),
					       hidbuf,
					       bufsz + 5,
					       error)) {
		g_prefix_error(error, "send cmd[%x] failed: ", cmd);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_goodixtp_brlb_device_ensure_version(FuGoodixtpBrlbDevice *self, GError **error)
{
	guint8 hidbuf[14] = {0};
	guint8 vice_ver;
	guint8 inter_ver;
	guint8 cfg_ver;
	guint32 version;
	guint32 patch_vid_raw;
	g_autofree gchar *patch_pid = NULL;

	if (!fu_goodixtp_brlb_device_hid_read(self, 0x1001E, hidbuf, sizeof(hidbuf), error)) {
		g_prefix_error(error, "failed read PID/VID: ");
		return FALSE;
	}
	vice_ver = hidbuf[10];
	inter_ver = hidbuf[11];

	patch_pid = fu_strsafe((const gchar *)hidbuf + 0, 8);
	if (patch_pid != NULL)
		fu_goodixtp_hid_device_set_patch_pid(FU_GOODIXTP_HID_DEVICE(self), patch_pid);
	patch_vid_raw = fu_memread_uint32(hidbuf + 8, G_BIG_ENDIAN);
	if (patch_vid_raw != 0) {
		g_autofree gchar *patch_vid = g_strdup_printf("%04X", patch_vid_raw);
		fu_goodixtp_hid_device_set_patch_vid(FU_GOODIXTP_HID_DEVICE(self), patch_vid);
	}

	fu_goodixtp_hid_device_set_sensor_id(FU_GOODIXTP_HID_DEVICE(self), hidbuf[13]);

	if (!fu_goodixtp_brlb_device_hid_read(self, 0x10076, hidbuf, 5, error)) {
		g_prefix_error(error, "Failed read config id/version: ");
		return FALSE;
	}

	cfg_ver = hidbuf[4];
	fu_goodixtp_hid_device_set_config_ver(FU_GOODIXTP_HID_DEVICE(self), cfg_ver);
	version = (vice_ver << 16) | (inter_ver << 8) | cfg_ver;
	fu_device_set_version_raw(FU_DEVICE(self), version);
	fu_device_set_version_from_uint32(FU_DEVICE(self), version);

	return TRUE;
}

static gboolean
fu_goodixtp_brlb_device_wait_mini_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuGoodixtpBrlbDevice *self = FU_GOODIXTP_BRLB_DEVICE(device);
	guint8 hidbuf[1] = {0x0};

	if (!fu_goodixtp_brlb_device_hid_read(self, 0x10010, hidbuf, sizeof(hidbuf), error))
		return FALSE;
	if (hidbuf[0] != 0xDD) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_READ, "ack=0x%02x", hidbuf[0]);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_goodixtp_brlb_device_wait_erase_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuGoodixtpBrlbDevice *self = FU_GOODIXTP_BRLB_DEVICE(device);
	guint8 hidbuf[5];
	guint8 recvBuf[5] = {0x0};

	memset(hidbuf, 0x55, sizeof(hidbuf));
	if (!fu_goodixtp_brlb_device_hid_write(self, 0x14000, hidbuf, 5, error))
		return FALSE;
	if (!fu_goodixtp_brlb_device_hid_read(self, 0x14000, recvBuf, 5, error))
		return FALSE;
	if (memcmp(hidbuf, recvBuf, 5)) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_READ, "sram not ready");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_goodixtp_brlb_device_update_prepare(FuGoodixtpBrlbDevice *self, GError **error)
{
	guint8 cmdbuf[1];

	/* step 1. switch mini system */
	cmdbuf[0] = 0x01;
	if (!fu_goodixtp_brlb_device_send_cmd(self, 0x10, cmdbuf, sizeof(cmdbuf), error)) {
		g_prefix_error(error, "failed send minisystem cmd: ");
		return FALSE;
	}

	fu_device_sleep(FU_DEVICE(self), 100);
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_goodixtp_brlb_device_wait_mini_cb,
				  5,
				  30,
				  NULL,
				  error)) {
		g_prefix_error(error, "wait brlb minisystem status failed: ");
		return FALSE;
	}
	g_debug("switch mini system successfully");

	/* step 2. erase flash */
	cmdbuf[0] = 0x01;
	if (!fu_goodixtp_brlb_device_send_cmd(self, 0x11, cmdbuf, sizeof(cmdbuf), error)) {
		g_prefix_error(error, "Failed send erase flash cmd: ");
		return FALSE;
	}

	fu_device_sleep(FU_DEVICE(self), 50);
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_goodixtp_brlb_device_wait_erase_cb,
				  5,
				  20,
				  NULL,
				  error)) {
		g_prefix_error(error, "wait brlb erase status failed: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_goodixtp_brlb_device_wait_flash_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuGoodixtpBrlbDevice *self = FU_GOODIXTP_BRLB_DEVICE(device);
	guint8 hidbuf[1] = {0};

	if (!fu_goodixtp_brlb_device_hid_read(self, 0x10011, hidbuf, 1, error)) {
		g_prefix_error(error, "Failed to read 0x10011");
		return FALSE;
	}
	if (hidbuf[0] != 0xAA) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_READ, "ack=0x%02x", hidbuf[0]);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_goodixtp_brlb_device_load_sub_firmware_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuGoodixtpBrlbDevice *self = FU_GOODIXTP_BRLB_DEVICE(device);
	FuChunk *chk = (FuChunk *)user_data;
	guint32 checksum = 0;
	guint8 buf_align4k[RAM_BUFFER_SIZE] = {0};
	guint8 cmdbuf[10] = {0};

	if (!fu_memcpy_safe(buf_align4k,
			    sizeof(buf_align4k),
			    0,
			    (guint8 *)fu_chunk_get_data(chk),
			    fu_chunk_get_data_sz(chk),
			    0,
			    fu_chunk_get_data_sz(chk),
			    error))
		return FALSE;

	/* send fw data to dram */
	if (!fu_goodixtp_brlb_device_hid_write(self,
					       0x14000,
					       buf_align4k,
					       sizeof(buf_align4k),
					       error)) {
		g_prefix_error(error, "write fw data failed: ");
		return FALSE;
	}

	/* send checksum */
	for (guint i = 0; i < sizeof(buf_align4k); i += 2) {
		guint16 tmp_val;
		if (!fu_memread_uint16_safe(buf_align4k,
					    sizeof(buf_align4k),
					    i,
					    &tmp_val,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
		checksum += tmp_val;
	}

	fu_memwrite_uint16(cmdbuf, sizeof(buf_align4k), G_BIG_ENDIAN);
	fu_memwrite_uint32(cmdbuf + 2, fu_chunk_get_address(chk), G_BIG_ENDIAN);
	fu_memwrite_uint32(cmdbuf + 6, checksum, G_BIG_ENDIAN);
	if (!fu_goodixtp_brlb_device_send_cmd(self, 0x12, cmdbuf, sizeof(cmdbuf), error)) {
		g_prefix_error(error, "failed send start update cmd: ");
		return FALSE;
	}

	fu_device_sleep(device, 80);

	/* wait update finish */
	if (!fu_device_retry_full(device,
				  fu_goodixtp_brlb_device_wait_flash_cb,
				  10,
				  20,
				  NULL,
				  error)) {
		g_prefix_error(error, "wait flash status failed: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_goodixtp_brlb_device_update_process(FuGoodixtpBrlbDevice *self, FuChunk *chk, GError **error)
{
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_goodixtp_brlb_device_load_sub_firmware_cb,
				  3,
				  10,
				  chk,
				  error)) {
		g_prefix_error(error,
			       "load sub firmware failed, addr:0x%04x: ",
			       fu_chunk_get_address(chk));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_goodixtp_brlb_device_update_finish(FuGoodixtpBrlbDevice *self, GError **error)
{
	guint8 cmdbuf[1] = {0x01};

	/* reset IC */
	if (!fu_goodixtp_brlb_device_send_cmd(self, 0x13, cmdbuf, sizeof(cmdbuf), error)) {
		g_prefix_error(error, "failed reset IC: ");
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), 100);
	return TRUE;
}

static gboolean
fu_goodixtp_brlb_device_setup(FuDevice *device, GError **error)
{
	FuGoodixtpBrlbDevice *self = FU_GOODIXTP_BRLB_DEVICE(device);
	if (!fu_goodixtp_brlb_device_ensure_version(self, error)) {
		g_prefix_error(error, "brlb read version failed: ");
		return FALSE;
	}
	return TRUE;
}

static FuFirmware *
fu_goodixtp_brlb_device_prepare_firmware(FuDevice *device,
					 GBytes *fw,
					 FwupdInstallFlags flags,
					 GError **error)
{
	FuGoodixtpBrlbDevice *self = FU_GOODIXTP_BRLB_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_goodixtp_brlb_firmware_new();
	if (!fu_goodixtp_brlb_firmware_parse(
		FU_GOODIXTP_FIRMWARE(firmware),
		fw,
		fu_goodixtp_hid_device_get_sensor_id(FU_GOODIXTP_HID_DEVICE(self)),
		error))
		return NULL;
	return g_steal_pointer(&firmware);
}

static gboolean
fu_goodixtp_brlb_device_write_image(FuGoodixtpBrlbDevice *self,
				    FuFirmware *img,
				    FuProgress *progress,
				    GError **error)
{
	g_autoptr(GBytes) blob = fu_firmware_get_bytes(img, NULL);
	g_autoptr(GPtrArray) chunks =
	    fu_chunk_array_new_from_bytes(blob, fu_firmware_get_addr(img), 0x0, RAM_BUFFER_SIZE);

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		if (!fu_goodixtp_brlb_device_update_process(self, chk, error))
			return FALSE;
		fu_device_sleep(FU_DEVICE(self), 20);
		fu_progress_step_done(progress);
	}
	return TRUE;
}

static gboolean
fu_goodixtp_brlb_device_write_images(FuGoodixtpBrlbDevice *self,
				     GPtrArray *imgs,
				     FuProgress *progress,
				     GError **error)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, imgs->len);
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		if (!fu_goodixtp_brlb_device_write_image(self,
							 img,
							 fu_progress_get_child(progress),
							 error))
			return FALSE;
		fu_progress_step_done(progress);
	}
	return TRUE;
}

static gboolean
fu_goodixtp_brlb_device_write_firmware(FuDevice *device,
				       FuFirmware *firmware,
				       FuProgress *progress,
				       FwupdInstallFlags flags,
				       GError **error)
{
	FuGoodixtpBrlbDevice *self = FU_GOODIXTP_BRLB_DEVICE(device);
	guint32 fw_ver = fu_goodixtp_firmware_get_version(FU_GOODIXTP_FIRMWARE(firmware));
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 10, "prepare");
	fu_progress_add_step(progress, FWUPD_STATUS_DOWNLOADING, 85, "download");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, "reload");

	if (!fu_goodixtp_brlb_device_update_prepare(self, error))
		return FALSE;
	fu_progress_step_done(progress);
	if (!fu_goodixtp_brlb_device_write_images(self,
						  imgs,
						  fu_progress_get_child(progress),
						  error))
		return FALSE;
	fu_progress_step_done(progress);
	if (!fu_goodixtp_brlb_device_update_finish(self, error))
		return FALSE;
	if (!fu_goodixtp_brlb_device_ensure_version(self, error))
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
fu_goodixtp_brlb_device_init(FuGoodixtpBrlbDevice *self)
{
}

static void
fu_goodixtp_brlb_device_class_init(FuGoodixtpBrlbDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->setup = fu_goodixtp_brlb_device_setup;
	klass_device->reload = fu_goodixtp_brlb_device_setup;
	klass_device->prepare_firmware = fu_goodixtp_brlb_device_prepare_firmware;
	klass_device->write_firmware = fu_goodixtp_brlb_device_write_firmware;
}
