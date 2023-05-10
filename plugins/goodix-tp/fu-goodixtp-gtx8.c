/*
 * Copyright (C) 2023 Goodix.inc <xulinkun@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-goodixtp-common.h"

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
	if (!set_report(device, HidBuf, 10, error))
		return FALSE;

	if (!get_report(device, HidBuf, error))
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
		if (!set_report(device, HidBuf, transfer_length + 10, error)) {
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
	if (!set_report(device, temp_buf, len, error)) {
		g_debug("failed to set feature");
		return FALSE;
	}

	return TRUE;
}

static gboolean
gtx8_get_version(FuDevice *device, gpointer user_data, GError **error)
{
	struct goodix_version_t *ver = (struct goodix_version_t *)user_data;
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
	ver->version = (vice_ver << 16) | (inter_ver << 8) | cfg_ver;

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
	struct transfer_data_t *pkg = (struct transfer_data_t *)user_data;

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
	struct transfer_data_t pkg;

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

struct goodix_hw_ops_t gtx8_hw_ops = {
    .get_version = gtx8_get_version,
    .update_prepare = gtx8_update_prepare,
    .update_process = gtx8_update_process,
    .update_finish = gtx8_update_finish,
};
