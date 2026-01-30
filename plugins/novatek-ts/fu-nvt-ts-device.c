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
    .flash_max_size = FU_NVT_TS_FLASH_MAP_CONST_FLASH_MAX_SIZE,
};

struct _FuNvtTsDevice {
	FuHidrawDevice parent_instance;
	const FuNvtTsMemMap *mmap;
	const FuNvtTsFlashMap *fmap;
	guint8 fw_ver;
	guint8 flash_mid;
	guint16 flash_did;
	guint16 flash_pid;
	const FuNvtTsFlashInfo *match_finfo;
	guint8 flash_prog_data_cmd;
	guint8 flash_read_data_cmd;
	guint8 flash_read_pem_byte_len;
	guint8 flash_read_dummy_byte_len;
};

static void
fu_nvt_ts_device_fw_bin_clear(FuNvtTsFwBin *fwb)
{
	g_clear_pointer(&fwb->bin_data, g_free);
	fwb->bin_size = 0;
}

static gboolean
fu_nvt_ts_device_hid_read_dev(FuNvtTsDevice *self,
			      guint32 addr,
			      guint8 *data,
			      gsize len,
			      GError **error)
{
	int32_t ret;
	g_autofree guint8 *buf_get = NULL;
	g_autoptr(FuStructNvtTsHidReadReq) st_req = NULL;

	if (len == 0) {
		g_warning("len must be > 0");
		if (error != NULL && *error == NULL)
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "len must be > 0");
		return FALSE;
	}

	g_debug("read addr=0x%08x len=%zu", addr, len);

	/* set feature */
	st_req = fu_struct_nvt_ts_hid_read_req_new();
	fu_struct_nvt_ts_hid_read_req_set_i2c_hid_eng_report_id(st_req, NVT_TS_REPORT_ID);
	fu_struct_nvt_ts_hid_read_req_set_write_len(st_req, 0x000B);
	fu_struct_nvt_ts_hid_read_req_set_i2c_eng_addr_0(
	    st_req,
	    (guint8)((self->mmap->hid_i2c_eng_addr >> 0) & 0xFF));
	fu_struct_nvt_ts_hid_read_req_set_i2c_eng_addr_1(
	    st_req,
	    (guint8)((self->mmap->hid_i2c_eng_addr >> 8) & 0xFF));
	fu_struct_nvt_ts_hid_read_req_set_i2c_eng_addr_2(
	    st_req,
	    (guint8)((self->mmap->hid_i2c_eng_addr >> 16) & 0xFF));
	fu_struct_nvt_ts_hid_read_req_set_target_addr_0(st_req, (guint8)((addr >> 0) & 0xFF));
	fu_struct_nvt_ts_hid_read_req_set_target_addr_1(st_req, (guint8)((addr >> 8) & 0xFF));
	fu_struct_nvt_ts_hid_read_req_set_target_addr_2(st_req, (guint8)((addr >> 16) & 0xFF));
	fu_struct_nvt_ts_hid_read_req_set_len(st_req, (guint16)(len + 3));

	ret = fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					   st_req->buf->data,
					   st_req->buf->len,
					   FU_IOCTL_FLAG_RETRY,
					   error);
	if (!ret) {
		g_warning("set feature failed");
		if (error != NULL && *error == NULL)
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_READ,
					    "hid set_feature failed");
		return FALSE;
	}

	/* get feature */
	buf_get = g_new0(guint8, len + 1);
	buf_get[0] = NVT_TS_REPORT_ID;

	ret = fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(self),
					   buf_get,
					   len + 1,
					   FU_IOCTL_FLAG_RETRY,
					   error);
	if (!ret) {
		g_warning("get feature failed");
		if (error != NULL && *error == NULL)
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_READ,
					    "hid get_feature failed");
		return FALSE;
	}

	if (!fu_memcpy_safe(data, len, 0, buf_get, len + 1, 1, len, error)) {
		g_warning("copying feature data failed");
		if (error != NULL && *error == NULL)
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "copying feature data failed");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_nvt_ts_device_hid_write_dev(FuNvtTsDevice *self,
			       guint32 addr,
			       guint8 *data,
			       gsize len,
			       GError **error)
{
	int32_t ret;
	gsize write_len, report_len;
	g_autofree guint8 *buf_set = NULL;
	g_autoptr(FuStructNvtTsHidWriteHdr) st_hdr = NULL;

	if (len == 0) {
		g_warning("len must be > 0");
		if (error != NULL && *error == NULL)
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "len must be > 0");
		return FALSE;
	}

	g_debug("write addr=0x%08x len=%zu, data:", addr, len);
	fu_dump_raw(G_LOG_DOMAIN, "write-data", data, len);

	write_len = len + 5;

	report_len = write_len + 1;

	buf_set = g_new0(guint8, report_len);

	st_hdr = fu_struct_nvt_ts_hid_write_hdr_new();
	fu_struct_nvt_ts_hid_write_hdr_set_i2c_hid_eng_report_id(st_hdr, NVT_TS_REPORT_ID);
	fu_struct_nvt_ts_hid_write_hdr_set_write_len(st_hdr, (guint16)write_len);
	fu_struct_nvt_ts_hid_write_hdr_set_target_addr_0(st_hdr, (guint8)((addr >> 0) & 0xFF));
	fu_struct_nvt_ts_hid_write_hdr_set_target_addr_1(st_hdr, (guint8)((addr >> 8) & 0xFF));
	fu_struct_nvt_ts_hid_write_hdr_set_target_addr_2(st_hdr, (guint8)((addr >> 16) & 0xFF));

	if (!fu_memcpy_safe(buf_set,
			    report_len,
			    0,
			    st_hdr->buf->data,
			    st_hdr->buf->len,
			    0,
			    st_hdr->buf->len,
			    error)) {
		g_warning("copying write header failed");
		return FALSE;
	}

	if (!fu_memcpy_safe(buf_set, report_len, 6, data, len, 0, len, error)) {
		g_warning("copying write buffer failed");
		return FALSE;
	}

	ret = fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					   buf_set,
					   report_len,
					   FU_IOCTL_FLAG_RETRY,
					   error);
	if (!ret) {
		g_warning("set feature failed");
		if (error != NULL && *error == NULL)
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_WRITE,
					    "hid set_feature failed");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_nvt_ts_device_hid_read(FuNvtTsDevice *self,
			  guint32 addr,
			  guint8 *data,
			  gsize len,
			  GError **error)
{
	return fu_nvt_ts_device_hid_read_dev(self, addr, data, len, error);
}

static gboolean
fu_nvt_ts_device_hid_write(FuNvtTsDevice *self,
			   guint32 addr,
			   guint8 *data,
			   gsize len,
			   GError **error)
{
	return fu_nvt_ts_device_hid_write_dev(self, addr, data, len, error);
}

static gboolean
fu_nvt_ts_device_write_reg_bits(FuNvtTsDevice *self, FuNvtTsReg reg, guint8 val, GError **error)
{
	guint8 mask = 0, shift = 0, temp = 0;
	guint8 buf[8] = {0};
	guint32 addr = 0;
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
	if (!fu_nvt_ts_device_hid_read(self, addr, buf, 1, error)) {
		g_warning("fu_nvt_ts_device_hid_read failed");
		return FALSE;
	}
	temp = buf[0] & (~mask);
	temp |= ((val << shift) & mask);
	buf[0] = temp;
	if (!fu_nvt_ts_device_hid_write(self, addr, buf, 1, error)) {
		g_warning("fu_nvt_ts_device_hid_write failed");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_nvt_ts_device_find_fw_bin_end_flag(const guint8 *base,
				      guint32 size,
				      guint32 *flag_offset,
				      guint32 *delta_out)
{
	const guint16 step = 0x1000;
	const char *expect = HID_FW_BIN_END_NAME_FULL;
	guint32 delta = 0;
	char end_char[BIN_END_FLAG_LEN_MAX] = {0};

	if (base == NULL || size < BIN_END_FLAG_LEN_MAX)
		return FALSE;

	for (delta = 0; size >= BIN_END_FLAG_LEN_MAX + delta; delta += step) {
		guint32 offset = size - delta - BIN_END_FLAG_LEN_MAX;

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
				return FALSE;
			}
		}
		/* we now check "NVT" only */
		if (memcmp(end_char + 1, expect, BIN_END_FLAG_LEN_FULL) == 0) {
			if (flag_offset != NULL)
				*flag_offset = offset;
			if (delta_out != NULL)
				*delta_out = delta;
			return TRUE;
		}

		if (size < BIN_END_FLAG_LEN_MAX + delta + step)
			break;
	}

	return FALSE;
}

typedef struct {
	guint32 addr;
	guint8 *buf;
	guint8 flash_cmd;
} FuNvtTsCmdIssueCtx;

static gboolean
fu_nvt_ts_device_wait_cmd_issue_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuNvtTsDevice *self = FU_NVT_TS_DEVICE(device);
	FuNvtTsCmdIssueCtx *ctx = (FuNvtTsCmdIssueCtx *)user_data;

	if (!fu_nvt_ts_device_hid_read(self, ctx->addr, ctx->buf, 1, error))
		return FALSE;
	if (ctx->buf[0] == 0x00)
		return TRUE;
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_BUSY,
		    "write GCM cmd 0x%02X not ready",
		    ctx->flash_cmd);
	return FALSE;
}

static gboolean
fu_nvt_ts_device_retry_busy_cb(FuDevice *device, gpointer user_data, GError **error)
{
	/* prevent excessive log print in busy wait */
	return TRUE;
}

static gboolean
fu_nvt_ts_device_gcm_xfer(FuNvtTsDevice *self, FuNvtTsGcmXfer *xfer, GError **error)
{
	g_autofree guint8 *buf = NULL;
	guint32 flash_cmd_addr = 0, flash_cmd_issue_addr = 0;
	guint32 rw_flash_data_addr = 0, tmp_addr = 0;
	int32_t tmp_len = 0, i = 0, transfer_len = 0;
	int32_t total_buf_size = 0, write_len = 0;

	flash_cmd_addr = self->mmap->flash_cmd_addr;
	flash_cmd_issue_addr = self->mmap->flash_cmd_issue_addr;
	rw_flash_data_addr = self->mmap->rw_flash_data_addr;

	transfer_len = NVT_TRANSFER_LEN;

	total_buf_size = 64 + xfer->tx_len + xfer->rx_len;
	buf = g_malloc0(total_buf_size);
	if (buf == NULL) {
		g_warning("No memory for %d bytes", total_buf_size);
		if (error != NULL && *error == NULL)
			g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "no memory");
		return FALSE;
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
					if (error != NULL && *error == NULL)
						g_set_error_literal(error,
								    FWUPD_ERROR,
								    FWUPD_ERROR_INTERNAL,
								    "copying tx buffer failed");
					return FALSE;
				}
			}
			if (!fu_nvt_ts_device_hid_write(self, tmp_addr, buf, tmp_len, error)) {
				g_warning("Write tx data error");
				return FALSE;
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
	if (!fu_nvt_ts_device_hid_write(self, flash_cmd_addr, buf, 12, error)) {
		g_warning("Write enter GCM error");
		return FALSE;
	}

	{
		FuNvtTsCmdIssueCtx ctx = {0};
		ctx.addr = flash_cmd_issue_addr;
		ctx.buf = buf;
		ctx.flash_cmd = xfer->flash_cmd;
		if (!fu_device_retry_full(FU_DEVICE(self),
					  fu_nvt_ts_device_wait_cmd_issue_cb,
					  2000,
					  1,
					  &ctx,
					  error)) {
			g_warning("write GCM cmd 0x%02X failed", xfer->flash_cmd);
			return FALSE;
		}
	}

	if ((xfer->rx_len > 0) && xfer->rx_buf != NULL) {
		memset(buf, 0, xfer->rx_len);
		for (i = 0; i < xfer->rx_len; i += transfer_len) {
			tmp_addr = rw_flash_data_addr + i;
			tmp_len = MIN(xfer->rx_len - i, transfer_len);
			if (!fu_nvt_ts_device_hid_read(self, tmp_addr, buf, tmp_len, error)) {
				g_warning("Read rx data fail error");
				return FALSE;
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
					if (error != NULL && *error == NULL)
						g_set_error_literal(error,
								    FWUPD_ERROR,
								    FWUPD_ERROR_INTERNAL,
								    "copying rx buffer failed");
					return FALSE;
				}
			}
		}
	}

	return TRUE;
}

static gboolean
fu_nvt_ts_device_write_enable_gcm(FuNvtTsDevice *self, GError **error)
{
	FuNvtTsGcmXfer xfer = {0};

	memset(&xfer, 0, sizeof(FuNvtTsGcmXfer));
	xfer.flash_cmd = 0x06;
	if (!fu_nvt_ts_device_gcm_xfer(self, &xfer, error)) {
		g_warning("Write Enable failed");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_nvt_ts_device_write_status_gcm(FuNvtTsDevice *self, guint8 status, GError **error)
{
	guint8 sr1 = 0;
	FuNvtTsFlashWrsrMethod wrsr_method = 0;
	FuNvtTsGcmXfer xfer = {0};

	memset(&xfer, 0, sizeof(FuNvtTsGcmXfer));
	if (fu_device_has_private_flag(FU_DEVICE(self),
				       FU_NVT_TS_DEVICE_FLAG_SKIP_STATUS_REGISTER_CONTROL) &&
	    self->match_finfo->mid == FLASH_MFR_UNKNOWN) {
		g_info("unknown flash for flash table skip status register control rdsr");
		xfer.flash_cmd = 0x01;
		xfer.flash_addr = status << 16;
		xfer.flash_addr_len = 1;
	} else {
		wrsr_method = self->match_finfo->wrsr_method;
		if (wrsr_method == WRSR_01H1BYTE) {
			xfer.flash_cmd = 0x01;
			xfer.flash_addr = status << 16;
			xfer.flash_addr_len = 1;
		} else if (wrsr_method == WRSR_01H2BYTE) {
			xfer.flash_cmd = self->match_finfo->rdsr1_cmd;
			xfer.rx_len = 1;
			xfer.rx_buf = &sr1;
			if (!fu_nvt_ts_device_gcm_xfer(self, &xfer, error)) {
				g_warning("Read Status Register-1 fail!!");
				return FALSE;
			}
			g_debug("read status register-1 ok, sr1=0x%02X", sr1);

			memset(&xfer, 0, sizeof(FuNvtTsGcmXfer));
			xfer.flash_cmd = 0x01;
			xfer.flash_addr = (status << 16) | (sr1 << 8);
			xfer.flash_addr_len = 2;
		} else {
			g_warning("Unknown or not support write status register method(%u)!",
				  wrsr_method);
			if (error != NULL && *error == NULL)
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_NOT_SUPPORTED,
						    "write status register method not supported");
			return FALSE;
		}
	}
	if (!fu_nvt_ts_device_gcm_xfer(self, &xfer, error)) {
		g_warning("Write Status GCM fail");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_nvt_ts_device_read_status_gcm(FuNvtTsDevice *self, guint8 *status, GError **error)
{
	FuNvtTsGcmXfer xfer = {0};

	memset(&xfer, 0, sizeof(FuNvtTsGcmXfer));
	xfer.flash_cmd = 0x05;
	xfer.rx_len = 1;
	xfer.rx_buf = status;
	if (!fu_nvt_ts_device_gcm_xfer(self, &xfer, error)) {
		g_warning("Read Status GCM fail");
		return FALSE;
	}

	return TRUE;
}

typedef struct {
	guint8 *status;
} FuNvtTsStatusReadyCtx;

static gboolean
fu_nvt_ts_device_wait_status_ready_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuNvtTsDevice *self = FU_NVT_TS_DEVICE(device);
	FuNvtTsStatusReadyCtx *ctx = (FuNvtTsStatusReadyCtx *)user_data;

	if (!fu_nvt_ts_device_read_status_gcm(self, ctx->status, error))
		return FALSE;
	if ((*ctx->status & 0x03) == 0x00)
		return TRUE;
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "device busy");
	return FALSE;
}

typedef struct {
	gboolean enable;
} FuNvtTsSwitchGcmCtx;

static gboolean
fu_nvt_ts_device_switch_gcm_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuNvtTsDevice *self = FU_NVT_TS_DEVICE(device);
	FuNvtTsSwitchGcmCtx *ctx = (FuNvtTsSwitchGcmCtx *)user_data;
	guint8 buf[3] = {0};

	if (ctx->enable) {
		buf[0] = 0x55;
		buf[1] = 0xFF;
		buf[2] = 0xAA;
	} else {
		buf[0] = 0xAA;
		buf[1] = 0x55;
		buf[2] = 0xFF;
	}
	if (!fu_nvt_ts_device_hid_write(self, self->mmap->gcm_code_addr, buf, 3, error))
		return FALSE;
	if (!fu_nvt_ts_device_hid_read(self, self->mmap->gcm_flag_addr, buf, 1, error))
		return FALSE;
	if (ctx->enable) {
		if ((buf[0] & 0x01) == 0x01)
			return TRUE;
	} else {
		if ((buf[0] & 0x01) == 0x00)
			return TRUE;
	}
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "gcm enable/disable not ready");
	return FALSE;
}

typedef struct {
	guint8 state;
	guint8 last_state;
} FuNvtTsResetStateCtx;

static gboolean
fu_nvt_ts_device_wait_reset_state_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuNvtTsDevice *self = FU_NVT_TS_DEVICE(device);
	FuNvtTsResetStateCtx *ctx = (FuNvtTsResetStateCtx *)user_data;
	guint8 buf[1] = {0};

	if (!fu_nvt_ts_device_hid_read(self, self->mmap->event_buf_reset_state_addr, buf, 1, error))
		return FALSE;
	ctx->last_state = buf[0];
	if ((buf[0] >= ctx->state) && (buf[0] <= RESET_STATE_MAX))
		return TRUE;
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "reset state not ready");
	return FALSE;
}

typedef struct {
	guint8 buf[2];
} FuNvtTsFwVerCtx;

static gboolean
fu_nvt_ts_device_get_fw_ver_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuNvtTsDevice *self = FU_NVT_TS_DEVICE(device);
	FuNvtTsFwVerCtx *ctx = (FuNvtTsFwVerCtx *)user_data;

	if (!fu_nvt_ts_device_hid_read(self, self->mmap->event_map_fwinfo_addr, ctx->buf, 2, error))
		return FALSE;
	if ((guint8)(ctx->buf[0] + ctx->buf[1]) == 0xFF)
		return TRUE;
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "fw info not ready");
	return FALSE;
}

typedef struct {
	guint32 flash_addr;
	guint16 len;
	guint8 *out;
} FuNvtTsReadFlashCtx;

static gboolean
fu_nvt_ts_device_read_flash_data_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuNvtTsDevice *self = FU_NVT_TS_DEVICE(device);
	FuNvtTsReadFlashCtx *ctx = (FuNvtTsReadFlashCtx *)user_data;
	FuNvtTsGcmXfer xfer = {0};
	guint8 buf[2] = {0};
	guint16 rd_checksum = 0;
	guint16 calc = 0;
	guint16 i = 0;

	calc += (ctx->flash_addr >> 16) & 0xFF;
	calc += (ctx->flash_addr >> 8) & 0xFF;
	calc += (ctx->flash_addr >> 0) & 0xFF;
	calc += (ctx->len >> 8) & 0xFF;
	calc += (ctx->len >> 0) & 0xFF;

	memset(&xfer, 0, sizeof(xfer));
	xfer.flash_cmd = self->flash_read_data_cmd;
	xfer.flash_addr = ctx->flash_addr;
	xfer.flash_addr_len = 3;
	xfer.pem_byte_len = self->flash_read_pem_byte_len;
	xfer.dummy_byte_len = self->flash_read_dummy_byte_len;
	xfer.rx_buf = ctx->out;
	xfer.rx_len = ctx->len;

	if (!fu_nvt_ts_device_gcm_xfer(self, &xfer, error))
		return FALSE;
	if (!fu_nvt_ts_device_hid_read(self, self->mmap->read_flash_checksum_addr, buf, 2, error))
		return FALSE;

	rd_checksum = (guint16)(buf[1] << 8 | buf[0]);
	for (i = 0; i < ctx->len; i++)
		calc += ctx->out[i];
	/* 0xFFFF - sum + 1 */
	calc = 65535 - calc + 1;

	if (rd_checksum == calc)
		return TRUE;

	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "flash read checksum mismatch");
	return FALSE;
}

typedef struct {
	guint32 flash_cksum_status_addr;
	guint8 *buf;
	gboolean allow_retry;
	gboolean *retry_out;
	gboolean checksum_error;
} FuNvtTsChecksumCtx;

static gboolean
fu_nvt_ts_device_check_flash_checksum_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuNvtTsDevice *self = FU_NVT_TS_DEVICE(device);
	FuNvtTsChecksumCtx *ctx = (FuNvtTsChecksumCtx *)user_data;

	ctx->buf[0] = 0x00;
	if (!fu_nvt_ts_device_hid_read(self, ctx->flash_cksum_status_addr, ctx->buf, 1, error))
		return FALSE;
	if (ctx->buf[0] == 0xAA)
		return TRUE;
	if (ctx->buf[0] == 0xEA) {
		if (ctx->allow_retry) {
			*ctx->retry_out = TRUE;
			return TRUE;
		}
		ctx->checksum_error = TRUE;
		return TRUE;
	}
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "checksum not ready");
	return FALSE;
}

static gboolean
fu_nvt_ts_device_sector_erase_gcm(FuNvtTsDevice *self, guint32 flash_addr, GError **error)
{
	FuNvtTsGcmXfer xfer = {0};

	memset(&xfer, 0, sizeof(FuNvtTsGcmXfer));
	xfer.flash_cmd = 0x20;
	xfer.flash_addr = flash_addr;
	xfer.flash_addr_len = 3;
	if (!fu_nvt_ts_device_gcm_xfer(self, &xfer, error)) {
		g_warning("Sector Erase GCM fail");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_nvt_ts_device_page_program_gcm(FuNvtTsDevice *self,
				  guint32 flash_addr,
				  guint16 data_len,
				  guint8 *data,
				  GError **error)
{
	FuNvtTsGcmXfer xfer = {0};
	guint16 checksum = 0;
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
	memset(&xfer, 0, sizeof(FuNvtTsGcmXfer));
	xfer.flash_cmd = self->flash_prog_data_cmd;
	xfer.flash_addr = flash_addr;
	xfer.flash_addr_len = 3;
	xfer.tx_buf = data;
	xfer.tx_len = data_len;
	xfer.flash_checksum = checksum & 0xFFFF;
	if (!fu_nvt_ts_device_gcm_xfer(self, &xfer, error)) {
		g_warning("Page Program GCM fail");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_nvt_ts_device_get_checksum_gcm(FuNvtTsDevice *self,
				  guint32 flash_addr,
				  guint32 data_len,
				  guint16 *checksum,
				  GError **error)
{
	FuNvtTsGcmXfer xfer = {0};
	guint8 buf[2] = {0};

	memset(&xfer, 0, sizeof(FuNvtTsGcmXfer));
	xfer.flash_cmd = self->flash_read_data_cmd;
	xfer.flash_addr = flash_addr;
	xfer.flash_addr_len = 3;
	xfer.pem_byte_len = self->flash_read_pem_byte_len;
	xfer.dummy_byte_len = self->flash_read_dummy_byte_len;
	xfer.rx_len = data_len;
	if (!fu_nvt_ts_device_gcm_xfer(self, &xfer, error)) {
		g_warning("Get Checksum GCM fail");
		return FALSE;
	}

	buf[0] = 0x00;
	buf[1] = 0x00;
	if (!fu_nvt_ts_device_hid_read(self, self->mmap->read_flash_checksum_addr, buf, 2, error)) {
		g_warning("Get checksum error");
		return FALSE;
	}
	*checksum = (buf[1] << 8) | buf[0];

	return TRUE;
}

static gboolean
fu_nvt_ts_device_switch_gcm(FuNvtTsDevice *self, guint8 enable, GError **error)
{
	FuNvtTsSwitchGcmCtx ctx = {0};

	ctx.enable = enable;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_nvt_ts_device_switch_gcm_cb,
				  3,
				  0,
				  &ctx,
				  error)) {
		if (enable)
			g_warning("enable gcm failed");
		else
			g_warning("disable gcm failed");
		if (error != NULL && *error == NULL)
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "gcm enable/disable failed");
		return FALSE;
	}

	if (enable)
		g_info("enable gcm ok");
	else
		g_info("disable gcm ok");

	return TRUE;
}

static gboolean
fu_nvt_ts_device_resume_pd_gcm(FuNvtTsDevice *self, GError **error)
{
	FuNvtTsGcmXfer xfer = {0};

	memset(&xfer, 0, sizeof(FuNvtTsGcmXfer));
	xfer.flash_cmd = 0xAB;
	if (!fu_nvt_ts_device_gcm_xfer(self, &xfer, error)) {
		g_warning("Resume PD failed");
		return FALSE;
	}

	g_debug("resume pd ok");
	return TRUE;
}

static gboolean
fu_nvt_ts_device_erase_flash_gcm(FuNvtTsDevice *self, FuNvtTsFwBin *fwb, GError **error)
{
	FuNvtTsFlashMfr mid = 0;
	guint8 status = 0;
	int32_t count = 0;
	int32_t i = 0;
	int32_t flash_address = 0;
	int32_t erase_length = 0;
	int32_t start_sector = 0;
	const FuNvtTsFlashQebInfo *qeb_info_p = NULL;

	if (fwb->flash_start_addr % FLASH_SECTOR_SIZE) {
		g_warning("flash_start_addr should be n*%d", FLASH_SECTOR_SIZE);
		if (error != NULL && *error == NULL)
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "flash_start_addr is not sector-aligned");
		return FALSE;
	}

	start_sector = fwb->flash_start_addr / FLASH_SECTOR_SIZE;
	erase_length = fwb->bin_size;
	if (erase_length < 0) {
		g_warning("Wrong erase_length = %d", erase_length);
		if (error != NULL && *error == NULL)
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "erase length invalid");
		return FALSE;
	}

	/* write enable */
	if (!fu_nvt_ts_device_write_enable_gcm(self, error)) {
		g_warning("Write Enable error");
		return FALSE;
	}

	if (fu_device_has_private_flag(FU_DEVICE(self),
				       FU_NVT_TS_DEVICE_FLAG_SKIP_STATUS_REGISTER_CONTROL) &&
	    self->match_finfo->mid == FLASH_MFR_UNKNOWN) {
		g_info("unknown flash for flash table skip status register control qeb");
		if (!fu_nvt_ts_device_write_status_gcm(self, status, error)) {
			g_warning("write status register error");
			return FALSE;
		}
	} else {
		mid = self->match_finfo->mid;
		qeb_info_p = &self->match_finfo->qeb_info;
		if ((mid != FLASH_MFR_UNKNOWN) && (qeb_info_p->qeb_pos != QEB_POS_UNKNOWN)) {
			/* check if QE bit is in status register byte 1, if yes set it back to 1 */
			if (qeb_info_p->qeb_pos == QEB_POS_SR_1B) {
				status = (0x01 << qeb_info_p->qeb_order);
			} else {
				status = 0;
			}
			/* write status register */
			if (!fu_nvt_ts_device_write_status_gcm(self, status, error)) {
				g_warning("Write Status Register error");
				return FALSE;
			}
			g_debug("write status register byte 0x%02X ok", status);
			fu_device_sleep(FU_DEVICE(self), 1);
		}
	}

	/* read status */
	{
		FuNvtTsStatusReadyCtx status_ctx = {0};
		status_ctx.status = &status;
		if (!fu_device_retry_full(FU_DEVICE(self),
					  fu_nvt_ts_device_wait_status_ready_cb,
					  100,
					  5,
					  &status_ctx,
					  error)) {
			g_warning("read status failed, status = 0x%02X", status);
			return FALSE;
		}
		g_debug("read status register ok, status = 0x%02X", status);
	}

	if (erase_length % FLASH_SECTOR_SIZE)
		count = erase_length / FLASH_SECTOR_SIZE + start_sector + 1;
	else
		count = erase_length / FLASH_SECTOR_SIZE + start_sector;

	for (i = start_sector; i < count; i++) {
		/* write enable */
		if (!fu_nvt_ts_device_write_enable_gcm(self, error)) {
			g_warning("Write enable error, page at = %d", i * FLASH_SECTOR_SIZE);
			return FALSE;
		}

		flash_address = i * FLASH_SECTOR_SIZE;

		/* sector erase */
		if (!fu_nvt_ts_device_sector_erase_gcm(self, flash_address, error)) {
			g_warning("Sector erase error, page at = %d", i * FLASH_SECTOR_SIZE);
			return FALSE;
		}
		fu_device_sleep(FU_DEVICE(self), 25);

		{
			FuNvtTsStatusReadyCtx status_ctx = {0};
			status_ctx.status = &status;
			if (!fu_device_retry_full(FU_DEVICE(self),
						  fu_nvt_ts_device_wait_status_ready_cb,
						  100,
						  5,
						  &status_ctx,
						  error)) {
				g_warning("wait sector erase timeout");
				return FALSE;
			}
		}
	}

	g_info("erase ok");

	return TRUE;
}

static gboolean
fu_nvt_ts_device_ensure_prog_flash_method(FuNvtTsDevice *self, GError **error)
{
	FuNvtTsFlashProgMethod prog_method = 0;
	guint8 pp4io_en = 0;
	guint8 q_wr_cmd = 0x00;
	guint8 bld_rd_addr_sel = 0;
	guint8 buf[4] = {0};

	prog_method = self->match_finfo->prog_method;
	switch (prog_method) {
	case SPP_0x02:
		self->flash_prog_data_cmd = 0x02;
		pp4io_en = 0;
		q_wr_cmd = 0x00; /* not 0x02, must 0x00! */
		break;
	case QPP_0x32:
		self->flash_prog_data_cmd = 0x32;
		pp4io_en = 1;
		q_wr_cmd = 0x32;
		bld_rd_addr_sel = 0;
		break;
	case QPP_0x38:
		self->flash_prog_data_cmd = 0x38;
		pp4io_en = 1;
		q_wr_cmd = 0x38;
		bld_rd_addr_sel = 1;
		break;
	default:
		g_warning("flash program method %u not support!", prog_method);
		if (error != NULL && *error == NULL)
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "flash program method not supported");
		return FALSE;
	}
	g_debug("prog_method=%u, self->flash_prog_data_cmd=0x%02X",
		prog_method,
		self->flash_prog_data_cmd);
	g_debug("pp4io_en=%d, q_wr_cmd=0x%02X, bld_rd_addr_sel=0x%02X",
		pp4io_en,
		q_wr_cmd,
		bld_rd_addr_sel);

	if (self->mmap->pp4io_en_reg.addr) {
		if (!fu_nvt_ts_device_write_reg_bits(self,
						     self->mmap->pp4io_en_reg,
						     pp4io_en,
						     error)) {
			g_warning("set pp4io_en_reg failed");
			return FALSE;
		} else {
			g_debug("set pp4io_en_reg=%d done", pp4io_en);
		}
	}
	if (self->mmap->q_wr_cmd_addr) {
		buf[0] = q_wr_cmd;
		if (!fu_nvt_ts_device_hid_write(self, self->mmap->q_wr_cmd_addr, buf, 1, error)) {
			g_warning("set q_wr_cmd_addr failed");
			return FALSE;
		} else {
			g_debug("set Q_WR_CMD_ADDR=0x%02X done", q_wr_cmd);
		}
	}
	if (pp4io_en) {
		if (self->mmap->bld_rd_addr_sel_reg.addr) {
			if (!fu_nvt_ts_device_write_reg_bits(self,
							     self->mmap->bld_rd_addr_sel_reg,
							     bld_rd_addr_sel,
							     error)) {
				g_warning("set bld_rd_addr_sel_reg failed");
				return FALSE;
			} else {
				g_debug("set bld_rd_addr_sel_reg=%d done", bld_rd_addr_sel);
			}
		}
	}

	return TRUE;
}

static gboolean
fu_nvt_ts_device_check_flash_checksum(FuNvtTsDevice *self,
				      guint32 flash_cksum_status_addr,
				      guint8 *buf,
				      gboolean allow_retry,
				      gboolean *retry_out,
				      GError **error);

static gboolean
fu_nvt_ts_device_write_flash_gcm(FuNvtTsDevice *self,
				 FuNvtTsFwBin *fwb,
				 FuProgress *progress,
				 GError **error)
{
	guint8 page_program_retry = 0;
	guint8 buf[1] = {0};
	guint32 flash_address = 0;
	guint32 flash_cksum_status_addr = self->mmap->flash_cksum_status_addr;
	guint32 step = 10, pre = 0, show = 0;
	guint32 offset = 0;
	int32_t i = 0;
	int32_t count = 0;
	guint8 status = 0;

	if (!fu_nvt_ts_device_ensure_prog_flash_method(self, error))
		return FALSE;

	count = fwb->bin_size / FLASH_PAGE_SIZE;
	if (fwb->bin_size % FLASH_PAGE_SIZE)
		count++;

	if (progress != NULL)
		fu_progress_set_id(progress, G_STRLOC);
	for (i = 0; i < count; i++) {
		gboolean checksum_ok = FALSE;
		offset = i * FLASH_PAGE_SIZE;
		flash_address = offset + fwb->flash_start_addr;
		page_program_retry = 0;

		while (!checksum_ok) {
			/* write enable */
			if (!fu_nvt_ts_device_write_enable_gcm(self, error)) {
				g_warning("write enable error");
				return FALSE;
			}
			/* write page: FLASH_PAGE_SIZE bytes */
			if (!fu_nvt_ts_device_page_program_gcm(
				self,
				flash_address,
				MIN(fwb->bin_size - offset, FLASH_PAGE_SIZE),
				&fwb->bin_data[offset],
				error)) {
				g_warning("page program error, i= %d", i);
				return FALSE;
			}

			/* check flash checksum status */
			{
				gboolean retry_page = FALSE;
				if (!fu_nvt_ts_device_check_flash_checksum(self,
									   flash_cksum_status_addr,
									   buf,
									   page_program_retry < 1,
									   &retry_page,
									   error))
					return FALSE;
				if (retry_page) {
					page_program_retry++;
					continue;
				}
				checksum_ok = TRUE;
			}
		}

		/* read status */
		{
			FuNvtTsStatusReadyCtx status_ctx = {0};
			status_ctx.status = &status;
			if (!fu_device_retry_full(FU_DEVICE(self),
						  fu_nvt_ts_device_wait_status_ready_cb,
						  200,
						  1,
						  &status_ctx,
						  error)) {
				g_warning("wait page program timeout");
				return FALSE;
			}
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

	return TRUE;
}

static gboolean
fu_nvt_ts_device_check_flash_checksum(FuNvtTsDevice *self,
				      guint32 flash_cksum_status_addr,
				      guint8 *buf,
				      gboolean allow_retry,
				      gboolean *retry_out,
				      GError **error)
{
	FuNvtTsChecksumCtx ctx = {0};

	*retry_out = FALSE;
	ctx.flash_cksum_status_addr = flash_cksum_status_addr;
	ctx.buf = buf;
	ctx.allow_retry = allow_retry;
	ctx.retry_out = retry_out;
	ctx.checksum_error = FALSE;

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_nvt_ts_device_check_flash_checksum_cb,
				  20,
				  1,
				  &ctx,
				  error))
		return FALSE;

	if (ctx.checksum_error) {
		g_warning("check flash checksum status error");
		if (error != NULL && *error == NULL)
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_WRITE,
					    "check flash checksum status error");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_nvt_ts_device_ensure_read_flash_method(FuNvtTsDevice *self, GError **error)
{
	guint8 bld_rd_io_sel = 0;
	guint8 bld_rd_addr_sel = 0;
	FuNvtTsFlashReadMethod rd_method = 0;

	bld_rd_io_sel = 0;
	bld_rd_addr_sel = 0;
	rd_method = self->match_finfo->rd_method;
	switch (rd_method) {
	case SISO_0x03:
		self->flash_read_data_cmd = 0x03;
		self->flash_read_pem_byte_len = 0;
		self->flash_read_dummy_byte_len = 0;
		bld_rd_io_sel = 0;
		bld_rd_addr_sel = 0;
		break;
	case SISO_0x0B:
		self->flash_read_data_cmd = 0x0B;
		self->flash_read_pem_byte_len = 0;
		self->flash_read_dummy_byte_len = 1;
		bld_rd_io_sel = 0;
		bld_rd_addr_sel = 0;
		break;
	case SIQO_0x6B:
		self->flash_read_data_cmd = 0x6B;
		self->flash_read_pem_byte_len = 0;
		self->flash_read_dummy_byte_len = 4;
		bld_rd_io_sel = 2;
		bld_rd_addr_sel = 0;
		break;
	case QIQO_0xEB:
		self->flash_read_data_cmd = 0xEB;
		self->flash_read_pem_byte_len = 1;
		self->flash_read_dummy_byte_len = 2;
		bld_rd_io_sel = 2;
		bld_rd_addr_sel = 1;
		break;
	default:
		g_warning("flash read method %u not support!", rd_method);
		if (error != NULL && *error == NULL)
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "flash read method not supported");
		return FALSE;
	}
	g_debug("rd_method = %u, self->flash_read_data_cmd = 0x%02X",
		rd_method,
		self->flash_read_data_cmd);
	g_debug("self->flash_read_pem_byte_len = %d, self->flash_read_dummy_byte_len = %d",
		self->flash_read_pem_byte_len,
		self->flash_read_dummy_byte_len);
	g_debug("bld_rd_io_sel = %d, bld_rd_addr_sel = %d", bld_rd_io_sel, bld_rd_addr_sel);

	if (self->mmap->bld_rd_io_sel_reg.addr) {
		if (!fu_nvt_ts_device_write_reg_bits(self,
						     self->mmap->bld_rd_io_sel_reg,
						     bld_rd_io_sel,
						     error)) {
			g_warning("set bld_rd_io_sel_reg failed");
			return FALSE;
		} else {
			g_debug("set bld_rd_io_sel_reg=%d done", bld_rd_io_sel);
		}
	}
	if (self->mmap->bld_rd_addr_sel_reg.addr) {
		if (!fu_nvt_ts_device_write_reg_bits(self,
						     self->mmap->bld_rd_addr_sel_reg,
						     bld_rd_addr_sel,
						     error)) {
			g_warning("set bld_rd_addr_sel_reg failed");
			return FALSE;
		} else {
			g_debug("set bld_rd_addr_sel_reg=%d done", bld_rd_addr_sel);
		}
	}

	return TRUE;
}

static gboolean
fu_nvt_ts_device_verify_flash_gcm(FuNvtTsDevice *self, FuNvtTsFwBin *fwb, GError **error)
{
	guint16 write_checksum = 0;
	guint16 read_checksum = 0;
	guint32 flash_addr = 0;
	guint32 data_len = 0;
	guint32 total_sector_need_check = 0;
	guint32 offset = 0;
	guint32 i = 0;
	guint32 j = 0;

	if (!fu_nvt_ts_device_ensure_read_flash_method(self, error))
		return FALSE;

	total_sector_need_check = (fwb->bin_size + SIZE_4KB - 1) / SIZE_4KB;
	for (i = 0; i < total_sector_need_check; i++) {
		offset = i * SIZE_4KB;
		flash_addr = offset + fwb->flash_start_addr;
		data_len = MIN(fwb->bin_size - offset, SIZE_4KB);
		/* calculate write_checksum of each 4KB block */
		write_checksum = (flash_addr & 0xFF);
		write_checksum += ((flash_addr >> 8) & 0xFF);
		write_checksum += ((flash_addr >> 16) & 0xFF);
		write_checksum += ((data_len) & 0xFF);
		write_checksum += (((data_len) >> 8) & 0xFF);
		for (j = 0; j < data_len; j++)
			write_checksum += fwb->bin_data[offset + j];
		write_checksum = ~write_checksum + 1;

		if (!fu_nvt_ts_device_get_checksum_gcm(self,
						       flash_addr,
						       data_len,
						       &read_checksum,
						       error)) {
			g_warning("Get Checksum failed, i = %u", i);
			return FALSE;
		}
		if (write_checksum != read_checksum) {
			g_warning("Verify Failed, i = %u, write_checksum = 0x%04X, "
				  "read_checksum = 0x%04X",
				  i,
				  write_checksum,
				  read_checksum);
			return FALSE;
		}
	}

	g_info("verify ok");

	return TRUE;
}

static gboolean
fu_nvt_ts_device_find_match_flash_info(FuNvtTsDevice *self, GError **error)
{
	guint32 i = 0, total_info_items = 0;
	const FuNvtTsFlashInfo *finfo = NULL;

	total_info_items =
	    sizeof(fu_nvt_ts_flash_info_table) / sizeof(fu_nvt_ts_flash_info_table[0]);
	if (total_info_items == 0) {
		if (error != NULL && *error == NULL)
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "flash info table is empty");
		return FALSE;
	}
	for (i = 0; i < total_info_items; i++) {
		if (fu_nvt_ts_flash_info_table[i].mid == self->flash_mid) {
			/* mid of this flash info item match current flash's mid */
			if (fu_nvt_ts_flash_info_table[i].did == self->flash_did) {
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
	self->match_finfo = &fu_nvt_ts_flash_info_table[i];
	finfo = self->match_finfo;
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

	return TRUE;
}

static gboolean
fu_nvt_ts_device_read_flash_mid_did_gcm(FuNvtTsDevice *self, GError **error)
{
	guint8 buf[3] = {0};
	FuNvtTsGcmXfer xfer = {0};

	memset(&xfer, 0, sizeof(FuNvtTsGcmXfer));
	xfer.flash_cmd = 0x9F;
	xfer.rx_buf = buf;
	xfer.rx_len = 3;
	if (!fu_nvt_ts_device_gcm_xfer(self, &xfer, error)) {
		g_warning("read flash mid did gcm failed");
		return FALSE;
	}

	self->flash_mid = buf[0];
	self->flash_did = (buf[1] << 8) | buf[2];
	g_debug("flash mid = 0x%02X, did = 0x%04X", self->flash_mid, self->flash_did);
	if (!fu_nvt_ts_device_find_match_flash_info(self, error))
		return FALSE;
	g_debug("read mid did ok");
	return TRUE;
}

static gboolean
fu_nvt_ts_device_check_end_flag(FuNvtTsFwBin *fwb)
{
	const guint32 sz = fwb->bin_size;
	const guint8 *base = (const guint8 *)fwb->bin_data;
	char end_char[BIN_END_FLAG_LEN_MAX] = {0};
	guint32 new_sz = 0;
	guint32 flag_offset = 0;
	guint32 delta = 0;

	if (!fu_nvt_ts_device_find_fw_bin_end_flag(base, sz, &flag_offset, &delta)) {
		g_warning("binary end flag not found at end or at (-0x1000) steps (expected [%s]), "
			  "abort.",
			  HID_FW_BIN_END_NAME_FULL);
		return FALSE;
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
			return FALSE;
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
	return TRUE;
}

static gboolean
fu_nvt_ts_device_get_binary_and_flash_start_addr_from_blob(FuNvtTsDevice *self,
							   FuNvtTsFwBin *fwb,
							   const guint8 *data,
							   gsize size,
							   GError **error)
{
	if (data == NULL || size == 0) {
		g_warning("invalid firmware blob (data=%p size=0x%zX)", data, size);
		return FALSE;
	}

	fu_nvt_ts_device_fw_bin_clear(fwb);

	if (size > MAX_BIN_SIZE) {
		g_warning("firmware blob too large (0x%zX > 0x%X)", size, (guint32)MAX_BIN_SIZE);
		return FALSE;
	}

	fwb->bin_data = g_malloc(size);
	if (fwb->bin_data == NULL) {
		g_warning("malloc for firmware blob failed (size=0x%zX)", size);
		return FALSE;
	}
	{
		g_autoptr(GError) error_local = NULL;
		if (!fu_memcpy_safe(fwb->bin_data, size, 0, data, size, 0, size, &error_local)) {
			g_warning("copying firmware blob failed: %s",
				  error_local != NULL ? error_local->message : "unknown");
			return FALSE;
		}
	}
	fwb->bin_size = (guint32)size;

	/* check and trim according to end-flag if needed */
	if (!fu_nvt_ts_device_check_end_flag(fwb))
		return FALSE;

	if (self->fmap->flash_normal_fw_start_addr == 0) {
		g_warning("normal FW flash should not start from 0");
		return FALSE;
	}

	/* always use FLASH_NORMAL start (0x2000) */
	fwb->flash_start_addr = self->fmap->flash_normal_fw_start_addr;
	if (fwb->flash_start_addr < FLASH_SECTOR_SIZE) {
		g_warning("flash start addr too low: 0x%X", fwb->flash_start_addr);
		if (error != NULL && *error == NULL)
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "flash start addr too low");
		return FALSE;
	}

	/* drop leading header region so data starts at flash_start_addr */
	if (fwb->flash_start_addr > fwb->bin_size) {
		g_warning("firmware blob too small (size=0x%X, start=0x%X)",
			  fwb->bin_size,
			  fwb->flash_start_addr);
		if (error != NULL && *error == NULL)
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "firmware blob too small for flash start");
		return FALSE;
	}
	if (fwb->flash_start_addr > 0) {
		memmove(fwb->bin_data,
			fwb->bin_data + fwb->flash_start_addr,
			fwb->bin_size - fwb->flash_start_addr);
		fwb->bin_size -= fwb->flash_start_addr;
	}

	g_info("flashing starts from 0x%X", fwb->flash_start_addr);
	g_info("size of bin for update = 0x%05X", fwb->bin_size);
	g_info("flash range to write = 0x%X-0x%X",
	       fwb->flash_start_addr,
	       fwb->flash_start_addr + fwb->bin_size - 1);

	if (self->fmap->flash_max_size > 0 && fwb->bin_size > self->fmap->flash_max_size) {
		g_warning("flash size 0x%X exceeds max 0x%X",
			  fwb->bin_size,
			  self->fmap->flash_max_size);
		if (error != NULL && *error == NULL)
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "firmware image exceeds max flash size");
		return FALSE;
	}
	if (self->fmap->flash_max_size > 0) {
		guint32 flash_end = fwb->flash_start_addr + fwb->bin_size;
		guint32 flash_limit =
		    self->fmap->flash_normal_fw_start_addr + self->fmap->flash_max_size;
		if (flash_end > flash_limit) {
			g_warning("flash end 0x%X exceeds limit 0x%X",
				  flash_end - 1,
				  flash_limit - 1);
			if (error != NULL && *error == NULL)
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_FILE,
						    "firmware image exceeds flash limit");
			return FALSE;
		}
	}
	g_info("get binary from blob ok");

	return TRUE;
}

static gboolean
fu_nvt_ts_device_update_firmware(FuNvtTsDevice *self,
				 FuNvtTsFwBin *fwb,
				 const guint8 *data,
				 gsize size,
				 FuProgress *progress,
				 GError **error)
{
	g_info("enable gcm");
	if (!fu_nvt_ts_device_switch_gcm(self, 1, error))
		return FALSE;

	g_info("resume pd");
	if (!fu_nvt_ts_device_resume_pd_gcm(self, error))
		return FALSE;

	g_info("read flash id");
	if (!fu_nvt_ts_device_read_flash_mid_did_gcm(self, error))
		return FALSE;

	g_info("erase");
	if (!fu_nvt_ts_device_erase_flash_gcm(self, fwb, error))
		return FALSE;

	g_info("program");
	if (!fu_nvt_ts_device_write_flash_gcm(self, fwb, progress, error))
		return FALSE;

	g_info("verify");
	if (!fu_nvt_ts_device_verify_flash_gcm(self, fwb, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_nvt_ts_device_bootloader_reset(FuNvtTsDevice *self, GError **error)
{
	guint8 buf[1] = {0};

	buf[0] = 0x69;
	if (!fu_nvt_ts_device_hid_write(self, self->mmap->swrst_sif_addr, buf, 1, error))
		return FALSE;
	g_debug("0x69 to 0x%06X", self->mmap->swrst_sif_addr);
	fu_device_sleep(FU_DEVICE(self), 235);
	return TRUE;
}

static gboolean
fu_nvt_ts_device_sw_reset_and_idle(FuNvtTsDevice *self, GError **error)
{
	guint8 buf[1] = {0};

	buf[0] = 0xAA;
	if (!fu_nvt_ts_device_hid_write(self, self->mmap->swrst_sif_addr, buf, 1, error))
		return FALSE;
	g_debug("0xAA to 0x%06X", self->mmap->swrst_sif_addr);
	fu_device_sleep(FU_DEVICE(self), 50);
	return TRUE;
}

static gboolean
fu_nvt_ts_device_stop_crc_reboot(FuNvtTsDevice *self, GError **error)
{
	guint8 buf[1] = {0};
	guint8 retry = 20;

	g_debug("%s (0xA5 to 0x%06X) %d times", __func__, self->mmap->bld_spe_pups_addr, retry);
	while (retry--) {
		buf[0] = 0xA5;
		if (!fu_nvt_ts_device_hid_write(self, self->mmap->bld_spe_pups_addr, buf, 1, error))
			return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), 5);
	return TRUE;
}

static gboolean
fu_nvt_ts_device_update_firmware_reset(FuNvtTsDevice *self,
				       FuNvtTsFwBin *fwb,
				       const guint8 *data,
				       gsize size,
				       FuProgress *progress,
				       GError **error)
{
	if (!fu_nvt_ts_device_bootloader_reset(self, error))
		return FALSE;
	if (!fu_nvt_ts_device_sw_reset_and_idle(self, error))
		return FALSE;
	if (!fu_nvt_ts_device_stop_crc_reboot(self, error))
		return FALSE;

	if (!fu_nvt_ts_device_update_firmware(self, fwb, data, size, progress, error))
		return FALSE;

	(void)fu_nvt_ts_device_bootloader_reset(self, error);
	return TRUE;
}

G_DEFINE_TYPE(FuNvtTsDevice, fu_nvt_ts_device, FU_TYPE_HIDRAW_DEVICE)

static gboolean
fu_nvt_ts_device_probe(FuDevice *device, GError **error)
{
	const gchar *subsystem;

	g_info("device probe");

	subsystem = fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device));
	if (g_strcmp0(subsystem, "hidraw") != 0) {
		if (error != NULL && *error == NULL)
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "subsystem is not hidraw");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_nvt_ts_device_clear_fw_reset_state(FuNvtTsDevice *self, GError **error)
{
	guint8 buf[1] = {0};

	if (!fu_nvt_ts_device_hid_write(self,
					self->mmap->event_buf_reset_state_addr,
					buf,
					1,
					error))
		return FALSE;
	g_debug("0x00 to 0x%06X", self->mmap->event_buf_reset_state_addr);

	return TRUE;
}

static gboolean
fu_nvt_ts_device_check_fw_reset_state(FuNvtTsDevice *self, guint8 state, GError **error)
{
	FuNvtTsResetStateCtx ctx = {0};

	g_info("checking reset state from address 0x%06X for state 0x%02X",
	       self->mmap->event_buf_reset_state_addr,
	       state);

	/* first clear */
	if (!fu_nvt_ts_device_clear_fw_reset_state(self, error))
		return FALSE;

	ctx.state = state;
	ctx.last_state = 0;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_nvt_ts_device_wait_reset_state_cb,
				  100,
				  10,
				  &ctx,
				  error)) {
		g_warning("error, reset state buf[0] = 0x%02X", ctx.last_state);
		return FALSE;
	}

	g_info("reset state 0x%02X pass", state);
	return TRUE;
}

static gboolean
fu_nvt_ts_device_get_fw_ver(FuNvtTsDevice *self, GError **error)
{
	FuNvtTsFwVerCtx ctx = {0};

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_nvt_ts_device_get_fw_ver_cb,
				  10,
				  0,
				  &ctx,
				  error)) {
		g_warning("fw info is broken, fw_ver=0x%02X, ~fw_ver=0x%02X",
			  ctx.buf[0],
			  ctx.buf[1]);
		return FALSE;
	}

	self->fw_ver = ctx.buf[0];
	g_info("fw_ver = 0x%02X", self->fw_ver);
	return TRUE;
}

static gboolean
fu_nvt_ts_device_read_flash_data_gcm(FuNvtTsDevice *self,
				     guint32 flash_addr,
				     guint16 len,
				     guint8 *out,
				     GError **error)
{
	FuNvtTsReadFlashCtx ctx = {0};

	if (out == NULL || len == 0)
		return FALSE;
	/* keep this simple; expand later if you want >256 */
	if (len > 256)
		return FALSE;

	ctx.flash_addr = flash_addr;
	ctx.len = len;
	ctx.out = out;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_nvt_ts_device_read_flash_data_cb,
				  10,
				  0,
				  &ctx,
				  error)) {
		g_warning("flash read checksum mismatch");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_nvt_ts_device_read_flash_pid_gcm(FuNvtTsDevice *self, GError **error)
{
	guint8 pid_raw[4] = {0};
	char pid_str[5] = {0};
	guint64 pid64 = 0;

	if (self->fmap == NULL || self->fmap->flash_pid_addr == 0)
		return FALSE;

	if (!fu_nvt_ts_device_switch_gcm(self, 1, error))
		return -EIO;

	if (!fu_nvt_ts_device_resume_pd_gcm(self, error))
		return -EIO;

	if (!fu_nvt_ts_device_read_flash_mid_did_gcm(self, error))
		return -EIO;

	if (!fu_nvt_ts_device_ensure_read_flash_method(self, error))
		return FALSE;

	if (!fu_nvt_ts_device_read_flash_data_gcm(self,
						  self->fmap->flash_pid_addr,
						  4,
						  pid_raw,
						  error))
		return FALSE;

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
			return FALSE;
		}
	}

	self->flash_pid = (guint16)pid64;
	if (self->flash_pid == 0x0000 || self->flash_pid == 0xFFFF) {
		g_warning("pid in flash should not be 0x0000 or 0xFFFF");
		return FALSE;
	}

	g_info("flash_pid = 0x%04X", self->flash_pid);
	return TRUE;
}

static gboolean
fu_nvt_ts_device_setup(FuDevice *device, GError **error)
{
	FuDeviceClass *parent_class;
	g_autofree gchar *iid = NULL;
	guint8 debug_buf[6] = {0};
	FuNvtTsDevice *self = FU_NVT_TS_DEVICE(device);

	g_info("device setup");

	if (!fu_device_open(device, error))
		return FALSE;

	/* todo: add mmap mapping if support more IC later on */
	self->mmap = &nt36536_cascade_memory_map;
	self->fmap = &nt36536_flash_map;

	if (!fu_nvt_ts_device_hid_read(self, self->mmap->chip_ver_trim_addr, debug_buf, 6, error))
		return FALSE;
	g_info("IC chip id: %02X %02X %02X %02X %02X %02X",
	       debug_buf[0],
	       debug_buf[1],
	       debug_buf[2],
	       debug_buf[3],
	       debug_buf[4],
	       debug_buf[5]);

	if (!fu_nvt_ts_device_check_fw_reset_state(self, RESET_STATE_NORMAL_RUN, NULL) ||
	    !fu_nvt_ts_device_get_fw_ver(self, NULL)) {
		g_info("FW is not ready");
		self->fw_ver = 0;
	}

	fu_device_add_protocol(device, "tw.com.novatek.ts");
	fu_device_set_summary(device, "Novatek touchscreen controller");
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_NUMBER);

	fu_device_set_version_raw(device, self->fw_ver);

	if (!fu_nvt_ts_device_bootloader_reset(self, error))
		return FALSE;
	if (!fu_nvt_ts_device_sw_reset_and_idle(self, error))
		return FALSE;
	if (!fu_nvt_ts_device_stop_crc_reboot(self, error))
		return FALSE;

	/* get pid in flash to build GUID */
	if (!fu_nvt_ts_device_read_flash_pid_gcm(self, error)) {
		g_warning("failed to read flash PID");
		if (error != NULL && *error == NULL)
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_READ,
					    "failed to read flash PID");
		return FALSE;
	}

	if (!fu_nvt_ts_device_bootloader_reset(self, error))
		return FALSE;

	fu_device_build_vendor_id_u16(device, "HIDRAW", NVT_VID_NUM);

	/* build instance id for GUID */
	iid = g_strdup_printf("NVT_TS\\VID_0603\\PJID_%04X", self->flash_pid);

	/* turn instance IDs into GUIDs */
	fu_device_add_instance_id(device, iid);

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	/* fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD); */

	parent_class = FU_DEVICE_CLASS(fu_nvt_ts_device_parent_class);
	if (parent_class->setup != NULL)
		return parent_class->setup(device, error);

	return TRUE;
}

static void
fu_nvt_ts_device_init(FuNvtTsDevice *self)
{
	FuDevice *device = FU_DEVICE(self);

	g_info("device init");
	fu_device_register_private_flag(device, FU_NVT_TS_DEVICE_FLAG_SKIP_STATUS_REGISTER_CONTROL);
	fu_device_add_private_flag(device, FU_NVT_TS_DEVICE_FLAG_SKIP_STATUS_REGISTER_CONTROL);
	fu_device_retry_add_recovery(device,
				     FWUPD_ERROR,
				     FWUPD_ERROR_BUSY,
				     fu_nvt_ts_device_retry_busy_cb);

	fu_device_set_vendor(device, "Novatek");
	fu_device_set_name(device, "Novatek Touchscreen");
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
	FuNvtTsFwBin fwb;
} FuNvtTsUpdateCtx;

static gboolean
fu_nvt_ts_device_update_firmware_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuNvtTsUpdateCtx *ctx = (FuNvtTsUpdateCtx *)user_data;
	FuNvtTsDevice *self = FU_NVT_TS_DEVICE(device);
	if (!fu_nvt_ts_device_update_firmware_reset(self,
						    &ctx->fwb,
						    ctx->data,
						    ctx->size,
						    ctx->progress,
						    error)) {
		g_warning("Update Normal FW Failed");
		if (error != NULL && *error == NULL)
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_WRITE,
					    "Update Normal FW Failed");
		return FALSE;
	}

	g_info("update normal fw ok");
	return TRUE;
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
	FuNvtTsUpdateCtx ctx = {0};
	FuNvtTsDevice *self = FU_NVT_TS_DEVICE(device);

	g_info("device write firmware");

	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL)
		return FALSE;

	data = g_bytes_get_data(blob, &size);

	if (!fu_nvt_ts_device_get_binary_and_flash_start_addr_from_blob(self,
									&ctx.fwb,
									data,
									size,
									error))
		return FALSE;

	if (!fu_device_open(device, error))
		return FALSE;

	ctx.data = data;
	ctx.size = size;
	ctx.progress = progress;
	if (!fu_device_retry(device, fu_nvt_ts_device_update_firmware_cb, 3, &ctx, error)) {
		fu_nvt_ts_device_fw_bin_clear(&ctx.fwb);
		return FALSE;
	}

	if (!(fu_nvt_ts_device_check_fw_reset_state(self, RESET_STATE_NORMAL_RUN, error) &&
	      fu_nvt_ts_device_get_fw_ver(self, error))) {
		g_warning("FW is not ready");
		self->fw_ver = 0;
		fu_device_set_version_raw(device, self->fw_ver);
		fu_nvt_ts_device_fw_bin_clear(&ctx.fwb);
		return FALSE;
	}

	fu_device_set_version_raw(device, self->fw_ver);

	fu_nvt_ts_device_fw_bin_clear(&ctx.fwb);
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
