/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "config.h"

#include "fu-nvt-ts-plugin.h"

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "FuPluginNvtTs"

#define NVT_TS_PLUGIN_VERSION "1"
#define NVT_DEBUG_DRY_RUN     0

static struct nvt_ts_data ts_data;
static struct nvt_ts_data *const ts = &ts_data;
static struct fw_bin fwb;
static FuDevice *nvt_dev;
gboolean if_flash_unknown_skip_status_register_control = TRUE;

static void
nvt_fw_bin_clear(void)
{
	g_clear_pointer(&fwb.bin_data, g_free);
	fwb.bin_size = 0;
}

static int32_t
ctp_hid_read_dev(FuDevice *device, uint32_t addr, uint8_t *data, uint16_t len, GError **error)
{
	int32_t ret;
	uint8_t buf_set[12];
	g_autofree uint8_t *buf_get = NULL;

	if (len == 0) {
		NVT_ERR("len must be > 0");
		return -EINVAL;
	}

	NVT_DBG("read addr=0x%08x len=%u", addr, len);

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

	ret = fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(device),
					   buf_set,
					   sizeof(buf_set),
					   FU_IOCTL_FLAG_RETRY,
					   error);
	if (!ret) {
		NVT_ERR("set feature failed");
		return -EIO;
	}

	/* get feature */
	buf_get = g_new0(uint8_t, len + 1);
	buf_get[0] = NVT_TS_REPORT_ID;

	ret = fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(device),
					   buf_get,
					   len + 1,
					   FU_IOCTL_FLAG_RETRY,
					   error);
	if (!ret) {
		NVT_ERR("get feature failed");
		return -EIO;
	}

	memcpy(data, buf_get + 1, len);

	return 0;
}

static int32_t
ctp_hid_write_dev(FuDevice *device, uint32_t addr, uint8_t *data, uint16_t len, GError **error)
{
	int32_t ret;
	uint16_t write_len, report_len;
	g_autofree uint8_t *buf_set = NULL;

	if (len == 0) {
		NVT_ERR("len must be > 0");
		return -EINVAL;
	}

	NVT_DBG("write addr=0x%08x len=%u, data:", addr, len);
	NVT_DBG_HEX(data, len);

	write_len = len + 5;

	report_len = write_len + 1;

	buf_set = g_new0(uint8_t, report_len);

	buf_set[0] = NVT_TS_REPORT_ID; /* report ID */
	buf_set[1] = (uint8_t)(write_len & 0xFF);
	buf_set[2] = (uint8_t)((write_len >> 8) & 0xFF);

	buf_set[3] = (uint8_t)((addr >> 0) & 0xFF);
	buf_set[4] = (uint8_t)((addr >> 8) & 0xFF);
	buf_set[5] = (uint8_t)((addr >> 16) & 0xFF);

	memcpy(&buf_set[6], data, len);

	ret = fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(device),
					   buf_set,
					   report_len,
					   FU_IOCTL_FLAG_RETRY,
					   error);
	if (!ret) {
		NVT_ERR("set feature failed");
		return -EIO;
	}

	return 0;
}

static int32_t
ctp_hid_read(uint32_t addr, uint8_t *data, uint16_t len)
{
	return ctp_hid_read_dev(nvt_dev, addr, data, len, NULL);
}

static int32_t
ctp_hid_write(uint32_t addr, uint8_t *data, uint16_t len)
{
	return ctp_hid_write_dev(nvt_dev, addr, data, len, NULL);
}

static int32_t
nvt_write_reg_bits(nvt_ts_reg_t reg, uint8_t val)
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
			NVT_ERR("mask all bits zero!\n");
			break;
		}
		shift++;
	}
	ret = ctp_hid_read(addr, buf, 1);
	if (ret < 0) {
		NVT_ERR("ctp_hid_read failed!(%d)\n", ret);
		return ret;
	}
	temp = buf[0] & (~mask);
	temp |= ((val << shift) & mask);
	buf[0] = temp;
	ret = ctp_hid_write(addr, buf, 1);
	if (ret < 0) {
		NVT_ERR("ctp_hid_write failed!(%d)\n", ret);
		return ret;
	}

	return ret;
}

static int32_t
find_fw_bin_end_flag(const uint8_t *base, uint32_t size, uint32_t *flag_offset, uint32_t *delta_out)
{
	const uint16_t step = 0x1000;
	const char *expect = HID_FW_BIN_END_NAME_FULL;
	uint32_t delta = 0;
	char end_char[BIN_END_FLAG_LEN_MAX] = {0};

	if (base == NULL || size < BIN_END_FLAG_LEN_MAX)
		return -EFAULT;

	for (delta = 0; size >= BIN_END_FLAG_LEN_MAX + delta; delta += step) {
		uint32_t offset = size - delta - BIN_END_FLAG_LEN_MAX;

		memcpy(end_char, base + offset, BIN_END_FLAG_LEN_MAX);
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
nvt_gcm_xfer(gcm_xfer_t *xfer)
{
	g_autofree uint8_t *buf = NULL;
	uint32_t flash_cmd_addr = 0, flash_cmd_issue_addr = 0;
	uint32_t rw_flash_data_addr = 0, tmp_addr = 0;
	int32_t ret = 0, tmp_len = 0, i = 0, transfer_len = 0;
	int32_t total_buf_size = 0, wait_cmd_issue_cnt = 0, write_len = 0;

	wait_cmd_issue_cnt = 0;
	flash_cmd_addr = ts->mmap->flash_cmd_addr;
	flash_cmd_issue_addr = ts->mmap->flash_cmd_issue_addr;
	rw_flash_data_addr = ts->mmap->rw_flash_data_addr;

	transfer_len = NVT_TRANSFER_LEN;

	total_buf_size = 64 + xfer->tx_len + xfer->rx_len;
	buf = g_malloc0(total_buf_size);
	if (buf == NULL) {
		NVT_ERR("No memory for %d bytes", total_buf_size);
		return -EAGAIN;
	}

	if ((xfer->tx_len > 0) && xfer->tx_buf != NULL) {
		for (i = 0; i < xfer->tx_len; i += transfer_len) {
			tmp_addr = rw_flash_data_addr + i;
			tmp_len = MIN(xfer->tx_len - i, transfer_len);
			memcpy(buf, xfer->tx_buf + i, tmp_len);
			ret = ctp_hid_write(tmp_addr, buf, tmp_len);
			if (ret < 0) {
				NVT_ERR("Write tx data error");
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
	ret = ctp_hid_write(flash_cmd_addr, buf, 12);
	if (ret < 0) {
		NVT_ERR("Write enter GCM error");
		return ret;
	}

	wait_cmd_issue_cnt = 0;
	while (1) {
		/* check flash cmd issue complete */
		ret = ctp_hid_read(flash_cmd_issue_addr, buf, 1);
		if (ret < 0) {
			NVT_ERR("Read flash_cmd_issue_addr status error");
			return ret;
		}
		if (buf[0] == 0x00) {
			break;
		}
		wait_cmd_issue_cnt++;
		if (wait_cmd_issue_cnt > 2000) {
			NVT_ERR("write GCM cmd 0x%02X failed", xfer->flash_cmd);
			return -EAGAIN;
		}
		msleep(1);
	}

	if ((xfer->rx_len > 0) && xfer->rx_buf != NULL) {
		memset(buf, 0, xfer->rx_len);
		for (i = 0; i < xfer->rx_len; i += transfer_len) {
			tmp_addr = rw_flash_data_addr + i;
			tmp_len = MIN(xfer->rx_len - i, transfer_len);
			ret = ctp_hid_read(tmp_addr, buf, tmp_len);
			if (ret < 0) {
				NVT_ERR("Read rx data fail error");
				return ret;
			}
			memcpy(xfer->rx_buf + i, buf, tmp_len);
		}
	}

	return 0;
}

static int32_t
write_enable_gcm(void)
{
	gcm_xfer_t xfer = {0};
	int32_t ret = 0;

	memset(&xfer, 0, sizeof(gcm_xfer_t));
	xfer.flash_cmd = 0x06;
	ret = nvt_gcm_xfer(&xfer);
	if (ret) {
		NVT_ERR("Write Enable failed, ret = %d", ret);
		ret = -EAGAIN;
	} else {
		ret = 0;
	}

	return ret;
}

static int32_t
write_status_gcm(uint8_t status)
{
	uint8_t sr1 = 0;
	int32_t ret = 0;
	flash_wrsr_method_t wrsr_method = 0;
	gcm_xfer_t xfer = {0};

	memset(&xfer, 0, sizeof(gcm_xfer_t));
	if (if_flash_unknown_skip_status_register_control &&
	    ts->match_finfo->mid == FLASH_MFR_UNKNOWN) {
		NVT_LOG("unknown flash for flash table skip status register control rdsr");
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
			ret = nvt_gcm_xfer(&xfer);
			if (ret) {
				NVT_ERR("Read Status Register-1 fail!!(%d)", ret);
				return -EINVAL;
			} else {
				NVT_DBG("Read Status Register-1 OK. sr1=0x%02X", sr1);
			}

			memset(&xfer, 0, sizeof(gcm_xfer_t));
			xfer.flash_cmd = 0x01;
			xfer.flash_addr = (status << 16) | (sr1 << 8);
			xfer.flash_addr_len = 2;
		} else {
			NVT_ERR("Unknown or not support write status register method(%u)!",
				wrsr_method);
			return -EINVAL;
		}
	}
	ret = nvt_gcm_xfer(&xfer);
	if (ret) {
		NVT_ERR("Write Status GCM fail, ret = %d", ret);
		ret = -EAGAIN;
	} else {
		ret = 0;
	}
	return ret;
}

static int32_t
read_status_gcm(uint8_t *status)
{
	gcm_xfer_t xfer = {0};
	int32_t ret = 0;

	memset(&xfer, 0, sizeof(gcm_xfer_t));
	xfer.flash_cmd = 0x05;
	xfer.rx_len = 1;
	xfer.rx_buf = status;
	ret = nvt_gcm_xfer(&xfer);
	if (ret) {
		NVT_ERR("Read Status GCM fail, ret = %d", ret);
		ret = -EAGAIN;
	} else {
		ret = 0;
	}

	return ret;
}

static int32_t
sector_erase_gcm(uint32_t flash_addr)
{
	gcm_xfer_t xfer = {0};
	int32_t ret = 0;

	memset(&xfer, 0, sizeof(gcm_xfer_t));
	xfer.flash_cmd = 0x20;
	xfer.flash_addr = flash_addr;
	xfer.flash_addr_len = 3;
	ret = nvt_gcm_xfer(&xfer);
	if (ret) {
		NVT_ERR("Sector Erase GCM fail, ret = %d", ret);
		ret = -EAGAIN;
	} else {
		ret = 0;
	}

	return ret;
}

static int32_t
page_program_gcm(uint32_t flash_addr, uint16_t data_len, uint8_t *data)
{
	gcm_xfer_t xfer = {0};
	uint16_t checksum = 0;
	int32_t ret = 0;
	int32_t i = 0;

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
	memset(&xfer, 0, sizeof(gcm_xfer_t));
	xfer.flash_cmd = ts->flash_prog_data_cmd;
	xfer.flash_addr = flash_addr;
	xfer.flash_addr_len = 3;
	xfer.tx_buf = data;
	xfer.tx_len = data_len;
	xfer.flash_checksum = checksum & 0xFFFF;
	ret = nvt_gcm_xfer(&xfer);
	if (ret) {
		NVT_ERR("Page Program GCM fail, ret = %d", ret);
		ret = -EAGAIN;
	} else {
		ret = 0;
	}

	return ret;
}

static int32_t
get_checksum_gcm(uint32_t flash_addr, uint32_t data_len, uint16_t *checksum)
{
	gcm_xfer_t xfer = {0};
	uint8_t buf[2] = {0};
	int32_t ret = 0;

	memset(&xfer, 0, sizeof(gcm_xfer_t));
	xfer.flash_cmd = ts->flash_read_data_cmd;
	xfer.flash_addr = flash_addr;
	xfer.flash_addr_len = 3;
	xfer.pem_byte_len = ts->flash_read_pem_byte_len;
	xfer.dummy_byte_len = ts->flash_read_dummy_byte_len;
	xfer.rx_len = data_len;
	ret = nvt_gcm_xfer(&xfer);
	if (ret) {
		NVT_ERR("Get Checksum GCM fail, ret = %d", ret);
		return -EAGAIN;
	}

	buf[0] = 0x00;
	buf[1] = 0x00;
	ret = ctp_hid_read(ts->mmap->read_flash_checksum_addr, buf, 2);
	if (ret < 0) {
		NVT_ERR("Get checksum error, ret = %d", ret);
		return -EAGAIN;
	}
	*checksum = (buf[1] << 8) | buf[0];

	return 0;
}

static int32_t
switch_gcm(uint8_t enable)
{
	uint8_t buf[3] = {0}, retry = 0, retry_max = 0;
	int32_t ret = 0;

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
		ret = ctp_hid_write(ts->mmap->gcm_code_addr, buf, 3);
		if (ret < 0)
			return ret;
		ret = ctp_hid_read(ts->mmap->gcm_flag_addr, buf, 1);
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
		NVT_LOG("Result mismatch, retry");
		retry++;
		if (retry == retry_max) {
			if (enable)
				NVT_ERR("Enable gcm failed");
			else
				NVT_ERR("Disable gcm failed");
			ret = -EAGAIN;
			break;
		}
	}

	if (!ret) {
		if (enable)
			NVT_LOG("Enable gcm OK");
		else
			NVT_LOG("Disable gcm OK");
	}

	return ret;
}

static int32_t
resume_pd_gcm(void)
{
	int32_t ret = 0;
	gcm_xfer_t xfer = {0};

	memset(&xfer, 0, sizeof(gcm_xfer_t));
	xfer.flash_cmd = 0xAB;
	ret = nvt_gcm_xfer(&xfer);
	if (ret) {
		NVT_ERR("Resume PD failed, ret = %d", ret);
		ret = -EAGAIN;
	} else {
		NVT_DBG("Resume PD OK");
		ret = 0;
	}

	return ret;
}

static int32_t
erase_flash_gcm(void)
{
	flash_mfr_t mid = 0;
	uint8_t status = 0;
	int32_t ret = 0;
	int32_t count = 0;
	int32_t i = 0;
	int32_t flash_address = 0;
	int32_t retry = 0;
	int32_t erase_length = 0;
	int32_t start_sector = 0;
	const flash_qeb_info_t *qeb_info_p = NULL;

	if (fwb.flash_start_addr % FLASH_SECTOR_SIZE) {
		NVT_ERR("flash_start_addr should be n*%d", FLASH_SECTOR_SIZE);
		return -EINVAL;
	}

	start_sector = fwb.flash_start_addr / FLASH_SECTOR_SIZE;
	erase_length = fwb.bin_size - fwb.flash_start_addr;
	if (erase_length < 0) {
		NVT_ERR("Wrong erase_length = %d", erase_length);
		return -EINVAL;
	}

	/* write enable */
	ret = write_enable_gcm();
	if (ret < 0) {
		NVT_ERR("Write Enable error, ret = %d", ret);
		return -EAGAIN;
	}

	if (if_flash_unknown_skip_status_register_control &&
	    ts->match_finfo->mid == FLASH_MFR_UNKNOWN) {
		NVT_LOG("unknown flash for flash table skip status register control qeb");
		ret = write_status_gcm(status);
		if (ret < 0) {
			NVT_ERR("Write Status Register error, ret = %d", ret);
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
				status = 0x00;
			}
			/* write status register */
			ret = write_status_gcm(status);
			if (ret < 0) {
				NVT_ERR("Write Status Register error, ret = %d", ret);
				return -EAGAIN;
			}
			NVT_DBG("Write Status Register byte 0x%02X OK", status);
			msleep(1);
		}
	}

	/* read status */
	retry = 0;
	while (1) {
		retry++;
		msleep(5);
		if (retry > 100) {
			NVT_ERR("Read Status failed, status = 0x%02X", status);
			return -EAGAIN;
		}
		ret = read_status_gcm(&status);
		if (ret < 0) {
			NVT_ERR("Read Status Register error, ret = %d", ret);
			continue;
		}
		if ((status & 0x03) == 0x00) {
			NVT_DBG("Read Status Register byte 0x%02X OK", status);
			break;
		}
	}

	if (erase_length % FLASH_SECTOR_SIZE)
		count = erase_length / FLASH_SECTOR_SIZE + start_sector + 1;
	else
		count = erase_length / FLASH_SECTOR_SIZE + start_sector;

	for (i = start_sector; i < count; i++) {
		/* write enable */
		ret = write_enable_gcm();
		if (ret < 0) {
			NVT_ERR("Write enable error, ret = %d, page at = %d",
				ret,
				i * FLASH_SECTOR_SIZE);
			return -EAGAIN;
		}

		flash_address = i * FLASH_SECTOR_SIZE;

		/* sector erase */
		ret = sector_erase_gcm(flash_address);
		if (ret < 0) {
			NVT_ERR("Sector erase error, ret = %d, page at = %d",
				ret,
				i * FLASH_SECTOR_SIZE);
			return -EAGAIN;
		}
		msleep(25);

		retry = 0;
		while (1) {
			retry++;
			if (retry > 100) {
				NVT_ERR("Wait sector erase timeout");
				return -EAGAIN;
			}
			ret = read_status_gcm(&status);
			if (ret < 0) {
				NVT_ERR("Read status register error, ret = %d", ret);
				continue;
			}
			if ((status & 0x03) == 0x00) {
				ret = 0;
				break;
			}
			msleep(5);
		}
	}

	NVT_LOG("Erase OK");

	return 0;
}

static int32_t
nvt_set_prog_flash_method(void)
{
	flash_prog_method_t prog_method = 0;
	uint8_t pp4io_en = 0;
	uint8_t q_wr_cmd = 0;
	uint8_t bld_rd_addr_sel = 0;
	uint8_t buf[4] = {0};
	int32_t ret = 0;

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
		NVT_ERR("flash program method %u not support!", prog_method);
		return -EINVAL;
	}
	NVT_DBG("prog_method=%u, ts->flash_prog_data_cmd=0x%02X",
		prog_method,
		ts->flash_prog_data_cmd);
	NVT_DBG("pp4io_en=%d, q_wr_cmd=0x%02X, bld_rd_addr_sel=0x%02X",
		pp4io_en,
		q_wr_cmd,
		bld_rd_addr_sel);

	if (ts->mmap->pp4io_en_reg.addr) {
		ret = nvt_write_reg_bits(ts->mmap->pp4io_en_reg, pp4io_en);
		if (ret < 0) {
			NVT_ERR("set pp4io_en_reg failed, ret = %d", ret);
			return ret;
		} else {
			NVT_DBG("set pp4io_en_reg=%d done", pp4io_en);
		}
	}
	if (ts->mmap->q_wr_cmd_addr) {
		buf[0] = q_wr_cmd;
		ret = ctp_hid_write(ts->mmap->q_wr_cmd_addr, buf, 1);
		if (ret < 0) {
			NVT_ERR("set q_wr_cmd_addr failed, ret = %d", ret);
			return ret;
		} else {
			NVT_DBG("set Q_WR_CMD_ADDR=0x%02X done", q_wr_cmd);
		}
	}
	if (pp4io_en) {
		if (ts->mmap->bld_rd_addr_sel_reg.addr) {
			ret = nvt_write_reg_bits(ts->mmap->bld_rd_addr_sel_reg, bld_rd_addr_sel);
			if (ret < 0) {
				NVT_ERR("set bld_rd_addr_sel_reg failed, ret = %d", ret);
				return ret;
			} else {
				NVT_DBG("set bld_rd_addr_sel_reg=%d done", bld_rd_addr_sel);
			}
		}
	}

	return 0;
}

static int32_t
write_flash_gcm(void)
{
	uint8_t page_program_retry = 0;
	uint8_t buf[1] = {0};
	uint32_t flash_address = 0;
	uint32_t flash_cksum_status_addr = ts->mmap->flash_cksum_status_addr;
	uint32_t step = 10, pre = 0, show = 0;
	int32_t ret = 0;
	int32_t i = 0;
	int32_t count = 0;
	int32_t retry = 0;
	uint8_t status = 0;

	nvt_set_prog_flash_method();

	count = (fwb.bin_size - fwb.flash_start_addr) / FLASH_PAGE_SIZE;
	if ((fwb.bin_size - fwb.flash_start_addr) % FLASH_PAGE_SIZE)
		count++;

	for (i = 0; i < count; i++) {
		flash_address = i * FLASH_PAGE_SIZE + fwb.flash_start_addr;
		page_program_retry = 0;

	page_program_start:
		/* write enable */
		ret = write_enable_gcm();
		if (ret < 0) {
			NVT_ERR("Write Enable error, ret = %d", ret);
			return -EAGAIN;
		}
		/* write page: FLASH_PAGE_SIZE bytes */
		/* page program */
		ret = page_program_gcm(flash_address,
				       MIN(fwb.bin_size - flash_address, FLASH_PAGE_SIZE),
				       &fwb.bin_data[flash_address]);
		if (ret < 0) {
			NVT_ERR("Page Program error, ret = %d, i= %d", ret, i);
			return -EAGAIN;
		}

		/* check flash checksum status */
		retry = 0;
		while (1) {
			buf[0] = 0x00;
			ret = ctp_hid_read(flash_cksum_status_addr, buf, 1);
			if (buf[0] == 0xAA) { /* checksum pass */
				ret = 0;
				break;
			} else if (buf[0] == 0xEA) { /* checksum error */
				if (page_program_retry < 1) {
					page_program_retry++;
					goto page_program_start;
				} else {
					NVT_ERR("Check Flash Checksum Status error");
					return -EAGAIN;
				}
			}
			retry++;
			if (retry > 20) {
				NVT_ERR("Check flash checksum fail, buf[0] = 0x%02X", buf[0]);
				return -EAGAIN;
			}
			msleep(1);
		}

		/* read status */
		retry = 0;
		while (1) {
			retry++;
			if (retry > 200) {
				NVT_ERR("Wait Page Program timeout");
				return -EAGAIN;
			}
			/* read status */
			ret = read_status_gcm(&status);
			if (ret < 0) {
				NVT_ERR("Read Status Register error, ret = %d", ret);
				continue;
			}
			if ((status & 0x03) == 0x00) {
				ret = 0;
				break;
			}
			msleep(1);
		}

		/* show progress */
		show = ((i * 100) / step / count);
		if (pre != show) {
			NVT_LOG("Programming...%2u%%", show * step);
			pre = show;
		}
	}
	NVT_LOG("Programming...%d%%", 100);
	NVT_LOG("Program OK");

	return 0;
}

static int32_t
nvt_set_read_flash_method(void)
{
	uint8_t bld_rd_io_sel = 0;
	uint8_t bld_rd_addr_sel = 0;
	int32_t ret = 0;
	flash_read_method_t rd_method = 0;

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
		NVT_ERR("flash read method %u not support!", rd_method);
		return -EINVAL;
	}
	NVT_DBG("rd_method = %u, ts->flash_read_data_cmd = 0x%02X",
		rd_method,
		ts->flash_read_data_cmd);
	NVT_DBG("ts->flash_read_pem_byte_len = %d, ts->flash_read_dummy_byte_len = %d",
		ts->flash_read_pem_byte_len,
		ts->flash_read_dummy_byte_len);
	NVT_DBG("bld_rd_io_sel = %d, bld_rd_addr_sel = %d", bld_rd_io_sel, bld_rd_addr_sel);

	if (ts->mmap->bld_rd_io_sel_reg.addr) {
		ret = nvt_write_reg_bits(ts->mmap->bld_rd_io_sel_reg, bld_rd_io_sel);
		if (ret < 0) {
			NVT_ERR("set bld_rd_io_sel_reg failed, ret = %d", ret);
			return ret;
		} else {
			NVT_DBG("set bld_rd_io_sel_reg=%d done", bld_rd_io_sel);
		}
	}
	if (ts->mmap->bld_rd_addr_sel_reg.addr) {
		ret = nvt_write_reg_bits(ts->mmap->bld_rd_addr_sel_reg, bld_rd_addr_sel);
		if (ret < 0) {
			NVT_ERR("set bld_rd_addr_sel_reg failed, ret = %d", ret);
			return ret;
		} else {
			NVT_DBG("set bld_rd_addr_sel_reg=%d done", bld_rd_addr_sel);
		}
	}

	return 0;
}

static int32_t
verify_flash_gcm(void)
{
	uint16_t write_checksum = 0;
	uint16_t read_checksum = 0;
	uint32_t flash_addr = 0;
	uint32_t data_len = 0;
	int32_t ret = 0;
	uint32_t total_sector_need_check = 0;
	uint32_t i = 0;
	uint32_t j = 0;

	nvt_set_read_flash_method();

	total_sector_need_check = (fwb.bin_size - fwb.flash_start_addr) / SIZE_4KB;
	for (i = 0; i < total_sector_need_check; i++) {
		flash_addr = i * SIZE_4KB + fwb.flash_start_addr;
		data_len = SIZE_4KB;
		/* calculate write_checksum of each 4KB block */
		write_checksum = (flash_addr & 0xFF);
		write_checksum += ((flash_addr >> 8) & 0xFF);
		write_checksum += ((flash_addr >> 16) & 0xFF);
		write_checksum += ((data_len)&0xFF);
		write_checksum += (((data_len) >> 8) & 0xFF);
		for (j = 0; j < data_len; j++) {
			write_checksum += fwb.bin_data[flash_addr + j];
		}
		write_checksum = ~write_checksum + 1;

		ret = get_checksum_gcm(flash_addr, data_len, &read_checksum);
		if (ret < 0) {
			NVT_ERR("Get Checksum failed, ret = %d, i = %u", ret, i);
			return -EAGAIN;
		}
		if (write_checksum != read_checksum) {
			NVT_ERR("Verify Failed, i = %u, write_checksum = 0x%04X, "
				"read_checksum = 0x%04X",
				i,
				write_checksum,
				read_checksum);
			return -EAGAIN;
		}
	}

	NVT_LOG("Verify OK");

	return 0;
}

static int32_t
nvt_find_match_flash_info(void)
{
	uint32_t i = 0, total_info_items = 0;
	const struct flash_info *finfo = NULL;

	total_info_items = sizeof(flash_info_table) / sizeof(flash_info_table[0]);
	for (i = 0; i < total_info_items; i++) {
		if (flash_info_table[i].mid == ts->flash_mid) {
			/* mid of this flash info item match current flash's mid */
			if (flash_info_table[i].did == ts->flash_did) {
				/* specific mid and did of this flash info item
				 * match current flash's mid and did */
				break;
			} else if (flash_info_table[i].did == FLASH_DID_ALL) {
				/* mid of this flash info item match current
				 * flash's mid, and all did have same flash info */
				break;
			}
		} else if (flash_info_table[i].mid == FLASH_MFR_UNKNOWN) {
			/* reach the last item of flash_info_table, no flash info item matched */
			break;
		} else {
			/* mid of this flash info item not math current flash's mid */
			continue;
		}
	}
	ts->match_finfo = &flash_info_table[i];
	finfo = ts->match_finfo;
	NVT_DBG("matched flash info item %u:", i);
	NVT_DBG("mid = 0x%02X, did = 0x%04X, qeb_pos = %u",
		finfo->mid,
		finfo->did,
		finfo->qeb_info.qeb_pos);
	NVT_DBG("qeb_order = %u, rd_method = %u, prog_method = %u",
		finfo->qeb_info.qeb_order,
		finfo->rd_method,
		finfo->prog_method);
	NVT_DBG("wrsr_method = %u, rdsr1_cmd_ = 0x%02X", finfo->wrsr_method, finfo->rdsr1_cmd);

	return 0;
}

static int32_t
read_flash_mid_did_gcm(void)
{
	uint8_t buf[3];
	int32_t ret = 0;
	gcm_xfer_t xfer = {0};

	memset(&xfer, 0, sizeof(gcm_xfer_t));
	xfer.flash_cmd = 0x9F;
	xfer.rx_buf = buf;
	xfer.rx_len = 3;
	ret = nvt_gcm_xfer(&xfer);
	if (ret) {
		NVT_ERR("Read Flash MID DID GCM fail, ret = %d", ret);
		return -EAGAIN;
	}

	ts->flash_mid = buf[0];
	ts->flash_did = (buf[1] << 8) | buf[2];
	NVT_DBG("Flash MID = 0x%02X, DID = 0x%04X", ts->flash_mid, ts->flash_did);
	nvt_find_match_flash_info();
	NVT_DBG("Read MID DID OK");
	return 0;
}

static int32_t
check_end_flag(void)
{
	const uint32_t sz = fwb.bin_size;
	const uint8_t *base = (const uint8_t *)fwb.bin_data;
	char end_char[BIN_END_FLAG_LEN_MAX] = {0};
	uint32_t new_sz = 0;
	uint32_t flag_offset = 0;
	uint32_t delta = 0;
	int32_t ret = 0;

	ret = find_fw_bin_end_flag(base, sz, &flag_offset, &delta);
	if (ret) {
		NVT_ERR("binary end flag not found at end or at (-0x1000) steps (expected [%s]), "
			"abort.",
			HID_FW_BIN_END_NAME_FULL);
		return ret;
	}

	memcpy(end_char, base + flag_offset, BIN_END_FLAG_LEN_MAX);
	NVT_LOG("Found HID FW bin flag [%.*s] at offset 0x%X (probe delta 0x%X).",
		BIN_END_FLAG_LEN_FULL,
		end_char + 1,
		flag_offset + 1,
		delta);
	NVT_LOG("Raw end bytes = [%c%c%c%c]", end_char[0], end_char[1], end_char[2], end_char[3]);

	new_sz = flag_offset + BIN_END_FLAG_LEN_MAX;
	NVT_LOG("Update fw bin size from 0x%X to 0x%X", sz, new_sz);
	fwb.bin_size = new_sz;
	return 0;
}

static int32_t
get_binary_and_flash_start_addr_from_blob(const guint8 *data, gsize size)
{
	int32_t ret = 0;

	if (data == NULL || size == 0) {
		NVT_ERR("invalid firmware blob (data=%p size=0x%zX)", data, size);
		return -EINVAL;
	}

	nvt_fw_bin_clear();

	if (size > MAX_BIN_SIZE) {
		NVT_ERR("firmware blob too large (0x%zX > 0x%X)", size, (uint32_t)MAX_BIN_SIZE);
		return -E2BIG;
	}

	fwb.bin_data = g_malloc(size);
	if (fwb.bin_data == NULL) {
		NVT_ERR("malloc for firmware blob failed (size=0x%zX)", size);
		return -ENOMEM;
	}
	memcpy(fwb.bin_data, data, size);
	fwb.bin_size = (uint32_t)size;

	/* check and trim according to end-flag if needed */
	ret = check_end_flag();
	if (ret)
		return ret;

	if (ts->fmap->flash_normal_fw_start_addr == 0) {
		NVT_ERR("normal FW flash should not start from 0");
		return -EFAULT;
	}

	/* always use FLASH_NORMAL start (0x2000) */
	fwb.flash_start_addr = ts->fmap->flash_normal_fw_start_addr;

	NVT_LOG("Flashing starts from 0x%X", fwb.flash_start_addr);
	NVT_LOG("Size of bin for update = 0x%05X", fwb.bin_size);
	NVT_LOG("Get binary from blob OK");

	return 0;
}

static int32_t
update_firmware(const guint8 *data, gsize size)
{
	int32_t ret = 0;

	NVT_LOG("Get binary and flash start address");
	ret = get_binary_and_flash_start_addr_from_blob(data, size);
	if (ret) {
		nvt_fw_bin_clear();
		return ret;
	}

	NVT_LOG("Enable gcm");
	ret = switch_gcm(1);
	if (ret) {
		nvt_fw_bin_clear();
		return ret;
	}

	NVT_LOG("Resume PD");
	ret = resume_pd_gcm();
	if (ret) {
		nvt_fw_bin_clear();
		return ret;
	}

	NVT_LOG("Read flash ID");
	ret = read_flash_mid_did_gcm();
	if (ret) {
		nvt_fw_bin_clear();
		return ret;
	}

	NVT_LOG("Erase");
	ret = erase_flash_gcm();
	if (ret) {
		nvt_fw_bin_clear();
		return ret;
	}

	NVT_LOG("Program");
	ret = write_flash_gcm();
	if (ret) {
		nvt_fw_bin_clear();
		return ret;
	}

	NVT_LOG("Verify");
	ret = verify_flash_gcm();
	if (ret) {
		nvt_fw_bin_clear();
		return ret;
	}

	nvt_fw_bin_clear();
	return 0;
}

static void
nvt_bootloader_reset(void)
{
	uint8_t buf[1] = {0};

	buf[0] = 0x69;
	ctp_hid_write(ts->mmap->swrst_sif_addr, buf, 1);
	NVT_DBG("0x69 to 0x%06X", ts->mmap->swrst_sif_addr);
	msleep(235);
}

static void
nvt_sw_reset_and_idle(void)
{
	uint8_t buf[1] = {0};

	buf[0] = 0xAA;
	ctp_hid_write(ts->mmap->swrst_sif_addr, buf, 1);
	NVT_DBG("0xAA to 0x%06X", ts->mmap->swrst_sif_addr);
	msleep(50);
}

static void
nvt_stop_crc_reboot(void)
{
	uint8_t buf[1] = {0};
	uint8_t retry = 20;

	NVT_DBG("%s (0xA5 to 0x%06X) %d times", __func__, ts->mmap->bld_spe_pups_addr, retry);
	while (retry--) {
		buf[0] = 0xA5;
		ctp_hid_write(ts->mmap->bld_spe_pups_addr, buf, 1);
	}
	msleep(5);
}

static int32_t
update_firmware_reset(const guint8 *data, gsize size)
{
	int32_t ret = 0;

	nvt_bootloader_reset();
	nvt_sw_reset_and_idle();
	nvt_stop_crc_reboot();

	ret = update_firmware(data, size);

	nvt_bootloader_reset();

	return ret;
}

struct _FuNvtTsPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuNvtTsPlugin, fu_nvt_ts_plugin, FU_TYPE_PLUGIN)

static void
fu_nvt_ts_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin;

	G_OBJECT_CLASS(fu_nvt_ts_plugin_parent_class)->constructed(obj);

	NVT_LOG("plugin constructed");

	plugin = FU_PLUGIN(obj);

	if (fu_plugin_get_name(plugin) == NULL)
		fwupd_plugin_set_name(FWUPD_PLUGIN(plugin), "nvt_ts");

	fu_plugin_add_device_udev_subsystem(plugin, "hidraw");

	fu_plugin_add_device_gtype(plugin, FU_TYPE_NVT_TS_DEVICE);

	/* fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_NVT_TS_FIRMWARE); */
}

static void
fu_nvt_ts_plugin_class_init(FuNvtTsPluginClass *klass)
{
	GObjectClass *object_class;

	NVT_LOG("plugin class init");

	object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_nvt_ts_plugin_constructed;
}

static void
fu_nvt_ts_plugin_init(FuNvtTsPlugin *self)
{
	NVT_LOG("plugin init, plugin version %s", NVT_TS_PLUGIN_VERSION);
}

struct _FuNvtTsDevice {
	FuHidrawDevice parent_instance;
};

G_DEFINE_TYPE(FuNvtTsDevice, fu_nvt_ts_device, FU_TYPE_HIDRAW_DEVICE)

static gboolean
fu_nvt_ts_device_probe(FuDevice *device, GError **error)
{
	const gchar *subsystem;

	NVT_LOG("device probe");

	subsystem = fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device));
	if (g_strcmp0(subsystem, "hidraw") != 0) {
		NVT_ERR("subsystem is not hidraw");
		return FALSE;
	}

	return TRUE;
}

static void
nvt_clear_fw_reset_state(void)
{
	uint8_t buf[1] = {0};

	ctp_hid_write(ts->mmap->event_buf_reset_state_addr, buf, 1);
	NVT_DBG("0x00 to 0x%06X", ts->mmap->event_buf_reset_state_addr);
}

static int32_t
nvt_check_fw_reset_state(uint8_t state)
{
	uint8_t buf[1] = {0};
	int32_t ret = 0, retry = 100;

	NVT_LOG("checking reset state from address 0x%06X for state 0x%02X",
		ts->mmap->event_buf_reset_state_addr,
		state);

	/* first clear */
	nvt_clear_fw_reset_state();

	while (--retry) {
		msleep(10);
		ctp_hid_read(ts->mmap->event_buf_reset_state_addr, buf, 1);

		if ((buf[0] >= state) && (buf[0] <= RESET_STATE_MAX)) {
			ret = 0;
			break;
		}
	}

	if (retry == 0) {
		NVT_ERR("error, reset state buf[0] = 0x%02X", buf[0]);
		ret = -EAGAIN;
	} else {
		NVT_LOG("reset state 0x%02X pass", state);
	}

	return ret;
}

static int32_t
nvt_get_fw_ver(void)
{
	uint8_t buf[2] = {0};
	int32_t ret = 0;
	uint8_t retry = 10;

	while (--retry) {
		ctp_hid_read(ts->mmap->event_map_fwinfo_addr, buf, 2);
		if ((uint8_t)(buf[0] + buf[1]) == 0xFF)
			break;
	}

	if (!retry) {
		NVT_ERR("FW info is broken, fw_ver=0x%02X, ~fw_ver=0x%02X", buf[0], buf[1]);
		return -EAGAIN;
	}

	ts->fw_ver = buf[0];
	NVT_LOG("fw_ver = 0x%02X", ts->fw_ver);
	return ret;
}

static int32_t
nvt_read_flash_data_gcm(uint32_t flash_addr, uint16_t len, uint8_t *out)
{
	gcm_xfer_t xfer = {0};
	uint8_t buf[2] = {0};
	uint16_t rd_checksum = 0;
	uint16_t calc = 0;
	uint16_t i = 0;
	uint8_t retry = 10;
	int32_t ret = 0;

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

		ret = nvt_gcm_xfer(&xfer);
		if (ret)
			continue;

		ret = ctp_hid_read(ts->mmap->read_flash_checksum_addr, buf, 2);
		if (ret < 0)
			continue;

		rd_checksum = (uint16_t)(buf[1] << 8 | buf[0]);

		for (i = 0; i < len; i++)
			calc += out[i];

		/* 0xFFFF - sum + 1 */
		calc = 65535 - calc + 1;

		if (rd_checksum == calc)
			return 0;

		NVT_DBG("flash read checksum mismatch: rd=0x%04X calc=0x%04X", rd_checksum, calc);
	}

	return -EAGAIN;
}

static int32_t
nvt_read_flash_pid_gcm(void)
{
	uint8_t pid_raw[4] = {0};
	char pid_str[5] = {0};
	gchar *endptr = NULL;
	guint64 pid64 = 0;
	int32_t ret = 0;

	if (ts->fmap == NULL || ts->fmap->flash_pid_addr == 0)
		return -EINVAL;

	ret = switch_gcm(1);
	if (ret)
		return ret;

	ret = resume_pd_gcm();
	if (ret)
		return ret;

	ret = read_flash_mid_did_gcm();
	if (ret)
		return ret;

	ret = nvt_set_read_flash_method();
	if (ret)
		return ret;

	ret = nvt_read_flash_data_gcm(ts->fmap->flash_pid_addr, 4, pid_raw);
	if (ret)
		return ret;

	/* same byte order as your userland tool: [2][3][0][1] */
	pid_str[0] = (char)pid_raw[2];
	pid_str[1] = (char)pid_raw[3];
	pid_str[2] = (char)pid_raw[0];
	pid_str[3] = (char)pid_raw[1];
	pid_str[4] = '\0';

	pid64 = g_ascii_strtoull(pid_str, &endptr, 16);
	if (endptr == pid_str || *endptr != '\0' || pid64 > 0xFFFF) {
		NVT_ERR("invalid PID read from flash: '%s' (%02X %02X %02X %02X)",
			pid_str,
			pid_raw[0],
			pid_raw[1],
			pid_raw[2],
			pid_raw[3]);
		return -EINVAL;
	}

	ts->flash_pid = (uint16_t)pid64;
	if (ts->flash_pid == 0x0000 || ts->flash_pid == 0xFFFF) {
		NVT_ERR("pid in flash should not be 0x0000 or 0xFFFF");
		return -EINVAL;
	}

	NVT_LOG("flash_pid = 0x%04X", ts->flash_pid);
	return 0;
}

static gboolean
fu_nvt_ts_device_setup(FuDevice *device, GError **error)
{
	FuDeviceClass *parent_class;
	g_autofree gchar *version = NULL;
	g_autofree gchar *iid = NULL;
	int32_t ret = 0;
	uint8_t debug_buf[6] = {0};

	NVT_LOG("device setup");

	if (fu_device_get_vendor(device) == NULL)
		fu_device_set_vendor(device, "Novatek");

	if (fu_device_get_name(device) == NULL)
		fu_device_set_name(device, "Novatek Touchscreen");

	if (!fu_device_open(device, error))
		return FALSE;

	nvt_dev = device;

	/* todo: add mmap mapping if support more IC later on */
	ts->mmap = &nt36536_cascade_memory_map;
	ts->fmap = &nt36536_flash_map;

	ctp_hid_read(0x1fb104, debug_buf, 6);
	NVT_LOG("IC chip id: %02X %02X %02X %02X %02X %02X",
		debug_buf[0],
		debug_buf[1],
		debug_buf[2],
		debug_buf[3],
		debug_buf[4],
		debug_buf[5]);

	if (!(nvt_check_fw_reset_state(RESET_STATE_NORMAL_RUN) == 0 && nvt_get_fw_ver() == 0)) {
		NVT_LOG("FW is not ready");
		ts->fw_ver = 0;
	}

	fu_device_add_protocol(device, "tw.com.novatek.ts");
	fu_device_set_summary(device, "Novatek touchscreen controller");
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_NUMBER);

	version = g_strdup_printf("%u", ts->fw_ver);
	fu_device_set_version(device, version);

	nvt_bootloader_reset();
	nvt_sw_reset_and_idle();
	nvt_stop_crc_reboot();

	/* get pid in flash to build GUID */
	ret = nvt_read_flash_pid_gcm();
	if (ret) {
		NVT_SET_ERR(FWUPD_ERROR_READ, "failed to read flash PID (ret=%d)", ret);
		ret = fu_device_close(device, NULL);
		return FALSE;
	}

	nvt_bootloader_reset();

	fu_device_build_vendor_id_u16(device, "HIDRAW", NVT_VID_NUM);

	/* build instance id for GUID */
	iid = g_strdup_printf("NVT_TS\\VID_0603\\PJID_%04X", ts->flash_pid);

	/* turn instance IDs into GUIDs */
	fu_device_add_instance_id(device, iid);

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	/* fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD); */

	if (!fu_device_close(device, NULL))
		return FALSE;

	parent_class = FU_DEVICE_CLASS(fu_nvt_ts_device_parent_class);
	if (parent_class->setup != NULL)
		return parent_class->setup(device, error);

	return TRUE;
}

static void
fu_nvt_ts_device_init(FuNvtTsDevice *self)
{
	NVT_LOG("device init");
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
	int32_t ret;
	uint8_t retry_left = 3;

	NVT_LOG("device write firmware");

	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL)
		return FALSE;

	data = g_bytes_get_data(blob, &size);

#if NVT_DEBUG_DRY_RUN
	NVT_LOG("NVT_DEBUG_DRY_RUN=1: skip real update");
	ret = get_binary_and_flash_start_addr_from_blob(data, size);
	if (ret) {
		NVT_SET_ERR(FWUPD_ERROR_INVALID_FILE, "failed to parse fw blob (ret=%d)", ret);
		return FALSE;
	}
	NVT_LOG("Dry-run info: fw_size=0x%05X, will flash from 0x%X to 0x%X",
		fwb.bin_size,
		fwb.flash_start_addr,
		fwb.bin_size);
	/* clean up allocated fw buffer */
	nvt_fw_bin_clear();
	return TRUE;
#endif

	if (!fu_device_open(device, error))
		return FALSE;

	do {
		ret = update_firmware_reset(data, size);
		if (!ret) {
			NVT_LOG("Update Normal FW OK");
			break;
		}

		retry_left--;
		if (retry_left > 0)
			NVT_LOG("Update failed (ret=%d), retry %u", ret, retry_left);
	} while (retry_left > 0);

	if (ret) {
		NVT_SET_ERR(FWUPD_ERROR_WRITE, "Update Normal FW Failed (ret=%d)", ret);
		ret = fu_device_close(device, NULL);
		return FALSE;
	}

	if (!(nvt_check_fw_reset_state(RESET_STATE_NORMAL_RUN) == 0 && nvt_get_fw_ver() == 0)) {
		NVT_ERR("FW is not ready");
		ts->fw_ver = 0;
	}

	fu_device_set_version(device, g_strdup_printf("%u", ts->fw_ver));

	if (!fu_device_close(device, NULL))
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
}
