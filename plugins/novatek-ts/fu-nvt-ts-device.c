/*
 * Copyright 2026 Novatekmsp <novatekmsp@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "config.h"

#include "fu-nvt-ts-device.h"
#include "fu-nvt-ts-regs-struct.h"

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "FuPluginNvtTs"

#define NVT_DEBUG_DRY_RUN 0

static const FuNvtTsMemMap nt36536_cascade_memory_map = {
    .read_flash_checksum_addr = FU_NVT_TS_MEM_MAP_REG_READ_FLASH_CHECKSUM_ADDR,
    .rw_flash_data_addr = FU_NVT_TS_MEM_MAP_REG_RW_FLASH_DATA_ADDR,
    .event_buf_cmd_addr = FU_NVT_TS_MEM_MAP_REG_EVENT_BUF_CMD_ADDR,
    .event_buf_hs_sub_cmd_addr = FU_NVT_TS_MEM_MAP_REG_EVENT_BUF_HS_SUB_CMD_ADDR,
    .event_buf_reset_state_addr = FU_NVT_TS_MEM_MAP_REG_EVENT_BUF_RESET_STATE_ADDR,
    .event_map_fwinfo_addr = FU_NVT_TS_MEM_MAP_REG_EVENT_MAP_FWINFO_ADDR,
    .chip_ver_trim_addr = FU_NVT_TS_MEM_MAP_REG_CHIP_VER_TRIM_ADDR,
    .enb_casc_addr = FU_NVT_TS_MEM_MAP_REG_ENB_CASC_ADDR,
    .swrst_sif_addr = FU_NVT_TS_MEM_MAP_REG_SWRST_SIF_ADDR,
    .hid_i2c_eng_addr = FU_NVT_TS_MEM_MAP_REG_HID_I2C_ENG_ADDR,
    .bld_spe_pups_addr = FU_NVT_TS_MEM_MAP_REG_BLD_SPE_PUPS_ADDR,
    .gcm_code_addr = FU_NVT_TS_MEM_MAP_REG_GCM_CODE_ADDR,
    .flash_cmd_addr = FU_NVT_TS_MEM_MAP_REG_FLASH_CMD_ADDR,
    .flash_cmd_issue_addr = FU_NVT_TS_MEM_MAP_REG_FLASH_CMD_ISSUE_ADDR,
    .flash_cksum_status_addr = FU_NVT_TS_MEM_MAP_REG_FLASH_CKSUM_STATUS_ADDR,
    .gcm_flag_addr = FU_NVT_TS_MEM_MAP_REG_GCM_FLAG_ADDR,
    .q_wr_cmd_addr = FU_NVT_TS_MEM_MAP_REG_Q_WR_CMD_ADDR,
};

static const FuNvtTsFlashMap nt36536_flash_map = {
    .flash_normal_fw_start_addr = FU_NVT_TS_FLASH_MAP_CONST_FLASH_NORMAL_FW_START_ADDR,
    .flash_pid_addr = FU_NVT_TS_FLASH_MAP_CONST_FLASH_PID_ADDR,
    .flash_fw_size = FU_NVT_TS_FLASH_MAP_CONST_FLASH_FW_SIZE,
};

struct _FuNvtTsDevice {
	FuHidrawDevice parent_instance;
	FuNvtTsData ts;
	FuNvtTsFwBin fwb;
	gboolean if_flash_unknown_skip_status_register_control;
};

static void
fu_nvt_ts_device_fw_bin_clear(FuNvtTsDevice *self)
{
	FuNvtTsFwBin *fwb = &self->fwb;

	g_clear_pointer(&fwb->bin_data, g_free);
	fwb->bin_size = 0;
}

static int32_t
fu_nvt_ts_device_hid_read_dev(FuNvtTsDevice *self,
			      uint32_t addr,
			      uint8_t *data,
			      uint16_t len,
			      GError **error)
{
	int32_t ret;
	uint8_t buf_set[12];
	g_autofree uint8_t *buf_get = NULL;
	FuNvtTsData *ts = &self->ts;

	if (len == 0) {
		g_warning("len must be > 0");
		return -EINVAL;
	}

	g_debug("read addr=0x%08x len=%u", addr, len);

	/* set feature */
	memset(buf_set, 0, sizeof(buf_set));

	buf_set[0] = NVT_TS_REPORT_ID;
	buf_set[1] = 0x0B;
	buf_set[2] = 0x00;

	buf_set[3] = (uint8_t)((ts->mmap->hid_i2c_eng_addr >> 0) & 0xFF);
	buf_set[4] = (uint8_t)((ts->mmap->hid_i2c_eng_addr >> 8) & 0xFF);
	buf_set[5] = (uint8_t)((ts->mmap->hid_i2c_eng_addr >> 16) & 0xFF);

	buf_set[6] = (uint8_t)((addr >> 0) & 0xFF);
	buf_set[7] = (uint8_t)((addr >> 8) & 0xFF);
	buf_set[8] = (uint8_t)((addr >> 16) & 0xFF);
	buf_set[9] = 0x00;

	buf_set[10] = (uint8_t)(((len + 3) >> 0) & 0xFF);
	buf_set[11] = (uint8_t)(((len + 3) >> 8) & 0xFF);

	ret = fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					   buf_set,
					   sizeof(buf_set),
					   FU_IOCTL_FLAG_RETRY,
					   error);
	if (!ret) {
		g_warning("set feature failed");
		return -EIO;
	}

	/* get feature */
	buf_get = g_new0(uint8_t, len + 1);
	buf_get[0] = NVT_TS_REPORT_ID;

	ret = fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(self),
					   buf_get,
					   len + 1,
					   FU_IOCTL_FLAG_RETRY,
					   error);
	if (!ret) {
		g_warning("get feature failed");
		return -EIO;
	}

	if (!fu_memcpy_safe(data, len, 0, buf_get, len + 1, 1, len, error)) {
		g_warning("copying feature data failed");
		return -EIO;
	}

	return 0;
}

static int32_t
fu_nvt_ts_device_hid_write_dev(FuNvtTsDevice *self,
			       uint32_t addr,
			       uint8_t *data,
			       uint16_t len,
			       GError **error)
{
	int32_t ret;
	uint16_t write_len, report_len;
	g_autofree uint8_t *buf_set = NULL;

	if (len == 0) {
		g_warning("len must be > 0");
		return -EINVAL;
	}

	g_debug("write addr=0x%08x len=%u, data:", addr, len);
	fu_dump_raw(G_LOG_DOMAIN, "write-data", data, len);

	write_len = len + 5;

	report_len = write_len + 1;

	buf_set = g_new0(uint8_t, report_len);

	buf_set[0] = NVT_TS_REPORT_ID; /* report ID */
	buf_set[1] = (uint8_t)(write_len & 0xFF);
	buf_set[2] = (uint8_t)((write_len >> 8) & 0xFF);

	buf_set[3] = (uint8_t)((addr >> 0) & 0xFF);
	buf_set[4] = (uint8_t)((addr >> 8) & 0xFF);
	buf_set[5] = (uint8_t)((addr >> 16) & 0xFF);

	if (!fu_memcpy_safe(buf_set, report_len, 6, data, len, 0, len, error)) {
		g_warning("copying write buffer failed");
		return -EINVAL;
	}

	ret = fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					   buf_set,
					   report_len,
					   FU_IOCTL_FLAG_RETRY,
					   error);
	if (!ret) {
		g_warning("set feature failed");
		return -EIO;
	}

	return 0;
}

static int32_t
fu_nvt_ts_device_hid_read(FuNvtTsDevice *self, uint32_t addr, uint8_t *data, uint16_t len)
{
	return fu_nvt_ts_device_hid_read_dev(self, addr, data, len, NULL);
}

static int32_t
fu_nvt_ts_device_hid_write(FuNvtTsDevice *self, uint32_t addr, uint8_t *data, uint16_t len)
{
	return fu_nvt_ts_device_hid_write_dev(self, addr, data, len, NULL);
}

static int32_t
fu_nvt_ts_device_write_reg_bits(FuNvtTsDevice *self, FuNvtTsReg reg, uint8_t val)
{
	uint8_t mask = 0, shift = 0, temp = 0;
	uint8_t buf[8] = {0};
	uint32_t addr = 0;
	int32_t ret = 0;

	addr = reg.addr;
	mask = reg.mask;
	temp = reg.mask;
	shift = 0;
	while (1) {
		if ((temp >> shift) & 0x01)
			break;
		if (shift == 8) {
			g_warning("mask all bits zero!\n");
			break;
		}
		shift++;
	}
	ret = fu_nvt_ts_device_hid_read(self, addr, buf, 1);
	if (ret < 0) {
		g_warning("fu_nvt_ts_device_hid_read failed!(%d)\n", ret);
		return ret;
	}
	temp = buf[0] & (~mask);
	temp |= ((val << shift) & mask);
	buf[0] = temp;
	ret = fu_nvt_ts_device_hid_write(self, addr, buf, 1);
	if (ret < 0) {
		g_warning("fu_nvt_ts_device_hid_write failed!(%d)\n", ret);
		return ret;
	}

	return ret;
}

static int32_t
fu_nvt_ts_device_find_fw_bin_end_flag(const uint8_t *base,
				      uint32_t size,
				      uint32_t *flag_offset,
				      uint32_t *delta_out)
{
	const uint16_t step = 0x1000;
	const char *expect = HID_FW_BIN_END_NAME_FULL;
	uint32_t delta = 0;
	char end_char[BIN_END_FLAG_LEN_MAX] = {0};

	if (base == NULL || size < BIN_END_FLAG_LEN_MAX)
		return -EFAULT;

	for (delta = 0; size >= BIN_END_FLAG_LEN_MAX + delta; delta += step) {
		uint32_t offset = size - delta - BIN_END_FLAG_LEN_MAX;

		{
			g_autoptr(GError) error_local = NULL;
			if (!fu_memcpy_safe((guint8 *)end_char,
					    sizeof(end_char),
					    0,
					    base,
					    size,
					    offset,
					    BIN_END_FLAG_LEN_MAX,
					    &error_local)) {
				g_warning("copying end flag failed: %s",
					  error_local != NULL ? error_local->message : "unknown");
				return -EFAULT;
			}
		}
		/* we now check "NVT" only */
		if (memcmp(end_char + 1, expect, BIN_END_FLAG_LEN_FULL) == 0) {
			if (flag_offset != NULL)
				*flag_offset = offset;
			if (delta_out != NULL)
				*delta_out = delta;
			return 0;
		}

		if (size < BIN_END_FLAG_LEN_MAX + delta + step)
			break;
	}

	return -EFAULT;
}

static int32_t
fu_nvt_ts_device_gcm_xfer(FuNvtTsDevice *self, FuNvtTsGcmXfer *xfer)
{
	g_autofree uint8_t *buf = NULL;
	uint32_t flash_cmd_addr = 0, flash_cmd_issue_addr = 0;
	uint32_t rw_flash_data_addr = 0, tmp_addr = 0;
	int32_t ret = 0, tmp_len = 0, i = 0, transfer_len = 0;
	int32_t total_buf_size = 0, wait_cmd_issue_cnt = 0, write_len = 0;
	FuNvtTsData *ts = &self->ts;

	wait_cmd_issue_cnt = 0;
	flash_cmd_addr = ts->mmap->flash_cmd_addr;
	flash_cmd_issue_addr = ts->mmap->flash_cmd_issue_addr;
	rw_flash_data_addr = ts->mmap->rw_flash_data_addr;

	transfer_len = NVT_TRANSFER_LEN;

	total_buf_size = 64 + xfer->tx_len + xfer->rx_len;
	buf = g_malloc0(total_buf_size);
	if (buf == NULL) {
		g_warning("No memory for %d bytes", total_buf_size);
		return -EAGAIN;
	}

	if ((xfer->tx_len > 0) && xfer->tx_buf != NULL) {
		for (i = 0; i < xfer->tx_len; i += transfer_len) {
			tmp_addr = rw_flash_data_addr + i;
			tmp_len = MIN(xfer->tx_len - i, transfer_len);
			{
				g_autoptr(GError) error_local = NULL;
				if (!fu_memcpy_safe(buf,
						    total_buf_size,
						    0,
						    xfer->tx_buf,
						    xfer->tx_len,
						    i,
						    tmp_len,
						    &error_local)) {
					g_warning("copying tx buffer failed: %s",
						  error_local != NULL ? error_local->message
								      : "unknown");
					return -EFAULT;
				}
			}
			ret = fu_nvt_ts_device_hid_write(self, tmp_addr, buf, tmp_len);
			if (ret < 0) {
				g_warning("Write tx data error");
				return ret;
			}
		}
	}

	memset(buf, 0, total_buf_size);
	buf[0] = xfer->flash_cmd;
	if (xfer->flash_addr_len > 0) {
		buf[1] = xfer->flash_addr & 0xFF;
		buf[2] = (xfer->flash_addr >> 8) & 0xFF;
		buf[3] = (xfer->flash_addr >> 16) & 0xFF;
	} else {
		buf[1] = 0x00;
		buf[2] = 0x00;
		buf[3] = 0x00;
	}
	write_len = xfer->flash_addr_len + xfer->pem_byte_len + xfer->dummy_byte_len + xfer->tx_len;
	if (write_len > 0) {
		buf[5] = write_len & 0xFF;
		buf[6] = (write_len >> 8) & 0xFF;
	} else {
		buf[5] = 0x00;
		buf[6] = 0x00;
	}
	if (xfer->rx_len > 0) {
		buf[7] = xfer->rx_len & 0xFF;
		buf[8] = (xfer->rx_len >> 8) & 0xFF;
	} else {
		buf[7] = 0x00;
		buf[8] = 0x00;
	}
	buf[9] = xfer->flash_checksum & 0xFF;
	buf[10] = (xfer->flash_checksum >> 8) & 0xFF;
	buf[11] = 0xC2;
	ret = fu_nvt_ts_device_hid_write(self, flash_cmd_addr, buf, 12);
	if (ret < 0) {
		g_warning("Write enter GCM error");
		return ret;
	}

	wait_cmd_issue_cnt = 0;
	while (1) {
		/* check flash cmd issue complete */
		ret = fu_nvt_ts_device_hid_read(self, flash_cmd_issue_addr, buf, 1);
		if (ret < 0) {
			g_warning("Read flash_cmd_issue_addr status error");
			return ret;
		}
		if (buf[0] == 0x00)
			break;
		wait_cmd_issue_cnt++;
		if (wait_cmd_issue_cnt > 2000) {
			g_warning("write GCM cmd 0x%02X failed", xfer->flash_cmd);
			return -EAGAIN;
		}
		msleep(1);
	}

	if ((xfer->rx_len > 0) && xfer->rx_buf != NULL) {
		memset(buf, 0, xfer->rx_len);
		for (i = 0; i < xfer->rx_len; i += transfer_len) {
			tmp_addr = rw_flash_data_addr + i;
			tmp_len = MIN(xfer->rx_len - i, transfer_len);
			ret = fu_nvt_ts_device_hid_read(self, tmp_addr, buf, tmp_len);
			if (ret < 0) {
				g_warning("Read rx data fail error");
				return ret;
			}
			{
				g_autoptr(GError) error_local = NULL;
				if (!fu_memcpy_safe(xfer->rx_buf,
						    xfer->rx_len,
						    i,
						    buf,
						    total_buf_size,
						    0,
						    tmp_len,
						    &error_local)) {
					g_warning("copying rx buffer failed: %s",
						  error_local != NULL ? error_local->message
								      : "unknown");
					return -EFAULT;
				}
			}
		}
	}

	return 0;
}

static int32_t
fu_nvt_ts_device_write_enable_gcm(FuNvtTsDevice *self)
{
	FuNvtTsGcmXfer xfer = {0};
	int32_t ret = 0;

	memset(&xfer, 0, sizeof(FuNvtTsGcmXfer));
	xfer.flash_cmd = 0x06;
	ret = fu_nvt_ts_device_gcm_xfer(self, &xfer);
	if (ret) {
		g_warning("Write Enable failed, ret = %d", ret);
		ret = -EAGAIN;
	} else {
		ret = 0;
	}

	return ret;
}

static int32_t
fu_nvt_ts_device_write_status_gcm(FuNvtTsDevice *self, uint8_t status)
{
	uint8_t sr1 = 0;
	int32_t ret = 0;
	FuNvtTsFlashWrsrMethod wrsr_method = 0;
	FuNvtTsGcmXfer xfer = {0};
	FuNvtTsData *ts = &self->ts;

	memset(&xfer, 0, sizeof(FuNvtTsGcmXfer));
	if (self->if_flash_unknown_skip_status_register_control &&
	    ts->match_finfo->mid == FLASH_MFR_UNKNOWN) {
		g_info("unknown flash for flash table skip status register control rdsr");
		xfer.flash_cmd = 0x01;
		xfer.flash_addr = status << 16;
		xfer.flash_addr_len = 1;
	} else {
		wrsr_method = ts->match_finfo->wrsr_method;
		if (wrsr_method == WRSR_01H1BYTE) {
			xfer.flash_cmd = 0x01;
			xfer.flash_addr = status << 16;
			xfer.flash_addr_len = 1;
		} else if (wrsr_method == WRSR_01H2BYTE) {
			xfer.flash_cmd = ts->match_finfo->rdsr1_cmd;
			xfer.rx_len = 1;
			xfer.rx_buf = &sr1;
			ret = fu_nvt_ts_device_gcm_xfer(self, &xfer);
			if (ret) {
				g_warning("Read Status Register-1 fail!!(%d)", ret);
				return -EINVAL;
			} else {
				g_debug("Read Status Register-1 OK. sr1=0x%02X", sr1);
			}

			memset(&xfer, 0, sizeof(FuNvtTsGcmXfer));
			xfer.flash_cmd = 0x01;
			xfer.flash_addr = (status << 16) | (sr1 << 8);
			xfer.flash_addr_len = 2;
		} else {
			g_warning("Unknown or not support write status register method(%u)!",
				  wrsr_method);
			return -EINVAL;
		}
	}
	ret = fu_nvt_ts_device_gcm_xfer(self, &xfer);
	if (ret) {
		g_warning("Write Status GCM fail, ret = %d", ret);
		ret = -EAGAIN;
	} else {
		ret = 0;
	}
	return ret;
}

static int32_t
fu_nvt_ts_device_read_status_gcm(FuNvtTsDevice *self, uint8_t *status)
{
	FuNvtTsGcmXfer xfer = {0};
	int32_t ret = 0;

	memset(&xfer, 0, sizeof(FuNvtTsGcmXfer));
	xfer.flash_cmd = 0x05;
	xfer.rx_len = 1;
	xfer.rx_buf = status;
	ret = fu_nvt_ts_device_gcm_xfer(self, &xfer);
	if (ret) {
		g_warning("Read Status GCM fail, ret = %d", ret);
		ret = -EAGAIN;
	} else {
		ret = 0;
	}

	return ret;
}

static int32_t
fu_nvt_ts_device_sector_erase_gcm(FuNvtTsDevice *self, uint32_t flash_addr)
{
	FuNvtTsGcmXfer xfer = {0};
	int32_t ret = 0;

	memset(&xfer, 0, sizeof(FuNvtTsGcmXfer));
	xfer.flash_cmd = 0x20;
	xfer.flash_addr = flash_addr;
	xfer.flash_addr_len = 3;
	ret = fu_nvt_ts_device_gcm_xfer(self, &xfer);
	if (ret) {
		g_warning("Sector Erase GCM fail, ret = %d", ret);
		ret = -EAGAIN;
	} else {
		ret = 0;
	}

	return ret;
}

static int32_t
fu_nvt_ts_device_page_program_gcm(FuNvtTsDevice *self,
				  uint32_t flash_addr,
				  uint16_t data_len,
				  uint8_t *data)
{
	FuNvtTsGcmXfer xfer = {0};
	uint16_t checksum = 0;
	int32_t ret = 0;
	int32_t i = 0;
	FuNvtTsData *ts = &self->ts;

	/* calculate checksum */
	checksum = (flash_addr & 0xFF);
	checksum += ((flash_addr >> 8) & 0xFF);
	checksum += ((flash_addr >> 16) & 0xFF);
	checksum += ((data_len + 3) & 0xFF);
	checksum += (((data_len + 3) >> 8) & 0xFF);
	for (i = 0; i < data_len; i++) {
		checksum += data[i];
	}
	checksum = ~checksum + 1;

	/* prepare gcm command transfer */
	memset(&xfer, 0, sizeof(FuNvtTsGcmXfer));
	xfer.flash_cmd = ts->flash_prog_data_cmd;
	xfer.flash_addr = flash_addr;
	xfer.flash_addr_len = 3;
	xfer.tx_buf = data;
	xfer.tx_len = data_len;
	xfer.flash_checksum = checksum & 0xFFFF;
	ret = fu_nvt_ts_device_gcm_xfer(self, &xfer);
	if (ret) {
		g_warning("Page Program GCM fail, ret = %d", ret);
		ret = -EAGAIN;
	} else {
		ret = 0;
	}

	return ret;
}

static int32_t
fu_nvt_ts_device_get_checksum_gcm(FuNvtTsDevice *self,
				  uint32_t flash_addr,
				  uint32_t data_len,
				  uint16_t *checksum)
{
	FuNvtTsGcmXfer xfer = {0};
	uint8_t buf[2] = {0};
	int32_t ret = 0;
	FuNvtTsData *ts = &self->ts;

	memset(&xfer, 0, sizeof(FuNvtTsGcmXfer));
	xfer.flash_cmd = ts->flash_read_data_cmd;
	xfer.flash_addr = flash_addr;
	xfer.flash_addr_len = 3;
	xfer.pem_byte_len = ts->flash_read_pem_byte_len;
	xfer.dummy_byte_len = ts->flash_read_dummy_byte_len;
	xfer.rx_len = data_len;
	ret = fu_nvt_ts_device_gcm_xfer(self, &xfer);
	if (ret) {
		g_warning("Get Checksum GCM fail, ret = %d", ret);
		return -EAGAIN;
	}

	buf[0] = 0x00;
	buf[1] = 0x00;
	ret = fu_nvt_ts_device_hid_read(self, ts->mmap->read_flash_checksum_addr, buf, 2);
	if (ret < 0) {
		g_warning("Get checksum error, ret = %d", ret);
		return -EAGAIN;
	}
	*checksum = (buf[1] << 8) | buf[0];

	return 0;
}

static int32_t
fu_nvt_ts_device_switch_gcm(FuNvtTsDevice *self, uint8_t enable)
{
	uint8_t buf[3] = {0}, retry = 0, retry_max = 0;
	int32_t ret = 0;
	FuNvtTsData *ts = &self->ts;

	retry_max = 3;

	while (1) {
		if (enable) {
			buf[0] = 0x55;
			buf[1] = 0xFF;
			buf[2] = 0xAA;
		} else {
			buf[0] = 0xAA;
			buf[1] = 0x55;
			buf[2] = 0xFF;
		}
		ret = fu_nvt_ts_device_hid_write(self, ts->mmap->gcm_code_addr, buf, 3);
		if (ret < 0)
			return ret;
		ret = fu_nvt_ts_device_hid_read(self, ts->mmap->gcm_flag_addr, buf, 1);
		if (ret < 0)
			return ret;
		if (enable) {
			if ((buf[0] & 0x01) == 0x01) {
				ret = 0;
				break;
			}
		} else {
			if ((buf[0] & 0x01) == 0x00) {
				ret = 0;
				break;
			}
		}
		g_info("result mismatch, retry");
		retry++;
		if (retry == retry_max) {
			if (enable)
				g_warning("enable gcm failed");
			else
				g_warning("disable gcm failed");
			ret = -EAGAIN;
			break;
		}
	}

	if (!ret) {
		if (enable)
			g_info("enable gcm ok");
		else
			g_info("disable gcm ok");
	}

	return ret;
}

static int32_t
fu_nvt_ts_device_resume_pd_gcm(FuNvtTsDevice *self)
{
	int32_t ret = 0;
	FuNvtTsGcmXfer xfer = {0};

	memset(&xfer, 0, sizeof(FuNvtTsGcmXfer));
	xfer.flash_cmd = 0xAB;
	ret = fu_nvt_ts_device_gcm_xfer(self, &xfer);
	if (ret) {
		g_warning("Resume PD failed, ret = %d", ret);
		ret = -EAGAIN;
	} else {
		g_debug("Resume PD OK");
		ret = 0;
	}

	return ret;
}

static int32_t
fu_nvt_ts_device_erase_flash_gcm(FuNvtTsDevice *self)
{
	FuNvtTsFlashMfr mid = 0;
	uint8_t status = 0;
	int32_t ret = 0;
	int32_t count = 0;
	int32_t i = 0;
	int32_t flash_address = 0;
	int32_t retry = 0;
	int32_t erase_length = 0;
	int32_t start_sector = 0;
	const FuNvtTsFlashQebInfo *qeb_info_p = NULL;
	FuNvtTsData *ts = &self->ts;
	FuNvtTsFwBin *fwb = &self->fwb;

	if (fwb->flash_start_addr % FLASH_SECTOR_SIZE) {
		g_warning("flash_start_addr should be n*%d", FLASH_SECTOR_SIZE);
		return -EINVAL;
	}

	start_sector = fwb->flash_start_addr / FLASH_SECTOR_SIZE;
	erase_length = fwb->bin_size - fwb->flash_start_addr;
	if (erase_length < 0) {
		g_warning("Wrong erase_length = %d", erase_length);
		return -EINVAL;
	}

	/* write enable */
	ret = fu_nvt_ts_device_write_enable_gcm(self);
	if (ret < 0) {
		g_warning("Write Enable error, ret = %d", ret);
		return -EAGAIN;
	}

	if (self->if_flash_unknown_skip_status_register_control &&
	    ts->match_finfo->mid == FLASH_MFR_UNKNOWN) {
		g_info("unknown flash for flash table skip status register control qeb");
		ret = fu_nvt_ts_device_write_status_gcm(self, status);
		if (ret < 0) {
			g_warning("write status register error, ret = %d", ret);
			return -EAGAIN;
		}
	} else {
		mid = ts->match_finfo->mid;
		qeb_info_p = &ts->match_finfo->qeb_info;
		if ((mid != FLASH_MFR_UNKNOWN) && (qeb_info_p->qeb_pos != QEB_POS_UNKNOWN)) {
			/* check if QE bit is in status register byte 1, if yes set it back to 1 */
			if (qeb_info_p->qeb_pos == QEB_POS_SR_1B) {
				status = (0x01 << qeb_info_p->qeb_order);
			} else {
				status = 0;
			}
			/* write status register */
			ret = fu_nvt_ts_device_write_status_gcm(self, status);
			if (ret < 0) {
				g_warning("Write Status Register error, ret = %d", ret);
				return -EAGAIN;
			}
			g_debug("write status register byte 0x%02X ok", status);
			msleep(1);
		}
	}

	/* read status */
	retry = 0;
	while (1) {
		retry++;
		msleep(5);
		if (retry > 100) {
			g_warning("read status failed, status = 0x%02X", status);
			return -EAGAIN;
		}
		ret = fu_nvt_ts_device_read_status_gcm(self, &status);
		if (ret < 0) {
			g_warning("read status register error, ret = %d", ret);
			continue;
		}
		if ((status & 0x03) == 0x00) {
			g_debug("Read Status Register byte 0x%02X OK", status);
			break;
		}
	}

	if (erase_length % FLASH_SECTOR_SIZE)
		count = erase_length / FLASH_SECTOR_SIZE + start_sector + 1;
	else
		count = erase_length / FLASH_SECTOR_SIZE + start_sector;

	for (i = start_sector; i < count; i++) {
		/* write enable */
		ret = fu_nvt_ts_device_write_enable_gcm(self);
		if (ret < 0) {
			g_warning("Write enable error, ret = %d, page at = %d",
				  ret,
				  i * FLASH_SECTOR_SIZE);
			return -EAGAIN;
		}

		flash_address = i * FLASH_SECTOR_SIZE;

		/* sector erase */
		ret = fu_nvt_ts_device_sector_erase_gcm(self, flash_address);
		if (ret < 0) {
			g_warning("Sector erase error, ret = %d, page at = %d",
				  ret,
				  i * FLASH_SECTOR_SIZE);
			return -EAGAIN;
		}
		msleep(25);

		retry = 0;
		while (1) {
			retry++;
			if (retry > 100) {
				g_warning("Wait sector erase timeout");
				return -EAGAIN;
			}
			ret = fu_nvt_ts_device_read_status_gcm(self, &status);
			if (ret < 0) {
				g_warning("read status register error, ret = %d", ret);
				continue;
			}
			if ((status & 0x03) == 0x00) {
				ret = 0;
				break;
			}
			msleep(5);
		}
	}

	g_info("erase ok");

	return 0;
}

static int32_t
fu_nvt_ts_device_set_prog_flash_method(FuNvtTsDevice *self)
{
	FuNvtTsFlashProgMethod prog_method = 0;
	uint8_t pp4io_en = 0;
	uint8_t q_wr_cmd = 0x00;
	uint8_t bld_rd_addr_sel = 0;
	uint8_t buf[4] = {0};
	int32_t ret = 0;
	FuNvtTsData *ts = &self->ts;

	prog_method = ts->match_finfo->prog_method;
	switch (prog_method) {
	case SPP_0x02:
		ts->flash_prog_data_cmd = 0x02;
		pp4io_en = 0;
		q_wr_cmd = 0x00; /* not 0x02, must 0x00! */
		break;
	case QPP_0x32:
		ts->flash_prog_data_cmd = 0x32;
		pp4io_en = 1;
		q_wr_cmd = 0x32;
		bld_rd_addr_sel = 0;
		break;
	case QPP_0x38:
		ts->flash_prog_data_cmd = 0x38;
		pp4io_en = 1;
		q_wr_cmd = 0x38;
		bld_rd_addr_sel = 1;
		break;
	default:
		g_warning("flash program method %u not support!", prog_method);
		return -EINVAL;
	}
	g_debug("prog_method=%u, ts->flash_prog_data_cmd=0x%02X",
		prog_method,
		ts->flash_prog_data_cmd);
	g_debug("pp4io_en=%d, q_wr_cmd=0x%02X, bld_rd_addr_sel=0x%02X",
		pp4io_en,
		q_wr_cmd,
		bld_rd_addr_sel);

	if (ts->mmap->pp4io_en_reg.addr) {
		ret = fu_nvt_ts_device_write_reg_bits(self, ts->mmap->pp4io_en_reg, pp4io_en);
		if (ret < 0) {
			g_warning("set pp4io_en_reg failed, ret = %d", ret);
			return ret;
		} else {
			g_debug("set pp4io_en_reg=%d done", pp4io_en);
		}
	}
	if (ts->mmap->q_wr_cmd_addr) {
		buf[0] = q_wr_cmd;
		ret = fu_nvt_ts_device_hid_write(self, ts->mmap->q_wr_cmd_addr, buf, 1);
		if (ret < 0) {
			g_warning("set q_wr_cmd_addr failed, ret = %d", ret);
			return ret;
		} else {
			g_debug("set Q_WR_CMD_ADDR=0x%02X done", q_wr_cmd);
		}
	}
	if (pp4io_en) {
		if (ts->mmap->bld_rd_addr_sel_reg.addr) {
			ret = fu_nvt_ts_device_write_reg_bits(self,
							      ts->mmap->bld_rd_addr_sel_reg,
							      bld_rd_addr_sel);
			if (ret < 0) {
				g_warning("set bld_rd_addr_sel_reg failed, ret = %d", ret);
				return ret;
			} else {
				g_debug("set bld_rd_addr_sel_reg=%d done", bld_rd_addr_sel);
			}
		}
	}

	return 0;
}

static int32_t
fu_nvt_ts_device_check_flash_checksum(FuNvtTsDevice *self,
				      uint32_t flash_cksum_status_addr,
				      uint8_t *buf,
				      gboolean allow_retry,
				      gboolean *retry_out);

static int32_t
fu_nvt_ts_device_write_flash_gcm(FuNvtTsDevice *self, FuProgress *progress)
{
	uint8_t page_program_retry = 0;
	uint8_t buf[1] = {0};
	uint32_t flash_address = 0;
	FuNvtTsData *ts = &self->ts;
	FuNvtTsFwBin *fwb = &self->fwb;
	uint32_t flash_cksum_status_addr = ts->mmap->flash_cksum_status_addr;
	uint32_t step = 10, pre = 0, show = 0;
	int32_t ret = 0;
	int32_t i = 0;
	int32_t count = 0;
	int32_t retry = 0;
	uint8_t status = 0;

	fu_nvt_ts_device_set_prog_flash_method(self);

	count = (fwb->bin_size - fwb->flash_start_addr) / FLASH_PAGE_SIZE;
	if ((fwb->bin_size - fwb->flash_start_addr) % FLASH_PAGE_SIZE)
		count++;

	if (progress != NULL)
		fu_progress_set_id(progress, G_STRLOC);
	for (i = 0; i < count; i++) {
		gboolean checksum_ok = FALSE;
		flash_address = i * FLASH_PAGE_SIZE + fwb->flash_start_addr;
		page_program_retry = 0;

		while (!checksum_ok) {
			/* write enable */
			ret = fu_nvt_ts_device_write_enable_gcm(self);
			if (ret < 0) {
				g_warning("write enable error, ret = %d", ret);
				return -EAGAIN;
			}
			/* write page: FLASH_PAGE_SIZE bytes */
			ret = fu_nvt_ts_device_page_program_gcm(
			    self,
			    flash_address,
			    MIN(fwb->bin_size - flash_address, FLASH_PAGE_SIZE),
			    &fwb->bin_data[flash_address]);
			if (ret < 0) {
				g_warning("page program error, ret = %d, i= %d", ret, i);
				return -EAGAIN;
			}

			/* check flash checksum status */
			{
				gboolean retry_page = FALSE;
				ret = fu_nvt_ts_device_check_flash_checksum(self,
									    flash_cksum_status_addr,
									    buf,
									    page_program_retry < 1,
									    &retry_page);
				if (ret < 0)
					return -EAGAIN;
				if (retry_page) {
					page_program_retry++;
					continue;
				}
				checksum_ok = TRUE;
			}
		}

		/* read status */
		retry = 0;
		while (1) {
			retry++;
			if (retry > 200) {
				g_warning("Wait Page Program timeout");
				return -EAGAIN;
			}
			/* read status */
			ret = fu_nvt_ts_device_read_status_gcm(self, &status);
			if (ret < 0) {
				g_warning("Read Status Register error, ret = %d", ret);
				continue;
			}
			if ((status & 0x03) == 0x00) {
				ret = 0;
				break;
			}
			msleep(1);
		}

		/* show progress */
		if (progress != NULL)
			fu_progress_set_percentage_full(progress, i + 1, count);
		show = ((i * 100) / step / count);
		if (pre != show)
			pre = show;
	}
	if (progress != NULL)
		fu_progress_set_percentage(progress, 100);
	g_info("program ok");

	return 0;
}

static int32_t
fu_nvt_ts_device_check_flash_checksum(FuNvtTsDevice *self,
				      uint32_t flash_cksum_status_addr,
				      uint8_t *buf,
				      gboolean allow_retry,
				      gboolean *retry_out)
{
	int32_t ret = 0;
	int32_t retry = 0;

	*retry_out = FALSE;
	while (1) {
		buf[0] = 0x00;
		ret = fu_nvt_ts_device_hid_read(self, flash_cksum_status_addr, buf, 1);
		if (ret < 0)
			return ret;
		if (buf[0] == 0xAA) /* checksum pass */
			return 0;
		if (buf[0] == 0xEA) { /* checksum error */
			if (allow_retry) {
				*retry_out = TRUE;
				return 0;
			}
			g_warning("check flash checksum status error");
			return -EAGAIN;
		}
		retry++;
		if (retry > 20) {
			g_warning("check flash checksum failed, buf[0] = 0x%02X", buf[0]);
			return -EAGAIN;
		}
		msleep(1);
	}
}

static int32_t
fu_nvt_ts_device_set_read_flash_method(FuNvtTsDevice *self)
{
	uint8_t bld_rd_io_sel = 0;
	uint8_t bld_rd_addr_sel = 0;
	int32_t ret = 0;
	FuNvtTsFlashReadMethod rd_method = 0;
	FuNvtTsData *ts = &self->ts;

	bld_rd_io_sel = 0;
	bld_rd_addr_sel = 0;
	ret = 0;
	rd_method = ts->match_finfo->rd_method;
	switch (rd_method) {
	case SISO_0x03:
		ts->flash_read_data_cmd = 0x03;
		ts->flash_read_pem_byte_len = 0;
		ts->flash_read_dummy_byte_len = 0;
		bld_rd_io_sel = 0;
		bld_rd_addr_sel = 0;
		break;
	case SISO_0x0B:
		ts->flash_read_data_cmd = 0x0B;
		ts->flash_read_pem_byte_len = 0;
		ts->flash_read_dummy_byte_len = 1;
		bld_rd_io_sel = 0;
		bld_rd_addr_sel = 0;
		break;
	case SIQO_0x6B:
		ts->flash_read_data_cmd = 0x6B;
		ts->flash_read_pem_byte_len = 0;
		ts->flash_read_dummy_byte_len = 4;
		bld_rd_io_sel = 2;
		bld_rd_addr_sel = 0;
		break;
	case QIQO_0xEB:
		ts->flash_read_data_cmd = 0xEB;
		ts->flash_read_pem_byte_len = 1;
		ts->flash_read_dummy_byte_len = 2;
		bld_rd_io_sel = 2;
		bld_rd_addr_sel = 1;
		break;
	default:
		g_warning("flash read method %u not support!", rd_method);
		return -EINVAL;
	}
	g_debug("rd_method = %u, ts->flash_read_data_cmd = 0x%02X",
		rd_method,
		ts->flash_read_data_cmd);
	g_debug("ts->flash_read_pem_byte_len = %d, ts->flash_read_dummy_byte_len = %d",
		ts->flash_read_pem_byte_len,
		ts->flash_read_dummy_byte_len);
	g_debug("bld_rd_io_sel = %d, bld_rd_addr_sel = %d", bld_rd_io_sel, bld_rd_addr_sel);

	if (ts->mmap->bld_rd_io_sel_reg.addr) {
		ret = fu_nvt_ts_device_write_reg_bits(self,
						      ts->mmap->bld_rd_io_sel_reg,
						      bld_rd_io_sel);
		if (ret < 0) {
			g_warning("set bld_rd_io_sel_reg failed, ret = %d", ret);
			return ret;
		} else {
			g_debug("set bld_rd_io_sel_reg=%d done", bld_rd_io_sel);
		}
	}
	if (ts->mmap->bld_rd_addr_sel_reg.addr) {
		ret = fu_nvt_ts_device_write_reg_bits(self,
						      ts->mmap->bld_rd_addr_sel_reg,
						      bld_rd_addr_sel);
		if (ret < 0) {
			g_warning("set bld_rd_addr_sel_reg failed, ret = %d", ret);
			return ret;
		} else {
			g_debug("set bld_rd_addr_sel_reg=%d done", bld_rd_addr_sel);
		}
	}

	return 0;
}

static int32_t
fu_nvt_ts_device_verify_flash_gcm(FuNvtTsDevice *self)
{
	uint16_t write_checksum = 0;
	uint16_t read_checksum = 0;
	uint32_t flash_addr = 0;
	uint32_t data_len = 0;
	int32_t ret = 0;
	uint32_t total_sector_need_check = 0;
	uint32_t i = 0;
	uint32_t j = 0;
	FuNvtTsFwBin *fwb = &self->fwb;

	fu_nvt_ts_device_set_read_flash_method(self);

	total_sector_need_check = (fwb->bin_size - fwb->flash_start_addr) / SIZE_4KB;
	for (i = 0; i < total_sector_need_check; i++) {
		flash_addr = i * SIZE_4KB + fwb->flash_start_addr;
		data_len = SIZE_4KB;
		/* calculate write_checksum of each 4KB block */
		write_checksum = (flash_addr & 0xFF);
		write_checksum += ((flash_addr >> 8) & 0xFF);
		write_checksum += ((flash_addr >> 16) & 0xFF);
		write_checksum += ((data_len) & 0xFF);
		write_checksum += (((data_len) >> 8) & 0xFF);
		for (j = 0; j < data_len; j++) {
			write_checksum += fwb->bin_data[flash_addr + j];
		}
		write_checksum = ~write_checksum + 1;

		ret = fu_nvt_ts_device_get_checksum_gcm(self, flash_addr, data_len, &read_checksum);
		if (ret < 0) {
			g_warning("Get Checksum failed, ret = %d, i = %u", ret, i);
			return -EAGAIN;
		}
		if (write_checksum != read_checksum) {
			g_warning("Verify Failed, i = %u, write_checksum = 0x%04X, "
				  "read_checksum = 0x%04X",
				  i,
				  write_checksum,
				  read_checksum);
			return -EAGAIN;
		}
	}

	g_info("verify ok");

	return 0;
}

static int32_t
fu_nvt_ts_device_find_match_flash_info(FuNvtTsDevice *self)
{
	uint32_t i = 0, total_info_items = 0;
	const FuNvtTsFlashInfo *finfo = NULL;
	FuNvtTsData *ts = &self->ts;

	total_info_items =
	    sizeof(fu_nvt_ts_flash_info_table) / sizeof(fu_nvt_ts_flash_info_table[0]);
	for (i = 0; i < total_info_items; i++) {
		if (fu_nvt_ts_flash_info_table[i].mid == ts->flash_mid) {
			/* mid of this flash info item match current flash's mid */
			if (fu_nvt_ts_flash_info_table[i].did == ts->flash_did) {
				/* specific mid and did of this flash info item
				 * match current flash's mid and did */
				break;
			} else if (fu_nvt_ts_flash_info_table[i].did == FLASH_DID_ALL) {
				/* mid of this flash info item match current
				 * flash's mid, and all did have same flash info */
				break;
			}
		} else if (fu_nvt_ts_flash_info_table[i].mid == FLASH_MFR_UNKNOWN) {
			/* reach the last item of fu_nvt_ts_flash_info_table, no flash info item
			 * matched */
			break;
		} else {
			/* mid of this flash info item not math current flash's mid */
			continue;
		}
	}
	ts->match_finfo = &fu_nvt_ts_flash_info_table[i];
	finfo = ts->match_finfo;
	g_debug("matched flash info item %u:", i);
	g_debug("mid = 0x%02X, did = 0x%04X, qeb_pos = %u",
		finfo->mid,
		finfo->did,
		finfo->qeb_info.qeb_pos);
	g_debug("qeb_order = %u, rd_method = %u, prog_method = %u",
		finfo->qeb_info.qeb_order,
		finfo->rd_method,
		finfo->prog_method);
	g_debug("wrsr_method = %u, rdsr1_cmd_ = 0x%02X", finfo->wrsr_method, finfo->rdsr1_cmd);

	return 0;
}

static int32_t
fu_nvt_ts_device_read_flash_mid_did_gcm(FuNvtTsDevice *self)
{
	uint8_t buf[3];
	int32_t ret = 0;
	FuNvtTsGcmXfer xfer = {0};
	FuNvtTsData *ts = &self->ts;

	memset(&xfer, 0, sizeof(FuNvtTsGcmXfer));
	xfer.flash_cmd = 0x9F;
	xfer.rx_buf = buf;
	xfer.rx_len = 3;
	ret = fu_nvt_ts_device_gcm_xfer(self, &xfer);
	if (ret) {
		g_warning("read flash mid did gcm failed, ret = %d", ret);
		return -EAGAIN;
	}

	ts->flash_mid = buf[0];
	ts->flash_did = (buf[1] << 8) | buf[2];
	g_debug("flash mid = 0x%02X, did = 0x%04X", ts->flash_mid, ts->flash_did);
	fu_nvt_ts_device_find_match_flash_info(self);
	g_debug("read mid did ok");
	return 0;
}

static int32_t
fu_nvt_ts_device_check_end_flag(FuNvtTsDevice *self)
{
	FuNvtTsFwBin *fwb = &self->fwb;
	const uint32_t sz = fwb->bin_size;
	const uint8_t *base = (const uint8_t *)fwb->bin_data;
	char end_char[BIN_END_FLAG_LEN_MAX] = {0};
	uint32_t new_sz = 0;
	uint32_t flag_offset = 0;
	uint32_t delta = 0;
	int32_t ret = 0;

	ret = fu_nvt_ts_device_find_fw_bin_end_flag(base, sz, &flag_offset, &delta);
	if (ret) {
		g_warning("binary end flag not found at end or at (-0x1000) steps (expected [%s]), "
			  "abort.",
			  HID_FW_BIN_END_NAME_FULL);
		return ret;
	}

	{
		g_autoptr(GError) error_local = NULL;
		if (!fu_memcpy_safe((guint8 *)end_char,
				    sizeof(end_char),
				    0,
				    base,
				    sz,
				    flag_offset,
				    BIN_END_FLAG_LEN_MAX,
				    &error_local)) {
			g_warning("copying end flag failed: %s",
				  error_local != NULL ? error_local->message : "unknown");
			return -EFAULT;
		}
	}
	g_info("found hid fw bin flag [%.*s] at offset 0x%X (probe delta 0x%X)",
	       BIN_END_FLAG_LEN_FULL,
	       end_char + 1,
	       flag_offset + 1,
	       delta);
	g_info("raw end bytes = [%c%c%c%c]", end_char[0], end_char[1], end_char[2], end_char[3]);

	new_sz = flag_offset + BIN_END_FLAG_LEN_MAX;
	g_info("update fw bin size from 0x%X to 0x%X", sz, new_sz);
	fwb->bin_size = new_sz;
	return 0;
}

static int32_t
fu_nvt_ts_device_get_binary_and_flash_start_addr_from_blob(FuNvtTsDevice *self,
							   const guint8 *data,
							   gsize size)
{
	int32_t ret = 0;
	FuNvtTsFwBin *fwb = &self->fwb;
	FuNvtTsData *ts = &self->ts;

	if (data == NULL || size == 0) {
		g_warning("invalid firmware blob (data=%p size=0x%zX)", data, size);
		return -EINVAL;
	}

	fu_nvt_ts_device_fw_bin_clear(self);

	if (size > MAX_BIN_SIZE) {
		g_warning("firmware blob too large (0x%zX > 0x%X)", size, (uint32_t)MAX_BIN_SIZE);
		return -E2BIG;
	}

	fwb->bin_data = g_malloc(size);
	if (fwb->bin_data == NULL) {
		g_warning("malloc for firmware blob failed (size=0x%zX)", size);
		return -ENOMEM;
	}
	{
		g_autoptr(GError) error_local = NULL;
		if (!fu_memcpy_safe(fwb->bin_data, size, 0, data, size, 0, size, &error_local)) {
			g_warning("copying firmware blob failed: %s",
				  error_local != NULL ? error_local->message : "unknown");
			return -EFAULT;
		}
	}
	fwb->bin_size = (uint32_t)size;

	/* check and trim according to end-flag if needed */
	ret = fu_nvt_ts_device_check_end_flag(self);
	if (ret)
		return ret;

	if (ts->fmap->flash_normal_fw_start_addr == 0) {
		g_warning("normal FW flash should not start from 0");
		return -EFAULT;
	}

	/* always use FLASH_NORMAL start (0x2000) */
	fwb->flash_start_addr = ts->fmap->flash_normal_fw_start_addr;

	g_info("flashing starts from 0x%X", fwb->flash_start_addr);
	g_info("size of bin for update = 0x%05X", fwb->bin_size);
	g_info("get binary from blob ok");

	return 0;
}

static int32_t
fu_nvt_ts_device_update_firmware(FuNvtTsDevice *self,
				 const guint8 *data,
				 gsize size,
				 FuProgress *progress)
{
	int32_t ret = 0;

	g_info("get binary and flash start address");
	ret = fu_nvt_ts_device_get_binary_and_flash_start_addr_from_blob(self, data, size);
	if (ret) {
		fu_nvt_ts_device_fw_bin_clear(self);
		return ret;
	}

	g_info("enable gcm");
	ret = fu_nvt_ts_device_switch_gcm(self, 1);
	if (ret) {
		fu_nvt_ts_device_fw_bin_clear(self);
		return ret;
	}

	g_info("resume pd");
	ret = fu_nvt_ts_device_resume_pd_gcm(self);
	if (ret) {
		fu_nvt_ts_device_fw_bin_clear(self);
		return ret;
	}

	g_info("read flash id");
	ret = fu_nvt_ts_device_read_flash_mid_did_gcm(self);
	if (ret) {
		fu_nvt_ts_device_fw_bin_clear(self);
		return ret;
	}

	g_info("erase");
	ret = fu_nvt_ts_device_erase_flash_gcm(self);
	if (ret) {
		fu_nvt_ts_device_fw_bin_clear(self);
		return ret;
	}

	g_info("program");
	ret = fu_nvt_ts_device_write_flash_gcm(self, progress);
	if (ret) {
		fu_nvt_ts_device_fw_bin_clear(self);
		return ret;
	}

	g_info("verify");
	ret = fu_nvt_ts_device_verify_flash_gcm(self);
	if (ret) {
		fu_nvt_ts_device_fw_bin_clear(self);
		return ret;
	}

	fu_nvt_ts_device_fw_bin_clear(self);
	return 0;
}

static void
fu_nvt_ts_device_bootloader_reset(FuNvtTsDevice *self)
{
	uint8_t buf[1] = {0};
	FuNvtTsData *ts = &self->ts;

	buf[0] = 0x69;
	fu_nvt_ts_device_hid_write(self, ts->mmap->swrst_sif_addr, buf, 1);
	g_debug("0x69 to 0x%06X", ts->mmap->swrst_sif_addr);
	msleep(235);
}

static void
fu_nvt_ts_device_sw_reset_and_idle(FuNvtTsDevice *self)
{
	uint8_t buf[1] = {0};
	FuNvtTsData *ts = &self->ts;

	buf[0] = 0xAA;
	fu_nvt_ts_device_hid_write(self, ts->mmap->swrst_sif_addr, buf, 1);
	g_debug("0xAA to 0x%06X", ts->mmap->swrst_sif_addr);
	msleep(50);
}

static void
fu_nvt_ts_device_stop_crc_reboot(FuNvtTsDevice *self)
{
	uint8_t buf[1] = {0};
	uint8_t retry = 20;
	FuNvtTsData *ts = &self->ts;

	g_debug("%s (0xA5 to 0x%06X) %d times", __func__, ts->mmap->bld_spe_pups_addr, retry);
	while (retry--) {
		buf[0] = 0xA5;
		fu_nvt_ts_device_hid_write(self, ts->mmap->bld_spe_pups_addr, buf, 1);
	}
	msleep(5);
}

static int32_t
fu_nvt_ts_device_update_firmware_reset(FuNvtTsDevice *self,
				       const guint8 *data,
				       gsize size,
				       FuProgress *progress)
{
	int32_t ret = 0;

	fu_nvt_ts_device_bootloader_reset(self);
	fu_nvt_ts_device_sw_reset_and_idle(self);
	fu_nvt_ts_device_stop_crc_reboot(self);

	ret = fu_nvt_ts_device_update_firmware(self, data, size, progress);

	fu_nvt_ts_device_bootloader_reset(self);

	return ret;
}

G_DEFINE_TYPE(FuNvtTsDevice, fu_nvt_ts_device, FU_TYPE_HIDRAW_DEVICE)

static gboolean
fu_nvt_ts_device_probe(FuDevice *device, GError **error)
{
	const gchar *subsystem;

	g_info("device probe");

	subsystem = fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device));
	if (g_strcmp0(subsystem, "hidraw") != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "subsystem is not hidraw");
		return FALSE;
	}

	return TRUE;
}

static void
fu_nvt_ts_device_clear_fw_reset_state(FuNvtTsDevice *self)
{
	uint8_t buf[1] = {0};
	FuNvtTsData *ts = &self->ts;

	fu_nvt_ts_device_hid_write(self, ts->mmap->event_buf_reset_state_addr, buf, 1);
	g_debug("0x00 to 0x%06X", ts->mmap->event_buf_reset_state_addr);
}

static int32_t
fu_nvt_ts_device_check_fw_reset_state(FuNvtTsDevice *self, uint8_t state)
{
	uint8_t buf[1] = {0};
	int32_t ret = 0, retry = 100;
	FuNvtTsData *ts = &self->ts;

	g_info("checking reset state from address 0x%06X for state 0x%02X",
	       ts->mmap->event_buf_reset_state_addr,
	       state);

	/* first clear */
	fu_nvt_ts_device_clear_fw_reset_state(self);

	while (--retry) {
		msleep(10);
		fu_nvt_ts_device_hid_read(self, ts->mmap->event_buf_reset_state_addr, buf, 1);

		if ((buf[0] >= state) && (buf[0] <= RESET_STATE_MAX)) {
			ret = 0;
			break;
		}
	}

	if (retry == 0) {
		g_warning("error, reset state buf[0] = 0x%02X", buf[0]);
		ret = -EAGAIN;
	} else {
		g_info("reset state 0x%02X pass", state);
	}

	return ret;
}

static int32_t
fu_nvt_ts_device_get_fw_ver(FuNvtTsDevice *self)
{
	uint8_t buf[2] = {0};
	int32_t ret = 0;
	uint8_t retry = 10;
	FuNvtTsData *ts = &self->ts;

	while (--retry) {
		fu_nvt_ts_device_hid_read(self, ts->mmap->event_map_fwinfo_addr, buf, 2);
		if ((uint8_t)(buf[0] + buf[1]) == 0xFF)
			break;
	}

	if (!retry) {
		g_warning("FW info is broken, fw_ver=0x%02X, ~fw_ver=0x%02X", buf[0], buf[1]);
		return -EAGAIN;
	}

	ts->fw_ver = buf[0];
	g_info("fw_ver = 0x%02X", ts->fw_ver);
	return ret;
}

static int32_t
fu_nvt_ts_device_read_flash_data_gcm(FuNvtTsDevice *self,
				     uint32_t flash_addr,
				     uint16_t len,
				     uint8_t *out)
{
	FuNvtTsGcmXfer xfer = {0};
	uint8_t buf[2] = {0};
	uint16_t rd_checksum = 0;
	uint16_t calc = 0;
	uint16_t i = 0;
	uint8_t retry = 10;
	int32_t ret = 0;
	FuNvtTsData *ts = &self->ts;

	if (out == NULL || len == 0)
		return -EINVAL;
	/* keep this simple; expand later if you want >256 */
	if (len > 256)
		return -EINVAL;

	while (retry--) {
		calc = 0;
		calc += (flash_addr >> 16) & 0xFF;
		calc += (flash_addr >> 8) & 0xFF;
		calc += (flash_addr >> 0) & 0xFF;
		calc += (len >> 8) & 0xFF;
		calc += (len >> 0) & 0xFF;

		memset(&xfer, 0, sizeof(xfer));
		xfer.flash_cmd = ts->flash_read_data_cmd;
		xfer.flash_addr = flash_addr;
		xfer.flash_addr_len = 3;
		xfer.pem_byte_len = ts->flash_read_pem_byte_len;
		xfer.dummy_byte_len = ts->flash_read_dummy_byte_len;
		xfer.rx_buf = out;
		xfer.rx_len = len;

		ret = fu_nvt_ts_device_gcm_xfer(self, &xfer);
		if (ret)
			continue;

		ret = fu_nvt_ts_device_hid_read(self, ts->mmap->read_flash_checksum_addr, buf, 2);
		if (ret < 0)
			continue;

		rd_checksum = (uint16_t)(buf[1] << 8 | buf[0]);

		for (i = 0; i < len; i++)
			calc += out[i];

		/* 0xFFFF - sum + 1 */
		calc = 65535 - calc + 1;

		if (rd_checksum == calc)
			return 0;

		g_debug("flash read checksum mismatch: rd=0x%04X calc=0x%04X", rd_checksum, calc);
	}

	return -EAGAIN;
}

static int32_t
fu_nvt_ts_device_read_flash_pid_gcm(FuNvtTsDevice *self)
{
	uint8_t pid_raw[4] = {0};
	char pid_str[5] = {0};
	guint64 pid64 = 0;
	int32_t ret = 0;
	FuNvtTsData *ts = &self->ts;

	if (ts->fmap == NULL || ts->fmap->flash_pid_addr == 0)
		return -EINVAL;

	ret = fu_nvt_ts_device_switch_gcm(self, 1);
	if (ret)
		return ret;

	ret = fu_nvt_ts_device_resume_pd_gcm(self);
	if (ret)
		return ret;

	ret = fu_nvt_ts_device_read_flash_mid_did_gcm(self);
	if (ret)
		return ret;

	ret = fu_nvt_ts_device_set_read_flash_method(self);
	if (ret)
		return ret;

	ret = fu_nvt_ts_device_read_flash_data_gcm(self, ts->fmap->flash_pid_addr, 4, pid_raw);
	if (ret)
		return ret;

	/* same byte order as your userland tool: [2][3][0][1] */
	pid_str[0] = (char)pid_raw[2];
	pid_str[1] = (char)pid_raw[3];
	pid_str[2] = (char)pid_raw[0];
	pid_str[3] = (char)pid_raw[1];
	pid_str[4] = '\0';

	{
		g_autoptr(GError) error_local = NULL;
		if (!fu_strtoull(pid_str, &pid64, 0, 0xFFFF, FU_INTEGER_BASE_16, &error_local)) {
			g_warning("invalid pid read from flash: '%s' (%02X %02X %02X %02X): %s",
				  pid_str,
				  pid_raw[0],
				  pid_raw[1],
				  pid_raw[2],
				  pid_raw[3],
				  error_local != NULL ? error_local->message : "unknown");
			return -EINVAL;
		}
	}

	ts->flash_pid = (uint16_t)pid64;
	if (ts->flash_pid == 0x0000 || ts->flash_pid == 0xFFFF) {
		g_warning("pid in flash should not be 0x0000 or 0xFFFF");
		return -EINVAL;
	}

	g_info("flash_pid = 0x%04X", ts->flash_pid);
	return 0;
}

static gboolean
fu_nvt_ts_device_setup(FuDevice *device, GError **error)
{
	FuDeviceClass *parent_class;
	g_autofree gchar *iid = NULL;
	int32_t ret = 0;
	uint8_t debug_buf[6] = {0};
	FuNvtTsDevice *self = FU_NVT_TS_DEVICE(device);
	FuNvtTsData *ts = &self->ts;

	g_info("device setup");

	if (fu_device_get_vendor(device) == NULL)
		fu_device_set_vendor(device, "Novatek");

	if (fu_device_get_name(device) == NULL)
		fu_device_set_name(device, "Novatek Touchscreen");

	if (!fu_device_open(device, error))
		return FALSE;

	/* todo: add mmap mapping if support more IC later on */
	ts->mmap = &nt36536_cascade_memory_map;
	ts->fmap = &nt36536_flash_map;

	fu_nvt_ts_device_hid_read(self, ts->mmap->chip_ver_trim_addr, debug_buf, 6);
	g_info("IC chip id: %02X %02X %02X %02X %02X %02X",
	       debug_buf[0],
	       debug_buf[1],
	       debug_buf[2],
	       debug_buf[3],
	       debug_buf[4],
	       debug_buf[5]);

	if (!(fu_nvt_ts_device_check_fw_reset_state(self, RESET_STATE_NORMAL_RUN) == 0 &&
	      fu_nvt_ts_device_get_fw_ver(self) == 0)) {
		g_info("FW is not ready");
		ts->fw_ver = 0;
	}

	fu_device_add_protocol(device, "tw.com.novatek.ts");
	fu_device_set_summary(device, "Novatek touchscreen controller");
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_NUMBER);

	fu_device_set_version_raw(device, ts->fw_ver);

	fu_nvt_ts_device_bootloader_reset(self);
	fu_nvt_ts_device_sw_reset_and_idle(self);
	fu_nvt_ts_device_stop_crc_reboot(self);

	/* get pid in flash to build GUID */
	ret = fu_nvt_ts_device_read_flash_pid_gcm(self);
	if (ret) {
		g_warning("failed to read flash PID (ret=%d)", ret);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "failed to read flash PID (ret=%d)",
			    ret);
		ret = fu_device_close(device, NULL);
		return FALSE;
	}

	fu_nvt_ts_device_bootloader_reset(self);

	fu_device_build_vendor_id_u16(device, "HIDRAW", NVT_VID_NUM);

	/* build instance id for GUID */
	iid = g_strdup_printf("NVT_TS\\VID_0603\\PJID_%04X", ts->flash_pid);

	/* turn instance IDs into GUIDs */
	fu_device_add_instance_id(device, iid);

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	/* fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD); */

	if (!fu_device_close(device, error))
		return FALSE;

	parent_class = FU_DEVICE_CLASS(fu_nvt_ts_device_parent_class);
	if (parent_class->setup != NULL)
		return parent_class->setup(device, error);

	return TRUE;
}

static void
fu_nvt_ts_device_init(FuNvtTsDevice *self)
{
	g_info("device init");
	self->if_flash_unknown_skip_status_register_control = TRUE;
}

static gchar *
fu_nvt_ts_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint16((guint16)version_raw, fu_device_get_version_format(device));
}

typedef struct {
	const guint8 *data;
	gsize size;
	FuProgress *progress;
} FuNvtTsUpdateCtx;

static gboolean
fu_nvt_ts_device_update_firmware_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuNvtTsUpdateCtx *ctx = (FuNvtTsUpdateCtx *)user_data;
	FuNvtTsDevice *self = FU_NVT_TS_DEVICE(device);
	int32_t ret;

	ret = fu_nvt_ts_device_update_firmware_reset(self, ctx->data, ctx->size, ctx->progress);
	if (ret == 0) {
		g_info("Update Normal FW OK");
		return TRUE;
	}

	g_warning("Update Normal FW Failed (ret=%d)", ret);
	g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_WRITE, "Update Normal FW Failed (ret=%d)", ret);
	return FALSE;
}

static gboolean
fu_nvt_ts_device_write_firmware(FuDevice *device,
				FuFirmware *firmware,
				FuProgress *progress,
				FwupdInstallFlags flags,
				GError **error)
{
	g_autoptr(GBytes) blob = NULL;
	const guint8 *data = NULL;
	gsize size = 0;
	int32_t ret = 0;
	FuNvtTsUpdateCtx ctx = {0};
	FuNvtTsDevice *self = FU_NVT_TS_DEVICE(device);
	FuNvtTsData *ts = &self->ts;

	g_info("device write firmware");

	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL)
		return FALSE;

	data = g_bytes_get_data(blob, &size);

#if NVT_DEBUG_DRY_RUN
	g_info("NVT_DEBUG_DRY_RUN=1: skip real update");
	ret = fu_nvt_ts_device_get_binary_and_flash_start_addr_from_blob(self, data, size);
	if (ret) {
		g_warning("failed to parse fw blob (ret=%d)", ret);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "failed to parse fw blob (ret=%d)",
			    ret);
		return FALSE;
	}
	g_info("dry-run info: fw_size=0x%05X, will flash from 0x%X to 0x%X",
	       self->fwb.bin_size,
	       self->fwb.flash_start_addr,
	       self->fwb.bin_size);
	/* clean up allocated fw buffer */
	fu_nvt_ts_device_fw_bin_clear(self);
	return TRUE;
#endif

	if (!fu_device_open(device, error))
		return FALSE;

	ctx.data = data;
	ctx.size = size;
	ctx.progress = progress;
	if (!fu_device_retry(device, fu_nvt_ts_device_update_firmware_cb, 3, &ctx, error)) {
		(void)fu_device_close(device, NULL);
		return FALSE;
	}

	if (!(fu_nvt_ts_device_check_fw_reset_state(self, RESET_STATE_NORMAL_RUN) == 0 &&
	      fu_nvt_ts_device_get_fw_ver(self) == 0)) {
		g_warning("FW is not ready");
		ts->fw_ver = 0;
	}

	fu_device_set_version_raw(device, ts->fw_ver);

	if (!fu_device_close(device, error))
		return FALSE;

	return TRUE;
}

static void
fu_nvt_ts_device_class_init(FuNvtTsDeviceClass *klass)
{
	FuDeviceClass *device_class;

	device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_nvt_ts_device_probe;
	device_class->setup = fu_nvt_ts_device_setup;
	device_class->write_firmware = fu_nvt_ts_device_write_firmware;
	device_class->convert_version = fu_nvt_ts_device_convert_version;
}
