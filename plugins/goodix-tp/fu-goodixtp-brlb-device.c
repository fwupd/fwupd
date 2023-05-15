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
read_pkg(FuDevice *device, guint32 addr, guint8 *buf, guint32 len, GError **error)
{
	guint8 HidBuf[PACKAGE_LEN] = {0};

	HidBuf[0] = REPORT_ID;
	HidBuf[1] = I2C_DIRECT_RW;
	HidBuf[2] = 0;
	HidBuf[3] = 0;
	HidBuf[4] = 7;
	HidBuf[5] = I2C_READ_FLAG;
	HidBuf[6] = (addr >> 24) & 0xFF;
	HidBuf[7] = (addr >> 16) & 0xFF;
	HidBuf[8] = (addr >> 8) & 0xFF;
	HidBuf[9] = addr & 0xFF;
	HidBuf[10] = (len >> 8) & 0xFF;
	HidBuf[11] = len & 0xFF;
	if (!fu_goodixtp_hid_device_set_report(device, HidBuf, 12, error))
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
	gint pkg_size = PACKAGE_LEN - 12;
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
		if (len - pos > PACKAGE_LEN - 12) {
			transfer_length = PACKAGE_LEN - 12;
			HidBuf[2] = 0x01;
		} else {
			transfer_length = len - pos;
			HidBuf[2] = 0x00;
		}
		HidBuf[3] = pkg_num++;
		HidBuf[4] = transfer_length + 7;
		HidBuf[5] = I2C_WRITE_FLAG;
		HidBuf[6] = (current_addr >> 24) & 0xFF;
		HidBuf[7] = (current_addr >> 16) & 0xFF;
		HidBuf[8] = (current_addr >> 8) & 0xFF;
		HidBuf[9] = current_addr & 0xFF;
		HidBuf[10] = (transfer_length >> 8) & 0xFF;
		HidBuf[11] = transfer_length & 0xFF;
		memcpy(&HidBuf[12], &buf[pos], transfer_length);
		if (!fu_goodixtp_hid_device_set_report(device, HidBuf, transfer_length + 12, error))
			return FALSE;
		pos += transfer_length;
		current_addr += transfer_length;
	}
	return TRUE;
}

static gboolean
send_cmd(FuDevice *device, guint8 cmd, guint8 *data, guint32 dataLen, GError **error)
{
	guint8 HidBuf[PACKAGE_LEN] = {0};

	HidBuf[0] = REPORT_ID;
	HidBuf[1] = cmd;
	HidBuf[2] = 0x00;
	HidBuf[3] = 0x00;
	HidBuf[4] = (guint8)dataLen;
	memcpy(&HidBuf[5], data, dataLen);
	if (!fu_goodixtp_hid_device_set_report(device, HidBuf, dataLen + 5, error)) {
		g_debug("send cmd[%x] failed,", cmd);
		return FALSE;
	}
	return TRUE;
}

static gboolean
brlb_get_version(FuDevice *device, gpointer user_data, GError **error)
{
	struct FuGoodixVersion *ver = (struct FuGoodixVersion *)user_data;
	guint8 tempBuf[14] = {0};
	guint8 vice_ver;
	guint8 inter_ver;

	if (!hid_read(device, 0x1001E, tempBuf, 14, error)) {
		g_prefix_error(error, "Failed read PID/VID,");
		return FALSE;
	}
	memcpy(ver->patch_pid, tempBuf, 8);
	memcpy(ver->patch_vid, &tempBuf[8], 4);
	ver->sensor_id = tempBuf[13];
	vice_ver = tempBuf[10];
	inter_ver = tempBuf[11];

	if (!hid_read(device, 0x10076, tempBuf, 5, error)) {
		g_prefix_error(error, "Failed read config id/version,");
		return FALSE;
	}

	ver->cfg_id = fu_memread_uint32(tempBuf, G_LITTLE_ENDIAN);
	ver->cfg_ver = tempBuf[4];
	ver->ver_num = (vice_ver << 16) | (inter_ver << 8) | ver->cfg_ver;

	return TRUE;
}

static gboolean
brlb_update_prepare(FuDevice *device, GError **error)
{
	guint8 tempBuf[5] = {0};
	guint8 recvBuf[5] = {0};
	gint retry = 3;

	/* step 1. switch mini system */
	tempBuf[0] = 0x01;
	if (!send_cmd(device, 0x10, tempBuf, 1, error)) {
		g_prefix_error(error, "Failed send minisystem cmd,");
		return FALSE;
	}
	while (retry--) {
		fu_device_sleep(device, 200);
		if (!hid_read(device, 0x10010, tempBuf, 1, error)) {
			g_prefix_error(error, "read 0x10010 failed,");
			return FALSE;
		}
		if (tempBuf[0] == 0xDD)
			break;
	}
	if (retry < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "Failed switch minisystem flag=0x%02x",
			    tempBuf[0]);
		return FALSE;
	}
	g_debug("Switch mini system successfully");

	/* step 2. erase flash */
	tempBuf[0] = 0x01;
	if (!send_cmd(device, 0x11, tempBuf, 1, error)) {
		g_prefix_error(error, "Failed send erase flash cmd,");
		return FALSE;
	}

	retry = 10;
	memset(tempBuf, 0x55, 5);
	while (retry--) {
		fu_device_sleep(device, 10);
		if (!hid_write(device, 0x14000, tempBuf, 5, error)) {
			g_prefix_error(error, "Failed write sram,");
			return FALSE;
		}
		if (!hid_read(device, 0x14000, recvBuf, 5, error)) {
			g_prefix_error(error, "Failed read 0x14000,");
			return FALSE;
		}
		if (!memcmp(tempBuf, recvBuf, 5))
			break;
	}
	if (retry < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "Read back failed, buf:%02x %02x %02x %02x %02x",
			    recvBuf[0],
			    recvBuf[1],
			    recvBuf[2],
			    recvBuf[3],
			    recvBuf[4]);
		return FALSE;
	}

	g_debug("Updata prepare OK");
	return TRUE;
}

static gboolean
load_sub_firmware_cb(FuDevice *parent, gpointer user_data, GError **error)
{
	struct FuGoodixTransferData *pkg = (struct FuGoodixTransferData *)user_data;
	guint32 checksum = 0;
	guint8 cmdBuf[10] = {0};
	guint8 flag;
	gint retry = 10;
	gint i;

	/* send fw data to dram */
	if (!hid_write(parent, 0x14000, pkg->buf, pkg->len, error)) {
		g_debug("Write fw data failed");
		return FALSE;
	}

	/* send checksum */
	for (i = 0; i < (gint)pkg->len; i += 2)
		checksum += fu_memread_uint16(&pkg->buf[i], G_LITTLE_ENDIAN);

	cmdBuf[0] = (pkg->len >> 8) & 0xFF;
	cmdBuf[1] = pkg->len & 0xFF;
	cmdBuf[2] = (pkg->addr >> 24) & 0xFF;
	cmdBuf[3] = (pkg->addr >> 16) & 0xFF;
	cmdBuf[4] = (pkg->addr >> 8) & 0xFF;
	cmdBuf[5] = pkg->addr & 0xFF;
	cmdBuf[6] = (checksum >> 24) & 0xFF;
	cmdBuf[7] = (checksum >> 16) & 0xFF;
	cmdBuf[8] = (checksum >> 8) & 0xFF;
	cmdBuf[9] = checksum & 0xFF;
	if (!send_cmd(parent, 0x12, cmdBuf, 10, error)) {
		g_prefix_error(error, "Failed send start update cmd,");
		return FALSE;
	}

	/* wait update finish */
	while (retry--) {
		fu_device_sleep(parent, 20);
		if (!hid_read(parent, 0x10011, &flag, 1, error)) {
			g_prefix_error(error, "Failed to read 0x10011");
			return FALSE;
		}
		if (flag == 0xAA)
			break;
	}
	if (retry < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "Failed get valid ack, flag=0x%02x",
			    flag);
		return FALSE;
	}

	return TRUE;
}

static gboolean
brlb_update_process(FuDevice *device, guint32 flash_addr, guint8 *buf, guint32 len, GError **error)
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
brlb_update_finish(FuDevice *device, GError **error)
{
	guint8 buf[1];

	/* reset IC */
	buf[0] = 1;
	if (!send_cmd(device, 0x13, buf, 1, error)) {
		g_debug("Failed reset IC");
		return FALSE;
	}
	fu_device_sleep(device, 100);
	return TRUE;
}

static gboolean
fu_goodixtp_brlb_device_setup(FuDevice *device, GError **error)
{
	FuGoodixtpHidDevice *self = FU_GOODIXTP_HID_DEVICE(device);
	struct FuGoodixVersion tmp_ver;

	if (!brlb_get_version(device, &tmp_ver, error)) {
		g_prefix_error(error, "brlb read version failed,");
		return FALSE;
	}
	fu_goodixtp_hid_device_set_version(self, &tmp_ver);
	fu_device_set_version_from_uint32(device, tmp_ver.ver_num);
	return TRUE;
}

static FuFirmware *
fu_goodixtp_brlb_device_prepare_firmware(FuDevice *device,
					 GBytes *fw,
					 FwupdInstallFlags flags,
					 GError **error)
{
	FuGoodixtpHidDevice *self = FU_GOODIXTP_HID_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_goodixtp_brlb_firmware_new();
	if (!fu_goodixtp_brlb_firmware_parse(FU_GOODIXTP_FIRMWARE(firmware),
					     fw,
					     fu_goodixtp_hid_device_get_sensor_id(self),
					     error))
		return NULL;
	return g_steal_pointer(&firmware);
}

static gboolean
fu_goodixtp_brlb_device_write_firmware(FuDevice *device,
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

	if (!brlb_update_prepare(device, error))
		return FALSE;
	fu_progress_step_done(progress);
	chunks = fu_chunk_array_new(buf, bufsz, 0x0, 0x0, RAM_BUFFER_SIZE);
	for (gint i = 0; i < (gint)chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		guint32 addr = fu_goodixtp_firmware_get_addr(firmware_goodixtp, i);
		if (!brlb_update_process(device,
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

	if (!brlb_update_finish(device, error))
		return FALSE;
	if (!brlb_get_version(device, &ic_ver, error))
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
