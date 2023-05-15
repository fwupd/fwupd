/*
 * Copyright (C) 2023 Goodix.inc <xulinkun@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-goodixtp-common.h"
#include "fu-goodixtp-gtx8-device.h"
#include "fu-goodixtp-gtx8-firmware.h"

struct _FuGoodixtpGtx8Device {
	FuGoodixtpHidDevice parent_instance;
};

G_DEFINE_TYPE(FuGoodixtpGtx8Device, fu_goodixtp_gtx8_device, FU_TYPE_GOODIXTP_HID_DEVICE)

#define CMD_ADDR       0x60CC

#define BL_STATE_ADDR	  0x5095
#define FLASH_RESULT_ADDR 0x5096
#define FLASH_BUFFER_ADDR 0xC000

static gboolean
read_pkg(FuDevice *device, guint32 addr, guint8 *buf, guint32 len, GError **error)
{
	guint8 HidBuf[PACKAGE_LEN] = {0};

	HidBuf[0] = REPORT_ID;
	HidBuf[1] = I2C_DIRECT_RW;
	HidBuf[2] = 0;
	HidBuf[3] = 0;
	HidBuf[4] = 5;
	HidBuf[5] = I2C_READ_FLAG;
	HidBuf[6] = (addr >> 8) & 0xFF;
	HidBuf[7] = addr & 0xFF;
	HidBuf[8] = (len >> 8) & 0xFF;
	HidBuf[9] = len & 0xFF;
	if (!fu_goodixtp_hid_device_set_report(device, HidBuf, 10, error))
		return FALSE;

	if (!fu_goodixtp_hid_device_get_report(device, HidBuf, error))
		return FALSE;

	if (HidBuf[3] != 0 || HidBuf[4] != len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "Failed to read_pkg, HidBuf[3]:%d HidBuf[4]:%d",
			    HidBuf[3],
			    HidBuf[4]);
		return FALSE;
	}
	memcpy(buf, &HidBuf[5], HidBuf[4]);
	return TRUE;
}

static gboolean
hid_read(FuDevice *device, guint32 addr, guint8 *buf, guint32 len, GError **error)
{
	guint32 tmp_addr = addr;
	gint pkg_size = PACKAGE_LEN - 10;
	gint pkg_num;
	gint remain_size;
	gint offset = 0;
	gint i;

	pkg_num = len / pkg_size;
	remain_size = len % pkg_size;
	for (i = 0; i < pkg_num; i++) {
		if (!read_pkg(device, tmp_addr + offset, &buf[offset], pkg_size, error))
			return FALSE;
		offset += pkg_size;
	}

	if (remain_size > 0) {
		if (!read_pkg(device, tmp_addr + offset, &buf[offset], remain_size, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
hid_write(FuDevice *device, guint32 addr, guint8 *buf, guint32 len, GError **error)
{
	guint8 HidBuf[PACKAGE_LEN] = {0};
	guint32 current_addr = addr;
	guint32 transfer_length = 0;
	guint32 pos = 0;
	guint8 pkg_num = 0;

	while (pos != len) {
		HidBuf[0] = REPORT_ID;
		HidBuf[1] = I2C_DIRECT_RW;
		if (len - pos > PACKAGE_LEN - 10) {
			transfer_length = PACKAGE_LEN - 10;
			HidBuf[2] = 0x01;
		} else {
			transfer_length = len - pos;
			HidBuf[2] = 0x00;
		}
		HidBuf[3] = pkg_num++;
		HidBuf[4] = transfer_length + 5;
		HidBuf[5] = I2C_WRITE_FLAG;
		HidBuf[6] = (current_addr >> 8) & 0xFF;
		HidBuf[7] = current_addr & 0xFF;
		HidBuf[8] = (transfer_length >> 8) & 0xFF;
		HidBuf[9] = transfer_length & 0xFF;
		memcpy(&HidBuf[10], &buf[pos], transfer_length);
		if (!fu_goodixtp_hid_device_set_report(device,
						       HidBuf,
						       transfer_length + 10,
						       error)) {
			g_debug("Failed write data to addr=0x%x, len=%d",
				current_addr,
				(gint)transfer_length);
			return FALSE;
		}
		pos += transfer_length;
		current_addr += transfer_length;
	}
	return TRUE;
}

static gboolean
send_cmd(FuDevice *device, guint8 *buf, guint32 len, GError **error)
{
	guint8 temp_buf[PACKAGE_LEN] = {0};

	memcpy(temp_buf, buf, len);
	temp_buf[0] = REPORT_ID;
	if (!fu_goodixtp_hid_device_set_report(device, temp_buf, len, error)) {
		g_debug("failed to set feature");
		return FALSE;
	}

	return TRUE;
}

static gboolean
gtx8_get_version(FuDevice *device, gpointer user_data, GError **error)
{
	struct FuGoodixVersion *ver = (struct FuGoodixVersion *)user_data;
	guint8 fw_info[72] = {0};
	guint8 vice_ver;
	guint8 inter_ver;
	guint8 cfg_ver;
	guint8 chksum;

	if (!hid_read(device, 0x60DC, &cfg_ver, 1, error)) {
		g_debug("Failed read cfg version");
		g_prefix_error(error, "Failed read cfg version,");
		return FALSE;
	}

	if (!hid_read(device, 0x452C, fw_info, sizeof(fw_info), error)) {
		g_debug("Failed read firmware version");
		g_prefix_error(error, "Failed read firmware version,");
		return FALSE;
	}

	/*check fw version*/
	chksum = fu_sum8(fw_info, sizeof(fw_info));
	if (chksum) {
		g_debug("fw version check sum error:%d", chksum);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "fw version check sum error:%d",
			    chksum);
		return FALSE;
	}

	memcpy(ver->patch_pid, &fw_info[9], 4);
	ver->sensor_id = fw_info[21] & 0x0F;
	memcpy(ver->patch_vid, &fw_info[17], 4);
	vice_ver = fw_info[19];
	inter_ver = fw_info[20];
	ver->cfg_ver = cfg_ver;
	ver->ver_num = (vice_ver << 16) | (inter_ver << 8) | cfg_ver;

	return TRUE;
}

static gboolean
disable_report(FuDevice *device, GError **error)
{
	guint8 cmdDisable[] = {0x33, 0x00, 0xCD};
	guint8 cmdConfirm[] = {0x35, 0x00, 0xCB};
	guint8 buf[3] = {0};

	for (gint i = 0; i < 3; i++) {
		if (!hid_write(device, CMD_ADDR, cmdDisable, sizeof(cmdDisable), error)) {
			g_debug("send close report cmd failed");
			return FALSE;
		}
		fu_device_sleep(device, 10);
	}

	if (!hid_write(device, CMD_ADDR, cmdConfirm, sizeof(cmdConfirm), error)) {
		g_debug("send confirm cmd failed");
		return FALSE;
	}
	fu_device_sleep(device, 30);
	if (!hid_read(device, CMD_ADDR, buf, sizeof(buf), error)) {
		g_debug("read confirm flag failed");
		return FALSE;
	}

	if (buf[1] != 1) {
		g_debug("close report failed, flag[0x%02X]", buf[1]);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "close report failed, flag[0x%02X]",
			    buf[1]);
		return FALSE;
	}

	g_debug("close report success");
	return TRUE;
}

static gboolean
gtx8_update_prepare(FuDevice *device, GError **error)
{
	guint8 temp_buf[PACKAGE_LEN];
	guint8 cmd_switch_to_patch[] = {0x00, 0x10, 0x00, 0x00, 0x01, 0x01};
	guint8 cmd_start_update[] = {0x00, 0x11, 0x00, 0x00, 0x01, 0x01};
	gint retry = 5;

	/* close report */
	if (!disable_report(device, error)) {
		g_debug("disable report failed");
		g_prefix_error(error, "disable report failed,");
		return FALSE;
	}

	if (!send_cmd(device, cmd_switch_to_patch, sizeof(cmd_switch_to_patch), error)) {
		g_debug("Failed switch to patch");
		g_prefix_error(error, "Failed switch to patch,");
		return FALSE;
	}

	fu_device_sleep(device, 100);
	do {
		if (!hid_read(device, BL_STATE_ADDR, temp_buf, 1, error)) {
			g_debug("Failed read 0x%x", (guint)BL_STATE_ADDR);
			return FALSE;
		}
		if (temp_buf[0] == 0xDD)
			break;
		g_debug("0x%x value is 0x%x != 0xDD, retry", (guint)BL_STATE_ADDR, temp_buf[0]);
		fu_device_sleep(device, 30);
	} while (--retry);
	if (retry == 0) {
		g_debug("Reg 0x%x != 0xDD", (guint)BL_STATE_ADDR);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Reg 0x%x != 0xDD",
			    (guint)BL_STATE_ADDR);
		return FALSE;
	}

	if (!disable_report(device, error)) {
		g_debug("disable report failed");
		g_prefix_error(error, "disable report failed,");
		return FALSE;
	}

	/* Start update */
	if (!send_cmd(device, cmd_start_update, sizeof(cmd_start_update), error)) {
		g_debug("Failed start update");
		g_prefix_error(error, "Failed start update,");
		return FALSE;
	}
	fu_device_sleep(device, 100);

	return TRUE;
}

static gboolean
soft_reset_ic(FuDevice *device, GError **error)
{
	guint8 cmd_reset[] = {0x0E, 0x13, 0x00, 0x00, 0x01, 0x01};
	guint8 cmd_switch_ptp_mode[] = {0x03, 0x03, 0x00, 0x00, 0x01, 0x01};
	gint retry = 3;

	g_debug("reset ic");
	while (retry--) {
		if (send_cmd(device, cmd_reset, sizeof(cmd_reset), error))
			break;
		fu_device_sleep(device, 20);
	}
	if (retry < 0) {
		g_debug("Failed write restart command");
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Failed write restart command");
		return FALSE;
	}

	fu_device_sleep(device, 100);
	if (!send_cmd(device, cmd_switch_ptp_mode, sizeof(cmd_switch_ptp_mode), error)) {
		g_debug("Failed switch to ptp mode");
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Failed switch to ptp mode");
		return FALSE;
	}
	return TRUE;
}

static gboolean
load_sub_firmware_cb(FuDevice *parent, gpointer user_data, GError **error)
{
	gint retry;
	guint16 check_sum;
	guint8 dummy = 0;
	guint8 temp_buf[PACKAGE_LEN] = {0};
	guint8 buf_load_flash[15] = {0x0e, 0x12, 0x00, 0x00, 0x06};
	struct FuGoodixTransferData *pkg = (struct FuGoodixTransferData *)user_data;

	if (!hid_write(parent, FLASH_BUFFER_ADDR, pkg->buf, pkg->len, error)) {
		g_debug("Failed load fw, len %d : addr 0x%x", (gint)pkg->len, pkg->addr);
		g_prefix_error(error, "Failed load fw,");
		return FALSE;
	}

	/* inform IC to load 4K data to flash */
	check_sum = fu_sum16w(pkg->buf, pkg->len, G_BIG_ENDIAN);
	buf_load_flash[5] = (pkg->len >> 8) & 0xFF;
	buf_load_flash[6] = pkg->len & 0xFF;
	buf_load_flash[7] = (pkg->addr >> 16) & 0xFF;
	buf_load_flash[8] = (pkg->addr >> 8) & 0xFF;
	buf_load_flash[9] = (check_sum >> 8) & 0xFF;
	buf_load_flash[10] = check_sum & 0xFF;

	if (!send_cmd(parent, buf_load_flash, 11, error)) {
		g_debug("Failed write load flash command");
		g_prefix_error(error, "Failed write load flash command,");
		return FALSE;
	}

	fu_device_sleep(parent, 80);
	retry = 10;
	do {
		memset(temp_buf, 0, sizeof(temp_buf));
		if (!hid_read(parent, FLASH_RESULT_ADDR, temp_buf, 1, error)) {
			g_debug("read flash result failed");
			g_prefix_error(error, "read flash result failed,");
			return FALSE;
		}
		if (temp_buf[0] == 0xAA)
			break;
		fu_device_sleep(parent, 20);
	} while (--retry);
	if (retry == 0) {
		g_debug("read flash result 0x%02x != 0xAA", temp_buf[0]);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "read flash result 0x%02x != 0xAA",
			    temp_buf[0]);
		return FALSE;
	}
	hid_write(parent, FLASH_RESULT_ADDR, &dummy, 1, error);
	fu_device_sleep(parent, 5);
	return TRUE;
}

static gboolean
gtx8_update_process(FuDevice *device, guint32 flash_addr, guint8 *buf, guint32 len, GError **error)
{
	struct FuGoodixTransferData pkg;

	pkg.addr = flash_addr;
	pkg.buf = buf;
	pkg.len = len;
	if (!fu_device_retry_full(device, load_sub_firmware_cb, 3, 10, &pkg, error)) {
		g_debug("load sub firmware failed, addr:0x%04x", flash_addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
gtx8_update_finish(FuDevice *device, GError **error)
{
	/* reset IC */
	return soft_reset_ic(device, error);
}

static gboolean
fu_goodixtp_gtx8_device_setup(FuDevice *device, GError **error)
{
	FuGoodixtpHidDevice *self = FU_GOODIXTP_HID_DEVICE(device);
	struct FuGoodixVersion tmp_ver;

	if (!gtx8_get_version(device, &tmp_ver, error)) {
		g_prefix_error(error, "gtx8 read version failed,");
		return FALSE;
	}
	fu_goodixtp_hid_device_set_version(self, &tmp_ver);
	fu_device_set_version_from_uint32(device, tmp_ver.ver_num);
	return TRUE;
}

static FuFirmware *
fu_goodixtp_gtx8_device_prepare_firmware(FuDevice *device,
					 GBytes *fw,
					 FwupdInstallFlags flags,
					 GError **error)
{
	FuGoodixtpHidDevice *self = FU_GOODIXTP_HID_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_goodixtp_gtx8_firmware_new();
	if (!fu_goodixtp_gtx8_firmware_parse(FU_GOODIXTP_FIRMWARE(firmware),
					     fw,
					     fu_goodixtp_hid_device_get_sensor_id(self),
					     error))
		return NULL;
	return g_steal_pointer(&firmware);
}

static gboolean
fu_goodixtp_gtx8_device_write_firmware(FuDevice *device,
				       FuFirmware *firmware,
				       FuProgress *progress,
				       FwupdInstallFlags flags,
				       GError **error)
{
	FuGoodixtpFirmware *firmware_goodixtp = FU_GOODIXTP_FIRMWARE(firmware);
	guint8 *buf = fu_goodixtp_firmware_get_data(firmware_goodixtp);
	gint bufsz = fu_goodixtp_firmware_get_len(firmware_goodixtp);
	guint32 fw_ver = fu_goodixtp_firmware_get_version(firmware_goodixtp);
	struct FuGoodixVersion ic_ver;
	g_autoptr(GPtrArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 10, "prepare");
	fu_progress_add_step(progress, FWUPD_STATUS_DOWNLOADING, 85, "download");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, "reload");

	if (!gtx8_update_prepare(device, error))
		return FALSE;
	fu_progress_step_done(progress);
	chunks = fu_chunk_array_new(buf, bufsz, 0x0, 0x0, RAM_BUFFER_SIZE);
	for (gint i = 0; i < (gint)chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		guint32 addr = fu_goodixtp_firmware_get_addr(firmware_goodixtp, i);
		if (!gtx8_update_process(device,
					 addr,
					 (guint8 *)fu_chunk_get_data(chk),
					 fu_chunk_get_data_sz(chk),
					 error)) {
			return FALSE;
		}
		fu_device_sleep(device, 20);
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						(gsize)i + 1,
						(gsize)chunks->len);
	}
	fu_progress_step_done(progress);

	if (!gtx8_update_finish(device, error))
		return FALSE;
	if (!gtx8_get_version(device, &ic_ver, error))
		return FALSE;
	fu_progress_step_done(progress);

	if (ic_ver.ver_num != fw_ver) {
		g_debug("update failed chip_ver:%x != bin_ver:%x",
			(guint)ic_ver.ver_num,
			(guint)fw_ver);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "update failed chip_ver:%x != bin_ver:%x",
			    (guint)ic_ver.ver_num,
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
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);

	klass_device->setup = fu_goodixtp_gtx8_device_setup;
	klass_device->reload = fu_goodixtp_gtx8_device_setup;
	klass_device->prepare_firmware = fu_goodixtp_gtx8_device_prepare_firmware;
	klass_device->write_firmware = fu_goodixtp_gtx8_device_write_firmware;
}
