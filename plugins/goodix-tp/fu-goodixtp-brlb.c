/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-goodixtp-common.h"

#define FW_HEADER_SIZE	      512
#define FW_SUBSYS_INFO_OFFSET 42
#define FW_SUBSYS_INFO_SIZE   10

struct fw_subsys_info {
	guint8 type;
	guint size;
	guint flash_addr;
	guint8 *data;
};

#pragma pack(1)
struct firmware_summary {
	guint size;
	guint checksum;
	guint8 hw_pid[6];
	guint8 hw_vid[3];
	guint8 fw_pid[8];
	guint8 fw_vid[4];
	guint8 subsys_num;
	guint8 chip_type;
	guint8 protocol_ver;
	guint8 bus_type;
	guint8 flash_protect;
	guint8 reserved[8];
	struct fw_subsys_info subsys[47];
};
#pragma pack()

struct goodix_update_ctrl_t {
	guint8 *config_data;
	guint32 config_size;
	gboolean has_config;
	gint bin_version;
	struct firmware_summary fw_summary;
};

static struct goodix_update_ctrl_t brlb_update_ctrl;

static gboolean
read_pkg(FuDevice *device, guint32 addr, guint8 *buf, guint32 len, GError **error)
{
	guint8 HidBuf[PACKAGE_LEN] = {0};
	gboolean ret;

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
	ret = set_report(device, HidBuf, 12, error);
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
	gint pkg_size = PACKAGE_LEN - 12;
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
		ret = set_report(device, HidBuf, transfer_length + 12, error);
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
send_cmd(FuDevice *device, guint8 cmd, guint8 *data, guint32 dataLen, GError **error)
{
	guint8 HidBuf[PACKAGE_LEN] = {0};
	gboolean ret;

	HidBuf[0] = REPORT_ID;
	HidBuf[1] = cmd;
	HidBuf[2] = 0x00;
	HidBuf[3] = 0x00;
	HidBuf[4] = (guint8)dataLen;
	memcpy(&HidBuf[5], data, dataLen);
	ret = set_report(device, HidBuf, dataLen + 5, error);
	if (!ret) {
		g_debug("Failed send cmd 0x%02x", cmd);
		return ret;
	}
	return TRUE;
}

static gint
brlb_get_version(FuDevice *device, GError **error)
{
	guint8 tempBuf[14] = {0};
	gint version;
	guint8 pid[9] = {0};
	guint8 vid[8] = {0};
	guint8 vice_ver;
	guint8 inter_ver;
	guint8 cfg_ver;
	guint32 cfg_id;
	guint8 sensor_id;
	gboolean ret;

	ret = hid_read(device, 0x1001E, tempBuf, 14, error);
	if (!ret) {
		g_debug("Failed read PID/VID");
		return -1;
	}
	memcpy(pid, tempBuf, 8);
	memcpy(vid, &tempBuf[8], 4);
	sensor_id = tempBuf[13];
	vice_ver = tempBuf[10];
	inter_ver = tempBuf[11];

	ret = hid_read(device, 0x10076, tempBuf, 5, error);
	if (!ret) {
		g_debug("Failed read config id/version");
		return -1;
	}

	cfg_id = *((guint32 *)tempBuf);
	cfg_ver = tempBuf[4];
	version = (vice_ver << 16) | (inter_ver << 8) | cfg_ver;

	g_debug("PID:%s", pid);
	g_debug("VID:%02x %02x %02x %02x", vid[0], vid[1], vid[2], vid[3]);
	g_debug("sensorID:%d", sensor_id);
	g_debug("configID:%08x", cfg_id);
	g_debug("configVer:%02x", cfg_ver);
	g_debug("version:%d", version);
	return version;
}

static gboolean
brlb_parse_firmware(FuDevice *device, guint8 *data, guint32 len, GError **error)
{
	struct firmware_summary *fw_summary = &brlb_update_ctrl.fw_summary;
	guint32 fw_offset;
	guint32 info_offset;
	guint32 checksum = 0;
	guint8 tmp_buf[9] = {0};
	guint8 cfg_ver = 0;
	guint32 firmware_size;
	guint32 i;

	if (!data || len < 100)
		return FALSE;

	firmware_size = ((data[3] << 24) | (data[2] << 16) | (data[1] << 8) | data[0]) + 8;

	if (firmware_size < len) {
		g_debug("Check firmware size:%d < file size:%d", (gint)firmware_size, (gint)len);
		g_debug("This bin file may contain config");
		brlb_update_ctrl.config_size = len - firmware_size - 64;
		brlb_update_ctrl.config_data = data + firmware_size + 64,
		g_debug("config size:%d", (gint)brlb_update_ctrl.config_size);
		brlb_update_ctrl.has_config = TRUE;
	}

	memcpy((guint8 *)fw_summary, data, sizeof(*fw_summary));
	if (firmware_size != (fw_summary->size + 8)) {
		g_debug("Bad firmware, size not match, %d != %d",
			(gint)firmware_size,
			(gint)fw_summary->size);
		return FALSE;
	}

	for (i = 8; i < firmware_size; i += 2)
		checksum += data[i] | (data[i + 1] << 8);

	if (checksum != fw_summary->checksum) {
		g_debug("Bad firmware, checksum error");
		return FALSE;
	}

	fw_offset = FW_HEADER_SIZE;
	for (i = 0; i < fw_summary->subsys_num; i++) {
		info_offset = FW_SUBSYS_INFO_OFFSET + i * FW_SUBSYS_INFO_SIZE;
		fw_summary->subsys[i].type = data[info_offset];
		fw_summary->subsys[i].size = *(guint32 *)&data[info_offset + 1];
		fw_summary->subsys[i].flash_addr = *(guint32 *)&data[info_offset + 5];
		if ((gint)fw_offset > (gint)firmware_size) {
			g_debug("Sybsys offset exceed Firmware size");
			return FALSE;
		}
		fw_summary->subsys[i].data = data + fw_offset;
		fw_offset += fw_summary->subsys[i].size;
	}

	memcpy(tmp_buf, fw_summary->fw_pid, 8);
	g_debug("Firmware package protocol: V%u", fw_summary->protocol_ver);
	g_debug("Firmware PID:GT%s", tmp_buf);
	g_debug("Firmware VID:%02x %02x %02x %02x",
		fw_summary->fw_vid[0],
		fw_summary->fw_vid[1],
		fw_summary->fw_vid[2],
		fw_summary->fw_vid[3]);
	g_debug("Firmware chip type:%02X", fw_summary->chip_type);
	g_debug("Firmware size:%u", fw_summary->size);
	g_debug("Firmware subsystem num:%u", fw_summary->subsys_num);

	if (brlb_update_ctrl.has_config) {
		cfg_ver = data[firmware_size + 64 + 34];
		g_debug("cfg_ver:%02x", cfg_ver);
	}

	brlb_update_ctrl.bin_version =
	    (fw_summary->fw_vid[2] << 16) | (fw_summary->fw_vid[3] << 8) | cfg_ver;

	return TRUE;
}

static gboolean
brlb_update_prepare(FuDevice *device, GError **error)
{
	guint8 tempBuf[5] = {0};
	guint8 recvBuf[5] = {0};
	gint retry = 3;
	gboolean ret;

	/* step 1. switch mini system */
	tempBuf[0] = 0x01;
	ret = send_cmd(device, 0x10, tempBuf, 1, error);
	if (!ret) {
		g_debug("Failed send minisystem cmd");
		return ret;
	}
	while (retry--) {
		fu_device_sleep(device, 200);
		ret = hid_read(device, 0x10010, tempBuf, 1, error);
		if (ret && tempBuf[0] == 0xDD)
			break;
	}
	if (retry < 0) {
		g_debug("Failed switch minisystem flag=0x%02x", tempBuf[0]);
		return FALSE;
	}
	g_debug("Switch mini system successfully");

	/* step 2. erase flash */
	tempBuf[0] = 0x01;
	ret = send_cmd(device, 0x11, tempBuf, 1, error);
	if (!ret) {
		g_debug("Failed send erase flash cmd");
		return ret;
	}

	retry = 10;
	memset(tempBuf, 0x55, 5);
	while (retry--) {
		fu_device_sleep(device, 10);
		ret = hid_write(device, 0x14000, tempBuf, 5, error);
		if (!ret) {
			g_debug("Failed write sram");
			return ret;
		}
		ret = hid_read(device, 0x14000, recvBuf, 5, error);
		if (!memcmp(tempBuf, recvBuf, 5))
			break;
	}
	if (retry < 0) {
		g_debug("Read back failed, buf:%02x %02x %02x %02x %02x\n",
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
flash_sub_system(FuDevice *device, struct fw_subsys_info *subsys, GError **error)
{
	guint32 data_size = 0;
	guint32 offset = 0;
	guint32 temp_addr = subsys->flash_addr;
	guint32 total_size = subsys->size;
	guint32 checksum;
	guint8 cmdBuf[10] = {0};
	guint8 flag;
	gint resend_rty = 3;
	gint retry;
	guint32 i;
	gboolean ret;

	while (total_size > 0) {
		data_size = total_size > 4096 ? 4096 : total_size;
	resend:
		/* send fw data to dram */
		ret = hid_write(device, 0x14000, &subsys->data[offset], data_size, error);
		if (!ret) {
			g_debug("Write fw data failed");
			return ret;
		}

		/* send checksum */
		for (i = 0, checksum = 0; i < data_size; i += 2) {
			checksum += subsys->data[offset + i] + (subsys->data[offset + i + 1] << 8);
		}

		cmdBuf[0] = (data_size >> 8) & 0xFF;
		cmdBuf[1] = data_size & 0xFF;
		cmdBuf[2] = (temp_addr >> 24) & 0xFF;
		cmdBuf[3] = (temp_addr >> 16) & 0xFF;
		cmdBuf[4] = (temp_addr >> 8) & 0xFF;
		cmdBuf[5] = temp_addr & 0xFF;
		cmdBuf[6] = (checksum >> 24) & 0xFF;
		cmdBuf[7] = (checksum >> 16) & 0xFF;
		cmdBuf[8] = (checksum >> 8) & 0xFF;
		cmdBuf[9] = checksum & 0xFF;
		ret = send_cmd(device, 0x12, cmdBuf, 10, error);
		if (!ret) {
			g_debug("Failed send start update cmd");
			return ret;
		}

		/* wait update finish */
		retry = 10;
		while (retry--) {
			fu_device_sleep(device, 20);
			ret = hid_read(device, 0x10011, &flag, 1, error);
			if (ret && flag == 0xAA)
				break;
			else if (ret && flag == 0xBB) {
				if (resend_rty-- > 0) {
					g_debug("Flash data checksum error, retry:%d",
						3 - resend_rty);
					goto resend;
				}
			}
		}
		if (retry < 0) {
			g_debug("Failed get valid ack, flag=0x%02x", flag);
			return FALSE;
		}

		g_debug("Flash package ok, addr:0x%06x", temp_addr);

		offset += data_size;
		temp_addr += data_size;
		total_size -= data_size;
	}

	return TRUE;
}

static gboolean
brlb_update(FuDevice *device, guint32 firmware_flag, GError **error)
{
	struct firmware_summary *fw_info = &brlb_update_ctrl.fw_summary;
	struct fw_subsys_info *fw_x;
	struct fw_subsys_info subsys_cfg;
	gint i;
	gboolean ret;

	/* flash config */
	if (brlb_update_ctrl.has_config) {
		subsys_cfg.data = brlb_update_ctrl.config_data;
		subsys_cfg.size = CFG_MAX_SIZE;
		subsys_cfg.flash_addr = 0x40000;
		subsys_cfg.type = 4;
		ret = flash_sub_system(device, &subsys_cfg, error);
		if (!ret) {
			g_debug("failed flash config with ISP");
			return ret;
		}
		g_debug("success flash config with ISP");
		fu_device_sleep(device, 20);
	}

	for (i = 1; i < fw_info->subsys_num; i++) {
		fw_x = &fw_info->subsys[i];
		if (fw_x->type == (guint8)firmware_flag) {
			g_debug("skip type[%02X] subsystem[%d]", fw_x->type, i);
			continue;
		}
		ret = flash_sub_system(device, fw_x, error);
		if (!ret) {
			g_debug("-------- Failed flash subsystem %d --------", i);
			return ret;
		}
		g_debug("-------- Success flash subsystem %d --------", i);
	}

	return TRUE;
}

static gboolean
brlb_update_finish(FuDevice *device, GError **error)
{
	guint8 buf[1];
	gboolean ret;
	gint version;

	/* reset IC */
	buf[0] = 1;
	ret = send_cmd(device, 0x13, buf, 1, error);
	if (!ret) {
		g_debug("Failed reset IC");
		return ret;
	}
	fu_device_sleep(device, 100);

	/* compare version */
	version = brlb_get_version(device, error);
	if (version < 0) {
		return FALSE;
	}
	if (!brlb_update_ctrl.has_config)
		version &= ~0x000000FF;

	if (version == brlb_update_ctrl.bin_version) {
		g_debug("update successfully, current ver:%x", (guint)version);
		return TRUE;
	} else {
		g_debug("update failed chip_ver:%x != bin_ver:%x",
			(guint)version,
			(guint)brlb_update_ctrl.bin_version);
		return FALSE;
	}
}

struct goodix_hw_ops_t brlb_hw_ops = {
    .get_version = brlb_get_version,
    .parse_firmware = brlb_parse_firmware,
    .update_prepare = brlb_update_prepare,
    .update = brlb_update,
    .update_finish = brlb_update_finish,
};
