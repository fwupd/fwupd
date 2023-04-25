/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <linux/hidraw.h>
#include <linux/input.h>

#include "fu-goodixtp-common.h"

#define CFG_FLASH_ADDR 0x1E000
#define CFG_START_ADDR 0X60DC
#define CMD_ADDR       0x60CC

#define RAM_BUFFER_SIZE	  4096
#define BL_STATE_ADDR	  0x5095
#define FLASH_RESULT_ADDR 0x5096
#define FLASH_BUFFER_ADDR 0xC000

#define GTX8_FW_INFO_OFFSET 32
#define GTX8_FW_DATA_OFFSET 256

enum updateFlag {
	NO_NEED_UPDATE = 0,
	NEED_UPDATE_FW = 1,
	NEED_UPDATE_CONFIG = 2,
	NEED_UPDATE_CONFIG_WITH_ISP = 0x10,
	NEED_UPDATE_HID_SUBSYSTEM = 0x80,
};

struct fw_subsys_info {
	guint8 type;
	guint size;
	guint flash_addr;
	guint8 *data;
};

struct goodix_update_ctrl_t {
	guint8 *config_data;
	guint32 config_size;
	gboolean has_config;
	guint8 sensor_id;
	gint bin_version;
	guint subsys_num;
	gboolean is_cfg_flashed_with_isp;
	struct fw_subsys_info subsys[30];
};

static struct goodix_update_ctrl_t gtx8_update_ctrl;

static gboolean
read_pkg(FuDevice *device, guint32 addr, guint8 *buf, guint32 len, GError **error)
{
	guint8 HidBuf[PACKAGE_LEN] = {0};
	gboolean ret;

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
	ret = set_report(device, HidBuf, 10, error);
	if (!ret)
		return ret;

	ret = get_report(device, HidBuf, error);
	if (!ret)
		return ret;

	if (HidBuf[3] == 0 && HidBuf[4] == len) {
		memcpy(buf, &HidBuf[5], HidBuf[4]);
		return TRUE;
	}

	g_debug("Failed to read_pkg, HidBuf[3]:%d HidBuf[4]:%d", HidBuf[3], HidBuf[4]);
	return FALSE;
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
	gboolean ret;

	pkg_num = len / pkg_size;
	remain_size = len % pkg_size;
	for (i = 0; i < pkg_num; i++) {
		ret = read_pkg(device, tmp_addr + offset, &buf[offset], pkg_size, error);
		if (!ret)
			return ret;
		offset += pkg_size;
	}

	if (remain_size > 0) {
		ret = read_pkg(device, tmp_addr + offset, &buf[offset], remain_size, error);
		if (!ret)
			return ret;
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
	gboolean ret;

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
		ret = set_report(device, HidBuf, transfer_length + 10, error);
		if (!ret) {
			g_debug("Failed write data to addr=0x%x, len=%d",
				current_addr,
				(gint)transfer_length);
			return ret;
		}
		pos += transfer_length;
		current_addr += transfer_length;
	}
	return TRUE;
}

static gboolean
send_cmd(FuDevice *device, guint8 *buf, gint len, GError **error)
{
	guint8 temp_buf[PACKAGE_LEN] = {0};
	gboolean ret;

	if ((gint)sizeof(temp_buf) < len)
		return FALSE;

	memcpy(temp_buf, buf, len);
	temp_buf[0] = REPORT_ID;
	ret = fu_udev_device_ioctl((FuUdevDevice *)device,
				   HIDIOCSFEATURE(len),
				   temp_buf,
				   NULL,
				   GOODIX_DEVICE_IOCTL_TIMEOUT,
				   error);
	if (!ret) {
		g_debug("failed to set feature");
	}
	return ret;
}

static guint8
checksum_u8(guint8 *data, gint len)
{
	guint8 chksum = 0;
	gint i = 0;

	for (i = 0; i < len; i++)
		chksum += data[i];
	return chksum;
}

static gint
gtx8_get_version(FuDevice *device, GError **error)
{
	guint8 fw_info[72] = {0};
	gint version;
	guint8 pid[9] = {0};
	guint8 vid[8] = {0};
	guint8 vice_ver;
	guint8 inter_ver;
	guint8 cfg_ver;
	guint8 sensor_id;
	guint8 chksum;
	gboolean ret;

	ret = hid_read(device, 0x60DC, &cfg_ver, 1, error);
	if (!ret) {
		g_debug("Failed read cfg version");
		return -1;
	}

	ret = hid_read(device, 0x452C, fw_info, sizeof(fw_info), error);
	if (!ret) {
		g_debug("Failed read firmware version");
		return -1;
	}

	/*check fw version*/
	chksum = checksum_u8(fw_info, sizeof(fw_info));
	if (chksum) {
		g_debug("fw version check sum error:%d", chksum);
		return -2;
	}

	memcpy(pid, &fw_info[9], 4);
	sensor_id = fw_info[21] & 0x0F;
	gtx8_update_ctrl.sensor_id = sensor_id;
	memcpy(vid, &fw_info[17], 4);
	vice_ver = fw_info[19];
	inter_ver = fw_info[20];
	version = (vice_ver << 16) | (inter_ver << 8) | cfg_ver;

	g_debug("PID:%s", pid);
	g_debug("VID:%02x %02x %02x %02x", vid[0], vid[1], vid[2], vid[3]);
	g_debug("sensorID:%d", sensor_id);
	g_debug("configVer:%02x", cfg_ver);
	g_debug("version:%d", version);
	return version;
}

static gboolean
gtx8_parse_firmware(FuDevice *device, guint8 *data, guint32 len, GError **error)
{
	struct fw_subsys_info *subsys = gtx8_update_ctrl.subsys;
	guint16 checksum = 0;
	guint8 fw_pid[9] = {0};
	guint8 fw_vid[4];
	guint8 cfg_ver = 0;
	gint sub_cfg_num;
	guint8 sub_cfg_id;
	guint sub_cfg_info_pos;
	guint cfg_offset;
	guint sub_cfg_len;
	gint i;
	gint firmware_size;
	guint32 sub_fw_info_pos = GTX8_FW_INFO_OFFSET;
	guint32 fw_image_offset = GTX8_FW_DATA_OFFSET;

	if (!data || len < 100)
		return FALSE;

	// parse firmware
	firmware_size = data[0] << 24 | data[1] << 16 | data[2] << 8 | data[3];

	if ((gint)firmware_size + 6 != (gint)len) {
		g_debug("Check file len unequal %d != %d, this bin may contain config",
			(gint)firmware_size + 6,
			(gint)len);
		gtx8_update_ctrl.has_config = TRUE;
	}

	for (i = 6; i < firmware_size + 6; i++)
		checksum += data[i];

	if (checksum != (data[4] << 8 | data[5])) {
		g_debug("Check_sum err  0x%x != 0x%x",
			(guint)checksum,
			(guint)(data[4] << 8 | data[5]));
		return FALSE;
	}

	gtx8_update_ctrl.subsys_num = data[27];
	if (gtx8_update_ctrl.subsys_num == 0) {
		g_debug("subsys_num is 0, exit");
		return FALSE;
	}
	g_debug("subsys_num:%d", (gint)gtx8_update_ctrl.subsys_num);
	for (i = 0; i < (gint)gtx8_update_ctrl.subsys_num; i++) {
		subsys[i].type = data[sub_fw_info_pos];
		subsys[i].size = (data[sub_fw_info_pos + 1] << 24) |
				 (data[sub_fw_info_pos + 2] << 16) |
				 (data[sub_fw_info_pos + 3] << 8) | data[sub_fw_info_pos + 4];
		subsys[i].flash_addr = (data[sub_fw_info_pos + 5] << 8) | data[sub_fw_info_pos + 6];
		subsys[i].flash_addr <<= 8;
		subsys[i].data = &data[fw_image_offset];
		fw_image_offset += subsys[i].size;
		sub_fw_info_pos += 8;
	}

	memcpy(fw_pid, &data[15], 8);
	memcpy(fw_vid, &data[23], 4);
	g_debug("Firmware PID:GT%s", fw_pid);
	g_debug("Firmware VID:%02x %02x %02x %02x", fw_vid[0], fw_vid[1], fw_vid[2], fw_vid[3]);
	g_debug("Firmware size:%d", (gint)firmware_size);

	// parse config
	if (gtx8_update_ctrl.has_config) {
		gint cfg_packlen = (data[firmware_size + 6] << 8) + data[firmware_size + 7];
		if ((gint)(len - firmware_size - 6) != (gint)cfg_packlen + 6) {
			g_debug("config pack len error, %d != %d",
				(gint)(len - firmware_size - 6),
				(gint)(cfg_packlen + 6));
			return FALSE;
		}

		for (i = firmware_size + 12, checksum = 0; i < (gint)len; i++)
			checksum += data[i];
		if (checksum != (data[firmware_size + 10] << 8) + data[firmware_size + 11]) {
			g_debug("config pack checksum error,%d != %d",
				checksum,
				(data[firmware_size + 10] << 8) + data[firmware_size + 11]);
			return FALSE;
		}
		sub_cfg_num = data[firmware_size + 9];
		if (sub_cfg_num == 0) {
			g_debug("sub_cfg_num is 0, exit");
			return FALSE;
		}
		sub_cfg_info_pos = firmware_size + 12;
		cfg_offset = firmware_size + 6 + 64;
		for (i = 0; i < sub_cfg_num; i++) {
			sub_cfg_id = data[sub_cfg_info_pos];
			sub_cfg_len =
			    (data[sub_cfg_info_pos + 1] << 8) | data[sub_cfg_info_pos + 2];
			if (gtx8_update_ctrl.sensor_id == sub_cfg_id) {
				gtx8_update_ctrl.config_data = &data[cfg_offset];
				gtx8_update_ctrl.config_size = sub_cfg_len;
				cfg_ver = gtx8_update_ctrl.config_data[0];
				g_debug("Find a cfg match sensorID:ID=%d, cfg version=%d",
					gtx8_update_ctrl.sensor_id,
					cfg_ver);
				break;
			}
			cfg_offset += sub_cfg_len;
			sub_cfg_info_pos += 3;
		}
		if (!gtx8_update_ctrl.config_data || gtx8_update_ctrl.config_size == 0) {
			g_debug("can't find valid sub_cfg, exit");
			return FALSE;
		}
		if (data[firmware_size + 8] & NEED_UPDATE_CONFIG_WITH_ISP)
			gtx8_update_ctrl.is_cfg_flashed_with_isp = TRUE;
		else
			gtx8_update_ctrl.is_cfg_flashed_with_isp = FALSE;
		g_debug("sub_cfg_ver:0x%02x", cfg_ver);
		g_debug("sub_cfg_size:%d", (gint)gtx8_update_ctrl.config_size);
	}

	gtx8_update_ctrl.bin_version = (fw_vid[2] << 16) | (fw_vid[3] << 8) | cfg_ver;
	return TRUE;
}

static gboolean
disable_report(FuDevice *device, GError **error)
{
	guint8 cmdDisable[] = {0x33, 0x00, 0xCD};
	guint8 cmdConfirm[] = {0x35, 0x00, 0xCB};
	guint8 buf[3] = {0};
	gboolean ret;

	for (gint i = 0; i < 3; i++) {
		ret = hid_write(device, CMD_ADDR, cmdDisable, sizeof(cmdDisable), error);
		if (!ret) {
			g_debug("send close report cmd failed");
			return ret;
		}
		fu_device_sleep(device, 10);
	}

	ret = hid_write(device, CMD_ADDR, cmdConfirm, sizeof(cmdConfirm), error);
	if (!ret) {
		g_debug("send confirm cmd failed");
		return ret;
	}
	fu_device_sleep(device, 30);
	ret = hid_read(device, CMD_ADDR, buf, sizeof(buf), error);
	if (!ret) {
		g_debug("read confirm flag failed");
		return ret;
	}

	if (buf[1] != 1) {
		g_debug("close report failed, flag[0x%02X]", buf[1]);
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
	gboolean ret;
	gint retry;

	/* close report */
	ret = disable_report(device, error);
	if (!ret)
		return ret;

	ret = send_cmd(device, cmd_switch_to_patch, sizeof(cmd_switch_to_patch), error);
	if (!ret) {
		g_debug("Failed switch to patch");
		return ret;
	}

	fu_device_sleep(device, 100);
	retry = 5;
	do {
		ret = hid_read(device, BL_STATE_ADDR, temp_buf, 1, error);
		g_debug("BL_STATE_ADDR:0x%x", (guint)BL_STATE_ADDR);
		if (!ret) {
			g_debug("Failed read 0x%x", (guint)BL_STATE_ADDR);
			return FALSE;
		}
		if (temp_buf[0] == 0xDD)
			break;
		g_debug("0x%x value is 0x%x != 0xDD, retry", (guint)BL_STATE_ADDR, temp_buf[0]);
		fu_device_sleep(device, 30);
	} while (--retry);

	if (!retry) {
		g_debug("Reg 0x%x != 0xDD", (guint)BL_STATE_ADDR);
		return FALSE;
	}

	ret = disable_report(device, error);
	if (!ret)
		return ret;

	return TRUE;
}

static void
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
	if (retry < 0)
		g_debug("Failed write restart command");
	fu_device_sleep(device, 100);
	if (!send_cmd(device, cmd_switch_ptp_mode, sizeof(cmd_switch_ptp_mode), error))
		g_debug("Failed switch to ptp mode");
}

static gboolean
load_sub_firmware(FuDevice *device, guint32 flash_addr, guint8 *fw_data, guint len, GError **error)
{
	gboolean ret;
	gint retry;
	guint i;
	guint unitlen = 0;
	guint8 temp_buf[PACKAGE_LEN] = {0};
	guint load_data_len = 0;
	guint8 buf_load_flash[15] = {0x0e, 0x12, 0x00, 0x00, 0x06};
	guint16 check_sum = 0;
	guint8 dummy = 0;
	gint retry_load = 0;

	while (retry_load < 3 && load_data_len != len) {
		unitlen = (len - load_data_len > RAM_BUFFER_SIZE) ? RAM_BUFFER_SIZE
								  : (len - load_data_len);
		ret = hid_write(device, FLASH_BUFFER_ADDR, &fw_data[load_data_len], unitlen, error);
		if (!ret) {
			g_debug("Failed load fw, len %d : addr 0x%x", (gint)unitlen, flash_addr);
			return ret;
		}

		/* inform IC to load 4K data to flash */
		for (check_sum = 0, i = 0; i < unitlen; i += 2) {
			check_sum +=
			    (fw_data[load_data_len + i] << 8) + fw_data[load_data_len + i + 1];
		}
		buf_load_flash[5] = (unitlen >> 8) & 0xFF;
		buf_load_flash[6] = unitlen & 0xFF;
		buf_load_flash[7] = (flash_addr >> 16) & 0xFF;
		buf_load_flash[8] = (flash_addr >> 8) & 0xFF;
		buf_load_flash[9] = (check_sum >> 8) & 0xFF;
		buf_load_flash[10] = check_sum & 0xFF;

		ret = send_cmd(device, buf_load_flash, 11, error);
		if (!ret) {
			g_debug("Failed write load flash command");
			return ret;
		}

		fu_device_sleep(device, 80);
		retry = 10;
		do {
			memset(temp_buf, 0, sizeof(temp_buf));
			ret = hid_read(device, FLASH_RESULT_ADDR, temp_buf, 1, error);
			if (ret && temp_buf[0] == 0xAA)
				break;
			fu_device_sleep(device, 20);
		} while (--retry);
		if (!retry) {
			g_debug("Read back 0x%x(0x%x) != 0xAA",
				(guint)FLASH_RESULT_ADDR,
				temp_buf[0]);
			g_debug("Reload(%d) subFW:addr:0x%x", retry_load, flash_addr);
			/* firmware chechsum err */
			retry_load++;
			ret = FALSE;
		} else {
			load_data_len += unitlen;
			flash_addr += unitlen;
			retry_load = 0;
			hid_write(device, FLASH_RESULT_ADDR, &dummy, 1, error);
			ret = TRUE;
		}
	}

	return ret;
}

static gboolean
cfg_update(FuDevice *device, GError **error)
{
	guint8 temp_buf[PACKAGE_LEN];
	gboolean ret;
	gint retry;

	// wait until ic is free
	retry = 10;
	do {
		ret = hid_read(device, CMD_ADDR, temp_buf, 1, error);
		if (ret && temp_buf[0] == 0xFF)
			break;
		fu_device_sleep(device, 10);
	} while (--retry);
	if (!retry) {
		g_debug("Reg 0x%x != 0xFF", (guint)CMD_ADDR);
		return FALSE;
	}

	// tell ic i want to send cfg
	temp_buf[0] = 0x80;
	temp_buf[1] = 0;
	temp_buf[2] = 0x80;
	ret = hid_write(device, CMD_ADDR, temp_buf, 3, error);
	if (!ret) {
		g_debug("Failed write send cfg cmd");
		return ret;
	}

	// wait ic to confirm
	fu_device_sleep(device, 100);
	retry = 5;
	do {
		ret = hid_read(device, CMD_ADDR, temp_buf, 1, error);
		if (ret && temp_buf[0] == 0x82)
			break;
		fu_device_sleep(device, 30);
	} while (--retry);
	if (!retry) {
		g_debug("Reg 0x%x != 0x82", (guint)CMD_ADDR);
		return FALSE;
	}

	/* Start load config */
	ret = hid_write(device,
			CFG_START_ADDR,
			gtx8_update_ctrl.config_data,
			gtx8_update_ctrl.config_size,
			error);
	if (!ret) {
		g_debug("Failed write cfg to xdata");
		return ret;
	}
	fu_device_sleep(device, 100);
	// tell ic cfg is ready in xdata
	temp_buf[0] = 0x83;
	ret = hid_write(device, CMD_ADDR, temp_buf, 1, error);
	if (!ret) {
		g_debug("Failed write send cfg finish cmd");
		return ret;
	}

	// check if ic is ok with the cfg
	fu_device_sleep(device, 80);
	retry = 5;
	do {
		ret = hid_read(device, CMD_ADDR, temp_buf, 1, error);
		if (ret && temp_buf[0] == 0xFF)
			break;
		fu_device_sleep(device, 30);
	} while (--retry);
	if (!retry) {
		g_debug("Reg 0x%x != 0xFF", (guint)CMD_ADDR);
		return FALSE;
	}

	g_debug("finish config download");
	return TRUE;
}

static gboolean
gtx8_update(FuDevice *device, guint32 firmware_flag, GError **error)
{
	guint8 cmd_start_update[] = {0x00, 0x11, 0x00, 0x00, 0x01, 0x01};
	struct fw_subsys_info *subsys = gtx8_update_ctrl.subsys;
	gboolean ret;
	guint i;

	/* Start update */
	ret = send_cmd(device, cmd_start_update, sizeof(cmd_start_update), error);
	if (!ret) {
		g_debug("Failed start update");
		return ret;
	}
	fu_device_sleep(device, 100);

	/* load normal firmware package */
	for (i = 0; i < gtx8_update_ctrl.subsys_num; i++) {
		if (!(firmware_flag & (0x01 << subsys[i].type))) {
			g_debug("Sub firmware type does not math:type=%d", subsys[i].type);
			continue;
		}
		g_debug("load sub firmware addr:0x%x, len:%d",
			subsys[i].flash_addr,
			(gint)subsys[i].size);
		ret = load_sub_firmware(device,
					subsys[i].flash_addr,
					subsys[i].data,
					subsys[i].size,
					error);
		if (!ret) {
			g_debug("Failed load sub firmware");
			return ret;
		}
	}
	g_debug("finish fw download");

	if (gtx8_update_ctrl.has_config) {
		if (gtx8_update_ctrl.is_cfg_flashed_with_isp) {
			g_debug("load cfg addr:0x%x, len:%d",
				(guint)CFG_FLASH_ADDR,
				(gint)gtx8_update_ctrl.config_size);
			ret = load_sub_firmware(device,
						CFG_FLASH_ADDR,
						gtx8_update_ctrl.config_data,
						gtx8_update_ctrl.config_size,
						error);
			if (!ret) {
				g_debug("Failed flash cofig with ISP");
				return ret;
			}
		} else {
			soft_reset_ic(device, error);
			ret = cfg_update(device, error);
			if (!ret) {
				g_debug("Failed update config");
				return ret;
			}
		}
	}

	return TRUE;
}

static gboolean
gtx8_update_finish(FuDevice *device, GError **error)
{
	gint version;

	/* reset IC */
	soft_reset_ic(device, error);

	version = gtx8_get_version(device, error);
	if (version < 0)
		return FALSE;
	if (!gtx8_update_ctrl.has_config)
		version &= ~0x000000FF;

	if (version == gtx8_update_ctrl.bin_version) {
		g_debug("update successfully, current ver:%x", (guint)version);
		return TRUE;
	} else {
		g_debug("update failed chip_ver:%x != bin_ver:%x",
			(guint)version,
			(guint)gtx8_update_ctrl.bin_version);
		return FALSE;
	}
}

struct goodix_hw_ops_t gtx8_hw_ops = {
    .get_version = gtx8_get_version,
    .parse_firmware = gtx8_parse_firmware,
    .update_prepare = gtx8_update_prepare,
    .update = gtx8_update,
    .update_finish = gtx8_update_finish,
};
