/*
 * Copyright 2026 Novatekmsp <novatekmsp@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "config.h"

#include "fu-nvt-ts-device.h"
#include "fu-nvt-ts-firmware.h"
#include "fu-nvt-ts-struct.h"

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
	guint16 flash_pid;
	guint8 flash_prog_data_cmd;
	guint8 flash_read_data_cmd;
	guint8 flash_read_pem_byte_len;
	guint8 flash_read_dummy_byte_len;
};

static gboolean
fu_nvt_ts_device_hid_read_dev(FuNvtTsDevice *self,
			      guint32 addr,
			      guint8 *data,
			      gsize len,
			      GError **error)
{
	g_autofree guint8 *buf_get = NULL;
	g_autoptr(FuStructNvtTsHidReadReq) st_req = NULL;

	if (len == 0) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "len must be > 0");
		return FALSE;
	}

	g_debug("read addr=0x%08x len=%zu", addr, len);

	/* set feature */
	st_req = fu_struct_nvt_ts_hid_read_req_new();
	fu_struct_nvt_ts_hid_read_req_set_i2c_hid_eng_report_id(st_req, NVT_TS_REPORT_ID);
	fu_struct_nvt_ts_hid_read_req_set_write_len(st_req, 0x000B);
	fu_struct_nvt_ts_hid_read_req_set_i2c_eng_addr(st_req, self->mmap->hid_i2c_eng_addr);
	fu_struct_nvt_ts_hid_read_req_set_target_addr(st_req, addr);
	fu_struct_nvt_ts_hid_read_req_set_len(st_req, (guint16)(len + 3));

	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  st_req->buf->data,
					  st_req->buf->len,
					  FU_IOCTL_FLAG_RETRY,
					  error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "set feature failed");
		return FALSE;
	}

	/* get feature */
	buf_get = g_new0(guint8, len + 1);
	buf_get[0] = NVT_TS_REPORT_ID;

	if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(self),
					  buf_get,
					  len + 1,
					  FU_IOCTL_FLAG_RETRY,
					  error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "get feature failed");
		return FALSE;
	}

	if (!fu_memcpy_safe(data, len, 0, buf_get, len + 1, 1, len, error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "copying feature data failed");
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
	gsize write_len, report_len;
	g_autofree guint8 *buf_set = NULL;
	g_autoptr(FuStructNvtTsHidWriteHdr) st_hdr = NULL;

	if (len == 0) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "len must be > 0");
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
	fu_struct_nvt_ts_hid_write_hdr_set_target_addr(st_hdr, addr);

	if (!fu_memcpy_safe(buf_set,
			    report_len,
			    0,
			    st_hdr->buf->data,
			    st_hdr->buf->len,
			    0,
			    st_hdr->buf->len,
			    error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "copying write header failed: ");
		return FALSE;
	}

	if (!fu_memcpy_safe(buf_set, report_len, 6, data, len, 0, len, error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "copying write buffer failed: ");
		return FALSE;
	}

	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  buf_set,
					  report_len,
					  FU_IOCTL_FLAG_RETRY,
					  error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_WRITE, "set feature failed: ");
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
	if (!fu_nvt_ts_device_hid_read_dev(self, addr, data, len, error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_READ, "fu_nvt_ts_device_hid_read failed");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_nvt_ts_device_hid_write(FuNvtTsDevice *self,
			   guint32 addr,
			   guint8 *data,
			   gsize len,
			   GError **error)
{
	if (!fu_nvt_ts_device_hid_write_dev(self, addr, data, len, error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_WRITE, "fu_nvt_ts_device_hid_write failed");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_nvt_ts_device_gcm_xfer_tx_chunk(FuNvtTsDevice *self,
				   FuChunk *chk,
				   guint8 *buf,
				   gsize buf_len,
				   guint32 base_addr,
				   GError **error)
{
	gsize len = fu_chunk_get_data_sz(chk);
	guint32 addr = base_addr + fu_chunk_get_address(chk);

	if (!fu_memcpy_safe(buf, buf_len, 0, fu_chunk_get_data(chk), len, 0, len, error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "copying tx buffer failed: ");
		return FALSE;
	}
	if (!fu_nvt_ts_device_hid_write(self, addr, buf, len, error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "Write tx data error");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_nvt_ts_device_gcm_xfer_rx_chunk(FuNvtTsDevice *self,
				   FuChunk *chk,
				   guint8 *buf,
				   gsize buf_len,
				   guint32 base_addr,
				   guint8 *rx_buf,
				   gsize rx_len,
				   GError **error)
{
	gsize len = fu_chunk_get_data_sz(chk);
	guint32 addr = base_addr + fu_chunk_get_address(chk);

	if (!fu_nvt_ts_device_hid_read(self, addr, buf, len, error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "Read rx data fail error");
		return FALSE;
	}
	if (!fu_memcpy_safe(rx_buf,
			    rx_len,
			    fu_chunk_get_address(chk),
			    buf,
			    buf_len,
			    0,
			    len,
			    error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "copying rx buffer failed: ");
		return FALSE;
	}
	return TRUE;
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
	SET_ERROR_OR_PREFIX(error,
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
	guint32 flash_cmd_addr = 0;
	guint32 flash_cmd_issue_addr = 0;
	guint32 rw_flash_data_addr = 0;
	guint32 i = 0;
	gint32 transfer_len = 0;
	gint32 total_buf_size = 0;
	gint32 write_len = 0;
	g_autoptr(GPtrArray) chunks_tx = NULL;
	g_autoptr(GPtrArray) chunks_rx = NULL;
	g_autoptr(FuStructNvtTsGcmCmd) st_cmd = NULL;
	FuChunk *chk = NULL;
	FuNvtTsCmdIssueCtx cmd_issue_ctx = {0};

	flash_cmd_addr = self->mmap->flash_cmd_addr;
	flash_cmd_issue_addr = self->mmap->flash_cmd_issue_addr;
	rw_flash_data_addr = self->mmap->rw_flash_data_addr;

	transfer_len = NVT_TRANSFER_LEN;

	total_buf_size = 64 + xfer->tx_len + xfer->rx_len;
	buf = g_malloc0(total_buf_size);
	if (buf == NULL) {
		SET_ERROR_OR_PREFIX(error,
				    FWUPD_ERROR_INTERNAL,
				    "No memory for %d bytes",
				    total_buf_size);
		return FALSE;
	}

	if ((xfer->tx_len > 0) && xfer->tx_buf != NULL) {
		chunks_tx = fu_chunk_array_new(xfer->tx_buf,
					       xfer->tx_len,
					       FU_CHUNK_ADDR_OFFSET_NONE,
					       FU_CHUNK_PAGESZ_NONE,
					       transfer_len);
		for (i = 0; i < chunks_tx->len; i++) {
			chk = g_ptr_array_index(chunks_tx, i);
			if (!fu_nvt_ts_device_gcm_xfer_tx_chunk(self,
								chk,
								buf,
								total_buf_size,
								rw_flash_data_addr,
								error))
				return FALSE;
		}
	}

	memset(buf, 0, total_buf_size);
	st_cmd = fu_struct_nvt_ts_gcm_cmd_new();
	fu_struct_nvt_ts_gcm_cmd_set_flash_cmd(st_cmd, xfer->flash_cmd);
	if (xfer->flash_addr_len > 0)
		fu_struct_nvt_ts_gcm_cmd_set_flash_addr(st_cmd, xfer->flash_addr);
	else
		fu_struct_nvt_ts_gcm_cmd_set_flash_addr(st_cmd, 0);
	write_len = xfer->flash_addr_len + xfer->pem_byte_len + xfer->dummy_byte_len + xfer->tx_len;
	fu_struct_nvt_ts_gcm_cmd_set_write_len(st_cmd, (guint16)write_len);
	fu_struct_nvt_ts_gcm_cmd_set_read_len(st_cmd, (guint16)xfer->rx_len);
	fu_struct_nvt_ts_gcm_cmd_set_flash_checksum(st_cmd, xfer->flash_checksum);
	fu_struct_nvt_ts_gcm_cmd_set_magic(st_cmd, 0xC2);
	if (!fu_nvt_ts_device_hid_write(self,
					flash_cmd_addr,
					st_cmd->buf->data,
					st_cmd->buf->len,
					error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "Write enter GCM error");
		return FALSE;
	}

	cmd_issue_ctx.addr = flash_cmd_issue_addr;
	cmd_issue_ctx.buf = buf;
	cmd_issue_ctx.flash_cmd = xfer->flash_cmd;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_nvt_ts_device_wait_cmd_issue_cb,
				  2000,
				  1,
				  &cmd_issue_ctx,
				  error)) {
		SET_ERROR_OR_PREFIX(error,
				    FWUPD_ERROR_INTERNAL,
				    "write GCM cmd 0x%02X failed",
				    xfer->flash_cmd);
		return FALSE;
	}

	if ((xfer->rx_len > 0) && xfer->rx_buf != NULL) {
		memset(buf, 0, xfer->rx_len);
		chunks_rx = fu_chunk_array_new(NULL,
					       xfer->rx_len,
					       FU_CHUNK_ADDR_OFFSET_NONE,
					       FU_CHUNK_PAGESZ_NONE,
					       transfer_len);
		for (i = 0; i < chunks_rx->len; i++) {
			chk = g_ptr_array_index(chunks_rx, i);
			if (!fu_nvt_ts_device_gcm_xfer_rx_chunk(self,
								chk,
								buf,
								total_buf_size,
								rw_flash_data_addr,
								xfer->rx_buf,
								xfer->rx_len,
								error))
				return FALSE;
		}
	}

	return TRUE;
}

static gboolean
fu_nvt_ts_device_write_enable_gcm(FuNvtTsDevice *self, GError **error)
{
	FuNvtTsGcmXfer xfer = {0};

	xfer.flash_cmd = 0x06;
	if (!fu_nvt_ts_device_gcm_xfer(self, &xfer, error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "Write Enable failed");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_nvt_ts_device_read_status_gcm(FuNvtTsDevice *self, guint8 *status, GError **error)
{
	FuNvtTsGcmXfer xfer = {0};

	xfer.flash_cmd = 0x05;
	xfer.rx_len = 1;
	xfer.rx_buf = status;
	if (!fu_nvt_ts_device_gcm_xfer(self, &xfer, error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "Read Status GCM fail");
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
	if ((*ctx->status & 0x01) == 0x00)
		return TRUE;
	SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_BUSY, "device busy");
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
	SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_BUSY, "gcm enable/disable not ready");
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
	SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_BUSY, "reset state not ready");
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
	SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_BUSY, "fw info not ready");
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

	calc += (ctx->flash_addr >> 16) & 0xFF;
	calc += (ctx->flash_addr >> 8) & 0xFF;
	calc += (ctx->flash_addr >> 0) & 0xFF;
	calc += (ctx->len >> 8) & 0xFF;
	calc += (ctx->len >> 0) & 0xFF;

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
	calc += fu_sum8(ctx->out, ctx->len);
	/* 0xFFFF - sum + 1 */
	calc = 65535 - calc + 1;

	if (rd_checksum == calc)
		return TRUE;

	SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_BUSY, "flash read checksum mismatch");
	return FALSE;
}

typedef struct {
	guint32 flash_cksum_status_addr;
	guint8 *buf;
	gboolean allow_retry_once;
	gboolean *retry_needed;
	gboolean checksum_error;
} FuNvtTsChecksumCtx;

typedef struct {
	FuNvtTsDevice *self;
	FuChunk *chk;
	guint32 flash_address;
	guint32 flash_cksum_status_addr;
	guint8 *buf;
	gboolean allow_retry_once;
} FuNvtTsPageProgramCtx;

static gboolean
fu_nvt_ts_device_sector_erase_gcm(FuNvtTsDevice *self, guint32 flash_addr, GError **error)
{
	FuNvtTsGcmXfer xfer = {0};

	xfer.flash_cmd = 0x20;
	xfer.flash_addr = flash_addr;
	xfer.flash_addr_len = 3;
	if (!fu_nvt_ts_device_gcm_xfer(self, &xfer, error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "Sector Erase GCM fail");
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
	guint32 i = 0;

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
	xfer.flash_cmd = self->flash_prog_data_cmd;
	xfer.flash_addr = flash_addr;
	xfer.flash_addr_len = 3;
	xfer.tx_buf = data;
	xfer.tx_len = data_len;
	xfer.flash_checksum = checksum & 0xFFFF;
	if (!fu_nvt_ts_device_gcm_xfer(self, &xfer, error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "Page Program GCM fail");
		return FALSE;
	}

	return TRUE;
}

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
		if (ctx->allow_retry_once) {
			*ctx->retry_needed = TRUE;
			ctx->allow_retry_once = FALSE;
			return TRUE;
		}
		ctx->checksum_error = TRUE;
		return TRUE;
	}
	SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_BUSY, "checksum not ready");
	return FALSE;
}

static gboolean
fu_nvt_ts_device_check_flash_checksum(FuNvtTsDevice *self,
				      guint32 flash_cksum_status_addr,
				      guint8 *buf,
				      gboolean allow_retry_once,
				      gboolean *retry_needed,
				      GError **error)
{
	FuNvtTsChecksumCtx ctx = {0};

	*retry_needed = FALSE;
	ctx.flash_cksum_status_addr = flash_cksum_status_addr;
	ctx.buf = buf;
	ctx.allow_retry_once = allow_retry_once;
	ctx.retry_needed = retry_needed;
	ctx.checksum_error = FALSE;

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_nvt_ts_device_check_flash_checksum_cb,
				  20,
				  1,
				  &ctx,
				  error))
		return FALSE;

	if (ctx.checksum_error) {
		SET_ERROR_OR_PREFIX(error,
				    FWUPD_ERROR_INTERNAL,
				    "check flash checksum status error");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_nvt_ts_device_page_program_retry_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuNvtTsPageProgramCtx *ctx = (FuNvtTsPageProgramCtx *)user_data;
	gboolean retry_page = FALSE;

	/* write enable */
	if (!fu_nvt_ts_device_write_enable_gcm(ctx->self, error))
		return FALSE;
	/* write page: FLASH_PAGE_SIZE bytes */
	if (!fu_nvt_ts_device_page_program_gcm(ctx->self,
					       ctx->flash_address,
					       (guint16)fu_chunk_get_data_sz(ctx->chk),
					       (guint8 *)fu_chunk_get_data(ctx->chk),
					       error))
		return FALSE;

	/* check flash checksum status */
	if (!fu_nvt_ts_device_check_flash_checksum(ctx->self,
						   ctx->flash_cksum_status_addr,
						   ctx->buf,
						   ctx->allow_retry_once,
						   &retry_page,
						   error))
		return FALSE;
	if (retry_page) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_BUSY, "page program retry");
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

	xfer.flash_cmd = self->flash_read_data_cmd;
	xfer.flash_addr = flash_addr;
	xfer.flash_addr_len = 3;
	xfer.pem_byte_len = self->flash_read_pem_byte_len;
	xfer.dummy_byte_len = self->flash_read_dummy_byte_len;
	xfer.rx_len = data_len;
	if (!fu_nvt_ts_device_gcm_xfer(self, &xfer, error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "Get Checksum GCM fail");
		return FALSE;
	}

	buf[0] = 0x00;
	buf[1] = 0x00;
	if (!fu_nvt_ts_device_hid_read(self, self->mmap->read_flash_checksum_addr, buf, 2, error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "Get checksum error");
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
	if (!fu_device_retry(FU_DEVICE(self), fu_nvt_ts_device_switch_gcm_cb, 3, &ctx, error)) {
		if (enable)
			SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "enable gcm failed");
		else
			SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "disable gcm failed");
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

	xfer.flash_cmd = 0xAB;
	if (!fu_nvt_ts_device_gcm_xfer(self, &xfer, error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "Resume PD failed");
		return FALSE;
	}

	g_info("resume pd ok");
	return TRUE;
}

static gboolean
fu_nvt_ts_device_erase_flash_gcm(FuNvtTsDevice *self, FuNvtTsFwBin *fwb, GError **error)
{
	guint8 status = 0;
	guint32 i = 0;
	guint32 flash_address = 0;
	guint32 erase_length = 0;
	FuNvtTsStatusReadyCtx status_ctx = {0};
	g_autoptr(GPtrArray) chunks = NULL;
	FuChunk *chk = NULL;

	if (fwb->flash_start_addr % FLASH_SECTOR_SIZE) {
		SET_ERROR_OR_PREFIX(error,
				    FWUPD_ERROR_INTERNAL,
				    "flash_start_addr should be n*%d",
				    FLASH_SECTOR_SIZE);
		return FALSE;
	}

	erase_length = fwb->bin_size;

	/* write enable */
	if (!fu_nvt_ts_device_write_enable_gcm(self, error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "Write Enable error");
		return FALSE;
	}

	/* read status */
	status_ctx.status = &status;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_nvt_ts_device_wait_status_ready_cb,
				  100,
				  5,
				  &status_ctx,
				  error)) {
		SET_ERROR_OR_PREFIX(error,
				    FWUPD_ERROR_INTERNAL,
				    "read status failed, status = 0x%02X",
				    status);
		return FALSE;
	}
	g_debug("read status register ok, status = 0x%02X", status);

	chunks = fu_chunk_array_new(NULL,
				    erase_length,
				    fwb->flash_start_addr,
				    FU_CHUNK_PAGESZ_NONE,
				    FLASH_SECTOR_SIZE);
	for (i = 0; i < chunks->len; i++) {
		guint32 page = 0;
		chk = g_ptr_array_index(chunks, i);
		flash_address = (guint32)fu_chunk_get_address(chk);
		page = flash_address / FLASH_SECTOR_SIZE;
		/* write enable */
		if (!fu_nvt_ts_device_write_enable_gcm(self, error)) {
			SET_ERROR_OR_PREFIX(error,
					    FWUPD_ERROR_INTERNAL,
					    "Write enable error, page at = %d",
					    page);
			return FALSE;
		}

		/* sector erase */
		if (!fu_nvt_ts_device_sector_erase_gcm(self, flash_address, error)) {
			SET_ERROR_OR_PREFIX(error,
					    FWUPD_ERROR_INTERNAL,
					    "Sector erase error, page at = %d",
					    page);
			return FALSE;
		}
		fu_device_sleep(FU_DEVICE(self), 25);

		status_ctx.status = &status;
		if (!fu_device_retry_full(FU_DEVICE(self),
					  fu_nvt_ts_device_wait_status_ready_cb,
					  100,
					  5,
					  &status_ctx,
					  error)) {
			SET_ERROR_OR_PREFIX(error,
					    FWUPD_ERROR_INTERNAL,
					    "wait sector erase timeout");
			return FALSE;
		}
	}

	g_info("erase flash ok");

	return TRUE;
}

static gboolean
fu_nvt_ts_device_ensure_prog_flash_method(FuNvtTsDevice *self, GError **error)
{
	self->flash_prog_data_cmd = 0x02;
	g_debug("flash program cmd = 0x%02X", self->flash_prog_data_cmd);
	return TRUE;
}

static gboolean
fu_nvt_ts_device_write_flash_gcm(FuNvtTsDevice *self,
				 FuNvtTsFwBin *fwb,
				 FuProgress *progress,
				 GError **error)
{
	guint8 buf[1] = {0};
	guint32 flash_address = 0;
	guint32 flash_cksum_status_addr = self->mmap->flash_cksum_status_addr;
	guint32 offset = 0;
	guint32 i = 0;
	guint8 status = 0;
	g_autoptr(GPtrArray) chunks = NULL;
	FuChunk *chk = NULL;
	FuNvtTsStatusReadyCtx status_ctx = {0};
	FuNvtTsPageProgramCtx page_ctx = {0};

	if (!fu_nvt_ts_device_ensure_prog_flash_method(self, error))
		return FALSE;

	fu_progress_set_id(progress, G_STRLOC);
	chunks = fu_chunk_array_new(fwb->bin_data,
				    fwb->bin_size,
				    FU_CHUNK_ADDR_OFFSET_NONE,
				    FU_CHUNK_PAGESZ_NONE,
				    FLASH_PAGE_SIZE);
	for (i = 0; i < chunks->len; i++) {
		chk = g_ptr_array_index(chunks, i);
		page_ctx.self = self;
		page_ctx.chk = chk;
		page_ctx.flash_address = 0;
		page_ctx.flash_cksum_status_addr = flash_cksum_status_addr;
		page_ctx.buf = buf;
		page_ctx.allow_retry_once = TRUE;
		offset = fu_chunk_get_address(chk);
		flash_address = offset + fwb->flash_start_addr;
		page_ctx.flash_address = flash_address;

		if (!fu_device_retry_full(FU_DEVICE(self),
					  fu_nvt_ts_device_page_program_retry_cb,
					  2,
					  1,
					  &page_ctx,
					  error)) {
			SET_ERROR_OR_PREFIX(error,
					    FWUPD_ERROR_INTERNAL,
					    "page program error, i= %d",
					    i);
			return FALSE;
		}

		/* read status */
		status_ctx.status = &status;
		if (!fu_device_retry_full(FU_DEVICE(self),
					  fu_nvt_ts_device_wait_status_ready_cb,
					  200,
					  1,
					  &status_ctx,
					  error)) {
			SET_ERROR_OR_PREFIX(error,
					    FWUPD_ERROR_INTERNAL,
					    "wait page program timeout");
			return FALSE;
		}

		/* show progress */
		fu_progress_set_percentage_full(progress, i + 1, chunks->len);
	}
	fu_progress_set_percentage(progress, 100);
	g_info("program flash ok");

	return TRUE;
}

static gboolean
fu_nvt_ts_device_ensure_read_flash_method(FuNvtTsDevice *self, GError **error)
{
	self->flash_read_data_cmd = 0x03;
	self->flash_read_pem_byte_len = 0;
	self->flash_read_dummy_byte_len = 0;
	g_debug("flash read cmd = 0x%02X", self->flash_read_data_cmd);
	return TRUE;
}

static gboolean
fu_nvt_ts_device_verify_flash_gcm(FuNvtTsDevice *self, FuNvtTsFwBin *fwb, GError **error)
{
	guint16 write_checksum = 0;
	guint16 read_checksum = 0;
	guint32 flash_addr = 0;
	guint32 data_len = 0;
	guint32 offset = 0;
	guint32 i = 0;
	g_autoptr(GPtrArray) chunks = NULL;
	FuChunk *chk = NULL;

	if (!fu_nvt_ts_device_ensure_read_flash_method(self, error))
		return FALSE;

	chunks = fu_chunk_array_new(fwb->bin_data,
				    fwb->bin_size,
				    FU_CHUNK_ADDR_OFFSET_NONE,
				    FU_CHUNK_PAGESZ_NONE,
				    FLASH_SECTOR_SIZE);
	for (i = 0; i < chunks->len; i++) {
		chk = g_ptr_array_index(chunks, i);
		offset = fu_chunk_get_address(chk);
		flash_addr = offset + fwb->flash_start_addr;
		data_len = fu_chunk_get_data_sz(chk);
		/* calculate write_checksum of each 4KB block */
		write_checksum = (flash_addr & 0xFF);
		write_checksum += ((flash_addr >> 8) & 0xFF);
		write_checksum += ((flash_addr >> 16) & 0xFF);
		write_checksum += ((data_len) & 0xFF);
		write_checksum += (((data_len) >> 8) & 0xFF);
		write_checksum += fu_sum16(&fwb->bin_data[offset], data_len);
		write_checksum = ~write_checksum + 1;

		if (!fu_nvt_ts_device_get_checksum_gcm(self,
						       flash_addr,
						       data_len,
						       &read_checksum,
						       error)) {
			SET_ERROR_OR_PREFIX(error,
					    FWUPD_ERROR_INTERNAL,
					    "Get Checksum failed, i = %u",
					    i);
			return FALSE;
		}
		if (write_checksum != read_checksum) {
			SET_ERROR_OR_PREFIX(error,
					    FWUPD_ERROR_INTERNAL,
					    "Verify Failed, i = %u, write_checksum = 0x%04X, "
					    "read_checksum = 0x%04X",
					    i,
					    write_checksum,
					    read_checksum);
			return FALSE;
		}
	}

	g_info("verify flash ok");

	return TRUE;
}

static gboolean
fu_nvt_ts_device_read_flash_mid_did_gcm(FuNvtTsDevice *self, GError **error)
{
	guint8 buf[3] = {0};
	FuNvtTsGcmXfer xfer = {0};
	guint8 flash_mid = 0;
	guint16 flash_did = 0;

	xfer.flash_cmd = 0x9F;
	xfer.rx_buf = buf;
	xfer.rx_len = 3;
	if (!fu_nvt_ts_device_gcm_xfer(self, &xfer, error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "read flash mid did gcm failed");
		return FALSE;
	}

	flash_mid = buf[0];
	flash_did = (buf[1] << 8) | buf[2];
	g_debug("flash mid = 0x%02X, did = 0x%04X", flash_mid, flash_did);
	g_debug("read mid did ok");
	return TRUE;
}

static gboolean
fu_nvt_ts_device_update_firmware(FuNvtTsDevice *self,
				 FuNvtTsFwBin *fwb,
				 FuProgress *progress,
				 GError **error)
{
	g_info("start enable gcm");
	if (!fu_nvt_ts_device_switch_gcm(self, 1, error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "enable gcm failed");
		return FALSE;
	}

	g_info("start resume pd");
	if (!fu_nvt_ts_device_resume_pd_gcm(self, error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "resume pd failed");
		return FALSE;
	}

	g_info("start read flash id");
	if (!fu_nvt_ts_device_read_flash_mid_did_gcm(self, error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "read flash id failed");
		return FALSE;
	}

	g_info("start erase flash");
	if (!fu_nvt_ts_device_erase_flash_gcm(self, fwb, error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "erase flash failed");
		return FALSE;
	}

	g_info("start program flash");
	if (!fu_nvt_ts_device_write_flash_gcm(self, fwb, progress, error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "program flash failed");
		return FALSE;
	}

	g_info("start verify flash");
	if (!fu_nvt_ts_device_verify_flash_gcm(self, fwb, error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "verify flash failed");
		return FALSE;
	}

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
	guint32 i = 0;

	g_debug("%s (0xA5 to 0x%06X) 20 times", __func__, self->mmap->bld_spe_pups_addr);
	for (i = 0; i < 20; i++) {
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
				       FuProgress *progress,
				       GError **error)
{
	if (!fu_nvt_ts_device_bootloader_reset(self, error))
		return FALSE;
	if (!fu_nvt_ts_device_sw_reset_and_idle(self, error))
		return FALSE;
	if (!fu_nvt_ts_device_stop_crc_reboot(self, error))
		return FALSE;

	if (!fu_nvt_ts_device_update_firmware(self, fwb, progress, error))
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
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_NOT_SUPPORTED, "subsystem is not hidraw");
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
		SET_ERROR_OR_PREFIX(error,
				    FWUPD_ERROR_INTERNAL,
				    "error, reset state buf[0] = 0x%02X",
				    ctx.last_state);
		return FALSE;
	}

	g_info("reset state 0x%02X pass", state);
	return TRUE;
}

static gboolean
fu_nvt_ts_device_get_fw_ver(FuNvtTsDevice *self, GError **error)
{
	FuNvtTsFwVerCtx ctx = {0};

	if (!fu_device_retry(FU_DEVICE(self), fu_nvt_ts_device_get_fw_ver_cb, 10, &ctx, error)) {
		SET_ERROR_OR_PREFIX(error,
				    FWUPD_ERROR_INTERNAL,
				    "fw info is broken, fw_ver=0x%02X, ~fw_ver=0x%02X",
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
	if (!fu_device_retry(FU_DEVICE(self),
			     fu_nvt_ts_device_read_flash_data_cb,
			     10,
			     &ctx,
			     error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "flash read checksum mismatch");
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

	if (self->fmap == NULL || self->fmap->flash_pid_addr == 0) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "flash PID address is not set");
		return FALSE;
	}

	if (!fu_nvt_ts_device_switch_gcm(self, 1, error))
		return FALSE;

	if (!fu_nvt_ts_device_resume_pd_gcm(self, error))
		return FALSE;

	if (!fu_nvt_ts_device_read_flash_mid_did_gcm(self, error))
		return FALSE;

	if (!fu_nvt_ts_device_ensure_read_flash_method(self, error))
		return FALSE;

	if (!fu_nvt_ts_device_read_flash_data_gcm(self,
						  self->fmap->flash_pid_addr,
						  4,
						  pid_raw,
						  error))
		return FALSE;

	/* pid_str: [2][3][0][1] */
	pid_str[0] = (char)pid_raw[2];
	pid_str[1] = (char)pid_raw[3];
	pid_str[2] = (char)pid_raw[0];
	pid_str[3] = (char)pid_raw[1];
	pid_str[4] = '\0';

	if (!fu_strtoull(pid_str, &pid64, 0, 0xFFFF, FU_INTEGER_BASE_16, error)) {
		SET_ERROR_OR_PREFIX(error,
				    FWUPD_ERROR_INTERNAL,
				    "invalid pid read from flash: '%s' (%02X %02X %02X %02X): ",
				    pid_str,
				    pid_raw[0],
				    pid_raw[1],
				    pid_raw[2],
				    pid_raw[3]);
		return FALSE;
	}

	self->flash_pid = (guint16)pid64;
	if (self->flash_pid == 0x0000 || self->flash_pid == 0xFFFF) {
		SET_ERROR_OR_PREFIX(error,
				    FWUPD_ERROR_INTERNAL,
				    "pid in flash should not be 0x0000 or 0xFFFF");
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

	if (!fu_nvt_ts_device_check_fw_reset_state(self, RESET_STATE_NORMAL_RUN, error) ||
	    !fu_nvt_ts_device_get_fw_ver(self, error)) {
		g_warning("firmware is not normal running before firmware update, proceed");
		self->fw_ver = 0;
		/* allow update to proceed */
		g_clear_error(error);
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
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "failed to read flash PID");
		return FALSE;
	}

	if (!fu_nvt_ts_device_bootloader_reset(self, error))
		return FALSE;

	fu_device_build_vendor_id_u16(device, "HIDRAW", NVT_VID_NUM);

	/* build instance id for GUID */
	iid = g_strdup_printf("NVT_TS\\VID_0603\\PJID_%04X", self->flash_pid);

	/* turn instance IDs into GUIDs */
	fu_device_add_instance_id(device, iid);

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
	fu_device_retry_add_recovery(device,
				     FWUPD_ERROR,
				     FWUPD_ERROR_BUSY,
				     fu_nvt_ts_device_retry_busy_cb);

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);

	fu_device_set_vendor(device, "Novatek");
	fu_device_set_name(device, "Novatek Touchscreen");
}

static gchar *
fu_nvt_ts_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint16((guint16)version_raw, fu_device_get_version_format(device));
}

typedef struct {
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
						    ctx->progress,
						    error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "Update Normal FW Failed");
		return FALSE;
	}

	g_info("update normal fw ok");
	return TRUE;
}

static FuFirmware *
fu_nvt_ts_device_prepare_firmware(FuDevice *device,
				  GInputStream *stream,
				  FuProgress *progress,
				  FuFirmwareParseFlags flags,
				  GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_nvt_ts_firmware_new();

	(void)device;

	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;
	return g_steal_pointer(&firmware);
}

static gboolean
fu_nvt_ts_device_write_firmware(FuDevice *device,
				FuFirmware *firmware,
				FuProgress *progress,
				FwupdInstallFlags flags,
				GError **error)
{
	gboolean success = TRUE;
	FuNvtTsUpdateCtx ctx = {0};
	FuNvtTsDevice *self = FU_NVT_TS_DEVICE(device);

	g_info("device write firmware");

	g_return_val_if_fail(FU_IS_NVT_TS_FIRMWARE(firmware), FALSE);

	if (!fu_nvt_ts_firmware_prepare_fwb(FU_NVT_TS_FIRMWARE(firmware),
					    &ctx.fwb,
					    self->fmap->flash_normal_fw_start_addr,
					    self->fmap->flash_max_size,
					    error))
		return FALSE;

	if (!fu_device_open(device, error))
		return FALSE;

	ctx.progress = progress;
	if (!fu_device_retry(device, fu_nvt_ts_device_update_firmware_cb, 3, &ctx, error)) {
		fu_nvt_ts_firmware_bin_clear(&ctx.fwb);
		return FALSE;
	}

	if (!(fu_nvt_ts_device_check_fw_reset_state(self, RESET_STATE_NORMAL_RUN, error) &&
	      fu_nvt_ts_device_get_fw_ver(self, error))) {
		SET_ERROR_OR_PREFIX(error,
				    FWUPD_ERROR_INTERNAL,
				    "firmware is not normal running after firmware update");
		self->fw_ver = 0;
		success = FALSE;
	}

	fu_device_set_version_raw(device, self->fw_ver);
	fu_nvt_ts_firmware_bin_clear(&ctx.fwb);
	return success;
}

static void
fu_nvt_ts_device_class_init(FuNvtTsDeviceClass *klass)
{
	FuDeviceClass *device_class;

	device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_nvt_ts_device_probe;
	device_class->setup = fu_nvt_ts_device_setup;
	device_class->prepare_firmware = fu_nvt_ts_device_prepare_firmware;
	device_class->write_firmware = fu_nvt_ts_device_write_firmware;
	device_class->convert_version = fu_nvt_ts_device_convert_version;
}
