/*
 * Copyright 2026 Novatekmsp <novatekmsp@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "config.h"

#include "fu-novatek-ts-device.h"
#include "fu-novatek-ts-firmware.h"
#include "fu-novatek-ts-struct.h"

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "FuPluginNovatekTs"

static FuNovatekTsMemMap *
fu_novatek_ts_device_mem_map_new_nt36536(void)
{
	FuNovatekTsMemMap *mmap = fu_novatek_ts_mem_map_new();
	fu_novatek_ts_mem_map_set_chip_ver_trim_addr(mmap,
						     FU_NOVATEK_TS_MEM_MAP_REG_CHIP_VER_TRIM_ADDR);
	fu_novatek_ts_mem_map_set_swrst_sif_addr(mmap, FU_NOVATEK_TS_MEM_MAP_REG_SWRST_SIF_ADDR);
	fu_novatek_ts_mem_map_set_event_buf_cmd_addr(mmap,
						     FU_NOVATEK_TS_MEM_MAP_REG_EVENT_BUF_CMD_ADDR);
	fu_novatek_ts_mem_map_set_event_buf_hs_sub_cmd_addr(
	    mmap,
	    FU_NOVATEK_TS_MEM_MAP_REG_EVENT_BUF_HS_SUB_CMD_ADDR);
	fu_novatek_ts_mem_map_set_event_buf_reset_state_addr(
	    mmap,
	    FU_NOVATEK_TS_MEM_MAP_REG_EVENT_BUF_RESET_STATE_ADDR);
	fu_novatek_ts_mem_map_set_event_map_fwinfo_addr(
	    mmap,
	    FU_NOVATEK_TS_MEM_MAP_REG_EVENT_MAP_FWINFO_ADDR);
	fu_novatek_ts_mem_map_set_read_flash_checksum_addr(
	    mmap,
	    FU_NOVATEK_TS_MEM_MAP_REG_READ_FLASH_CHECKSUM_ADDR);
	fu_novatek_ts_mem_map_set_rw_flash_data_addr(mmap,
						     FU_NOVATEK_TS_MEM_MAP_REG_RW_FLASH_DATA_ADDR);
	fu_novatek_ts_mem_map_set_enb_casc_addr(mmap, FU_NOVATEK_TS_MEM_MAP_REG_ENB_CASC_ADDR);
	fu_novatek_ts_mem_map_set_hid_i2c_eng_addr(mmap,
						   FU_NOVATEK_TS_MEM_MAP_REG_HID_I2C_ENG_ADDR);
	fu_novatek_ts_mem_map_set_gcm_code_addr(mmap, FU_NOVATEK_TS_MEM_MAP_REG_GCM_CODE_ADDR);
	fu_novatek_ts_mem_map_set_gcm_flag_addr(mmap, FU_NOVATEK_TS_MEM_MAP_REG_GCM_FLAG_ADDR);
	fu_novatek_ts_mem_map_set_flash_cmd_addr(mmap, FU_NOVATEK_TS_MEM_MAP_REG_FLASH_CMD_ADDR);
	fu_novatek_ts_mem_map_set_flash_cmd_issue_addr(
	    mmap,
	    FU_NOVATEK_TS_MEM_MAP_REG_FLASH_CMD_ISSUE_ADDR);
	fu_novatek_ts_mem_map_set_flash_cksum_status_addr(
	    mmap,
	    FU_NOVATEK_TS_MEM_MAP_REG_FLASH_CKSUM_STATUS_ADDR);
	fu_novatek_ts_mem_map_set_bld_spe_pups_addr(mmap,
						    FU_NOVATEK_TS_MEM_MAP_REG_BLD_SPE_PUPS_ADDR);
	return mmap;
}

static FuNovatekTsFlashMap *
fu_novatek_ts_device_flash_map_new_nt36536(void)
{
	FuNovatekTsFlashMap *fmap = fu_novatek_ts_flash_map_new();
	fu_novatek_ts_flash_map_set_flash_normal_fw_start_addr(
	    fmap,
	    FU_NOVATEK_TS_FLASH_MAP_CONST_FLASH_NORMAL_FW_START_ADDR);
	fu_novatek_ts_flash_map_set_flash_pid_addr(fmap,
						   FU_NOVATEK_TS_FLASH_MAP_CONST_FLASH_PID_ADDR);
	fu_novatek_ts_flash_map_set_flash_max_size(fmap,
						   FU_NOVATEK_TS_FLASH_MAP_CONST_FLASH_MAX_SIZE);
	return fmap;
}

struct _FuNovatekTsDevice {
	FuHidrawDevice parent_instance;
	FuNovatekTsMemMap *mmap;
	FuNovatekTsFlashMap *fmap;
	guint8 fw_ver;
	guint16 flash_pid;
	guint8 flash_prog_data_cmd;
	guint8 flash_read_data_cmd;
	guint8 flash_read_pem_byte_len;
	guint8 flash_read_dummy_byte_len;
};

static gboolean
fu_novatek_ts_device_hid_read_dev(FuNovatekTsDevice *self,
				  guint32 addr,
				  guint8 *data,
				  gsize len,
				  GError **error)
{
	g_autofree guint8 *buf_get = NULL;
	g_autoptr(FuStructNovatekTsHidReadReq) st_req = NULL;

	if (len == 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "len must be > 0");
		return FALSE;
	}

	g_debug("read addr=0x%08x len=%zu", addr, len);

	/* set feature */
	st_req = fu_struct_novatek_ts_hid_read_req_new();
	fu_struct_novatek_ts_hid_read_req_set_i2c_hid_eng_report_id(st_req, NVT_TS_REPORT_ID);
	fu_struct_novatek_ts_hid_read_req_set_write_len(st_req,
							FU_NOVATEK_TS_HID_CONST_READ_REQ_WRITE_LEN);
	fu_struct_novatek_ts_hid_read_req_set_i2c_eng_addr(
	    st_req,
	    fu_novatek_ts_mem_map_get_hid_i2c_eng_addr(self->mmap));
	fu_struct_novatek_ts_hid_read_req_set_target_addr(st_req, addr);
	fu_struct_novatek_ts_hid_read_req_set_len(st_req, (guint16)(len + 3));

	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  st_req->buf->data,
					  st_req->buf->len,
					  FU_IOCTL_FLAG_RETRY,
					  error)) {
		g_prefix_error_literal(error, "set feature failed: ");
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
		g_prefix_error_literal(error, "get feature failed: ");
		return FALSE;
	}

	if (!fu_memcpy_safe(data, len, 0, buf_get, len + 1, 1, len, error)) {
		g_prefix_error_literal(error, "copying feature data failed: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_novatek_ts_device_hid_write_dev(FuNovatekTsDevice *self,
				   guint32 addr,
				   guint8 *data,
				   gsize len,
				   GError **error)
{
	gsize write_len, report_len;
	g_autofree guint8 *buf_set = NULL;
	g_autoptr(FuStructNovatekTsHidWriteHdr) st_hdr = NULL;

	if (len == 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "len must be > 0");
		return FALSE;
	}

	g_debug("write addr=0x%08x len=%zu, data:", addr, len);
	fu_dump_raw(G_LOG_DOMAIN, "write-data", data, len);

	write_len = len + 5;

	report_len = write_len + 1;

	buf_set = g_new0(guint8, report_len);

	st_hdr = fu_struct_novatek_ts_hid_write_hdr_new();
	fu_struct_novatek_ts_hid_write_hdr_set_i2c_hid_eng_report_id(st_hdr, NVT_TS_REPORT_ID);
	fu_struct_novatek_ts_hid_write_hdr_set_write_len(st_hdr, (guint16)write_len);
	fu_struct_novatek_ts_hid_write_hdr_set_target_addr(st_hdr, addr);

	if (!fu_memcpy_safe(buf_set,
			    report_len,
			    0,
			    st_hdr->buf->data,
			    st_hdr->buf->len,
			    0,
			    st_hdr->buf->len,
			    error)) {
		g_prefix_error_literal(error, "copying write header failed: ");
		return FALSE;
	}

	if (!fu_memcpy_safe(buf_set, report_len, 6, data, len, 0, len, error)) {
		g_prefix_error_literal(error, "copying write buffer failed: ");
		return FALSE;
	}

	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  buf_set,
					  report_len,
					  FU_IOCTL_FLAG_RETRY,
					  error)) {
		g_prefix_error_literal(error, "set feature failed: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_novatek_ts_device_hid_read(FuNovatekTsDevice *self,
			      guint32 addr,
			      guint8 *data,
			      gsize len,
			      GError **error)
{
	if (!fu_novatek_ts_device_hid_read_dev(self, addr, data, len, error)) {
		g_prefix_error_literal(error, "fu_novatek_ts_device_hid_read failed: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_novatek_ts_device_hid_write(FuNovatekTsDevice *self,
			       guint32 addr,
			       guint8 *data,
			       gsize len,
			       GError **error)
{
	if (!fu_novatek_ts_device_hid_write_dev(self, addr, data, len, error)) {
		g_prefix_error_literal(error, "fu_novatek_ts_device_hid_write failed: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_novatek_ts_device_gcm_xfer_tx_chunk(FuNovatekTsDevice *self,
				       FuChunk *chk,
				       guint8 *buf,
				       gsize buf_len,
				       guint32 base_addr,
				       GError **error)
{
	gsize len = fu_chunk_get_data_sz(chk);
	guint32 addr = base_addr + fu_chunk_get_address(chk);

	if (!fu_memcpy_safe(buf, buf_len, 0, fu_chunk_get_data(chk), len, 0, len, error)) {
		g_prefix_error_literal(error, "copying tx buffer failed: ");
		return FALSE;
	}
	if (!fu_novatek_ts_device_hid_write(self, addr, buf, len, error)) {
		g_prefix_error_literal(error, "Write tx data error: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_novatek_ts_device_gcm_xfer_rx_chunk(FuNovatekTsDevice *self,
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

	if (!fu_novatek_ts_device_hid_read(self, addr, buf, len, error)) {
		g_prefix_error_literal(error, "Read rx data fail error: ");
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
		g_prefix_error_literal(error, "copying rx buffer failed: ");
		return FALSE;
	}
	return TRUE;
}

typedef struct {
	guint32 addr;
	guint8 *buf;
	guint8 flash_cmd;
} FuNovatekTsCmdIssueCtx;

static gboolean
fu_novatek_ts_device_wait_cmd_issue_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuNovatekTsDevice *self = FU_NOVATEK_TS_DEVICE(device);
	FuNovatekTsCmdIssueCtx *ctx = (FuNovatekTsCmdIssueCtx *)user_data;

	if (!fu_novatek_ts_device_hid_read(self, ctx->addr, ctx->buf, 1, error))
		return FALSE;
	if (ctx->buf[0] == FU_NOVATEK_TS_VALUE_CONST_ZERO)
		return TRUE;
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_BUSY,
		    "write GCM cmd 0x%02X not ready",
		    ctx->flash_cmd);
	return FALSE;
}

static gboolean
fu_novatek_ts_device_retry_busy_cb(FuDevice *device, gpointer user_data, GError **error)
{
	/* prevent excessive log print in busy wait */
	return TRUE;
}

static gboolean
fu_novatek_ts_device_gcm_xfer(FuNovatekTsDevice *self, FuNovatekTsGcmXfer *xfer, GError **error)
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
	g_autoptr(FuStructNovatekTsGcmCmd) st_cmd = NULL;
	FuChunk *chk = NULL;
	FuNovatekTsCmdIssueCtx cmd_issue_ctx = {0};

	flash_cmd_addr = fu_novatek_ts_mem_map_get_flash_cmd_addr(self->mmap);
	flash_cmd_issue_addr = fu_novatek_ts_mem_map_get_flash_cmd_issue_addr(self->mmap);
	rw_flash_data_addr = fu_novatek_ts_mem_map_get_rw_flash_data_addr(self->mmap);

	transfer_len = NVT_TRANSFER_LEN;

	total_buf_size = 64 + xfer->tx_len + xfer->rx_len;
	buf = g_malloc0(total_buf_size);
	if (buf == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
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
			if (!fu_novatek_ts_device_gcm_xfer_tx_chunk(self,
								    chk,
								    buf,
								    total_buf_size,
								    rw_flash_data_addr,
								    error))
				return FALSE;
		}
	}

	memset(buf, 0, total_buf_size);
	st_cmd = fu_struct_novatek_ts_gcm_cmd_new();
	fu_struct_novatek_ts_gcm_cmd_set_flash_cmd(st_cmd, xfer->flash_cmd);
	if (xfer->flash_addr_len > 0)
		fu_struct_novatek_ts_gcm_cmd_set_flash_addr(st_cmd, xfer->flash_addr);
	else
		fu_struct_novatek_ts_gcm_cmd_set_flash_addr(st_cmd, 0);
	write_len = xfer->flash_addr_len + xfer->pem_byte_len + xfer->dummy_byte_len + xfer->tx_len;
	fu_struct_novatek_ts_gcm_cmd_set_write_len(st_cmd, (guint16)write_len);
	fu_struct_novatek_ts_gcm_cmd_set_read_len(st_cmd, (guint16)xfer->rx_len);
	fu_struct_novatek_ts_gcm_cmd_set_flash_checksum(st_cmd, xfer->flash_checksum);
	fu_struct_novatek_ts_gcm_cmd_set_magic(st_cmd, FU_NOVATEK_TS_GCM_CONST_CMD_MAGIC);
	if (!fu_novatek_ts_device_hid_write(self,
					    flash_cmd_addr,
					    st_cmd->buf->data,
					    st_cmd->buf->len,
					    error)) {
		g_prefix_error_literal(error, "Write enter GCM error: ");
		return FALSE;
	}

	cmd_issue_ctx.addr = flash_cmd_issue_addr;
	cmd_issue_ctx.buf = buf;
	cmd_issue_ctx.flash_cmd = xfer->flash_cmd;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_novatek_ts_device_wait_cmd_issue_cb,
				  2000,
				  1,
				  &cmd_issue_ctx,
				  error)) {
		g_prefix_error(error, "write GCM cmd 0x%02X failed: ", xfer->flash_cmd);
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
			if (!fu_novatek_ts_device_gcm_xfer_rx_chunk(self,
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
fu_novatek_ts_device_write_enable_gcm(FuNovatekTsDevice *self, GError **error)
{
	FuNovatekTsGcmXfer xfer = {0};

	xfer.flash_cmd = FU_NOVATEK_TS_FLASH_CMD_CONST_WRITE_ENABLE;
	if (!fu_novatek_ts_device_gcm_xfer(self, &xfer, error)) {
		g_prefix_error_literal(error, "Write Enable failed: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_novatek_ts_device_read_status_gcm(FuNovatekTsDevice *self, guint8 *status, GError **error)
{
	FuNovatekTsGcmXfer xfer = {0};

	xfer.flash_cmd = FU_NOVATEK_TS_FLASH_CMD_CONST_READ_STATUS;
	xfer.rx_len = 1;
	xfer.rx_buf = status;
	if (!fu_novatek_ts_device_gcm_xfer(self, &xfer, error)) {
		g_prefix_error_literal(error, "Read Status GCM fail: ");
		return FALSE;
	}

	return TRUE;
}

typedef struct {
	guint8 *status;
} FuNovatekTsStatusReadyCtx;

static gboolean
fu_novatek_ts_device_wait_status_ready_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuNovatekTsDevice *self = FU_NOVATEK_TS_DEVICE(device);
	FuNovatekTsStatusReadyCtx *ctx = (FuNovatekTsStatusReadyCtx *)user_data;

	if (!fu_novatek_ts_device_read_status_gcm(self, ctx->status, error))
		return FALSE;
	if ((*ctx->status & FU_NOVATEK_TS_MASK_CONST_STATUS_BUSY_MASK) ==
	    FU_NOVATEK_TS_VALUE_CONST_ZERO)
		return TRUE;
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "device busy");
	return FALSE;
}

typedef struct {
	gboolean enable;
} FuNovatekTsSwitchGcmCtx;

static gboolean
fu_novatek_ts_device_switch_gcm_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuNovatekTsDevice *self = FU_NOVATEK_TS_DEVICE(device);
	FuNovatekTsSwitchGcmCtx *ctx = (FuNovatekTsSwitchGcmCtx *)user_data;
	guint8 buf[3] = {0};

	if (ctx->enable) {
		buf[0] = FU_NOVATEK_TS_GCM_CODE_CONST_ENABLE0;
		buf[1] = FU_NOVATEK_TS_GCM_CODE_CONST_ENABLE1;
		buf[2] = FU_NOVATEK_TS_GCM_CODE_CONST_ENABLE2;
	} else {
		buf[0] = FU_NOVATEK_TS_GCM_CODE_CONST_DISABLE0;
		buf[1] = FU_NOVATEK_TS_GCM_CODE_CONST_DISABLE1;
		buf[2] = FU_NOVATEK_TS_GCM_CODE_CONST_DISABLE2;
	}
	if (!fu_novatek_ts_device_hid_write(self,
					    fu_novatek_ts_mem_map_get_gcm_code_addr(self->mmap),
					    buf,
					    3,
					    error))
		return FALSE;
	if (!fu_novatek_ts_device_hid_read(self,
					   fu_novatek_ts_mem_map_get_gcm_flag_addr(self->mmap),
					   buf,
					   1,
					   error))
		return FALSE;
	if (ctx->enable) {
		if ((buf[0] & FU_NOVATEK_TS_MASK_CONST_STATUS_BUSY_MASK) ==
		    FU_NOVATEK_TS_MASK_CONST_STATUS_BUSY_MASK)
			return TRUE;
	} else {
		if ((buf[0] & FU_NOVATEK_TS_MASK_CONST_STATUS_BUSY_MASK) ==
		    FU_NOVATEK_TS_VALUE_CONST_ZERO)
			return TRUE;
	}
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "gcm enable/disable not ready");
	return FALSE;
}

typedef struct {
	guint8 state;
	guint8 last_state;
} FuNovatekTsResetStateCtx;

static gboolean
fu_novatek_ts_device_wait_reset_state_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuNovatekTsDevice *self = FU_NOVATEK_TS_DEVICE(device);
	FuNovatekTsResetStateCtx *ctx = (FuNovatekTsResetStateCtx *)user_data;
	guint8 buf[1] = {0};

	if (!fu_novatek_ts_device_hid_read(
		self,
		fu_novatek_ts_mem_map_get_event_buf_reset_state_addr(self->mmap),
		buf,
		1,
		error))
		return FALSE;
	ctx->last_state = buf[0];
	if ((buf[0] >= ctx->state) && (buf[0] <= FU_NOVATEK_TS_RESET_STATE_RESET_STATE_MAX))
		return TRUE;
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "reset state not ready");
	return FALSE;
}

typedef struct {
	guint8 buf[2];
} FuNovatekTsFwVerCtx;

static gboolean
fu_novatek_ts_device_get_fw_ver_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuNovatekTsDevice *self = FU_NOVATEK_TS_DEVICE(device);
	FuNovatekTsFwVerCtx *ctx = (FuNovatekTsFwVerCtx *)user_data;

	if (!fu_novatek_ts_device_hid_read(
		self,
		fu_novatek_ts_mem_map_get_event_map_fwinfo_addr(self->mmap),
		ctx->buf,
		2,
		error))
		return FALSE;
	if ((guint8)(ctx->buf[0] + ctx->buf[1]) == FU_NOVATEK_TS_VALUE_CONST_INFO_CHECKSUM_OK)
		return TRUE;
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "fw info not ready");
	return FALSE;
}

typedef struct {
	guint32 flash_addr;
	guint16 len;
	guint8 *out;
} FuNovatekTsReadFlashCtx;

static gboolean
fu_novatek_ts_device_read_flash_data_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuNovatekTsDevice *self = FU_NOVATEK_TS_DEVICE(device);
	FuNovatekTsReadFlashCtx *ctx = (FuNovatekTsReadFlashCtx *)user_data;
	FuNovatekTsGcmXfer xfer = {0};
	guint8 buf[2] = {0};
	guint16 rd_checksum = 0;
	guint16 calc = 0;

	calc += (ctx->flash_addr >> 16) & FU_NOVATEK_TS_MASK_CONST_BYTE_MASK;
	calc += (ctx->flash_addr >> 8) & FU_NOVATEK_TS_MASK_CONST_BYTE_MASK;
	calc += (ctx->flash_addr >> 0) & FU_NOVATEK_TS_MASK_CONST_BYTE_MASK;
	calc += (ctx->len >> 8) & FU_NOVATEK_TS_MASK_CONST_BYTE_MASK;
	calc += (ctx->len >> 0) & FU_NOVATEK_TS_MASK_CONST_BYTE_MASK;

	xfer.flash_cmd = self->flash_read_data_cmd;
	xfer.flash_addr = ctx->flash_addr;
	xfer.flash_addr_len = 3;
	xfer.pem_byte_len = self->flash_read_pem_byte_len;
	xfer.dummy_byte_len = self->flash_read_dummy_byte_len;
	xfer.rx_buf = ctx->out;
	xfer.rx_len = ctx->len;

	if (!fu_novatek_ts_device_gcm_xfer(self, &xfer, error))
		return FALSE;
	if (!fu_novatek_ts_device_hid_read(
		self,
		fu_novatek_ts_mem_map_get_read_flash_checksum_addr(self->mmap),
		buf,
		2,
		error))
		return FALSE;

	rd_checksum = (guint16)(buf[1] << 8 | buf[0]);
	calc += fu_sum8(ctx->out, ctx->len);
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
	gboolean allow_retry_once;
	gboolean *retry_needed;
	gboolean checksum_error;
} FuNovatekTsChecksumCtx;

typedef struct {
	FuNovatekTsDevice *self;
	FuChunk *chk;
	guint32 flash_address;
	guint32 flash_cksum_status_addr;
	guint8 *buf;
	gboolean allow_retry_once;
} FuNovatekTsPageProgramCtx;

static gboolean
fu_novatek_ts_device_sector_erase_gcm(FuNovatekTsDevice *self, guint32 flash_addr, GError **error)
{
	FuNovatekTsGcmXfer xfer = {0};

	xfer.flash_cmd = FU_NOVATEK_TS_FLASH_CMD_CONST_SECTOR_ERASE;
	xfer.flash_addr = flash_addr;
	xfer.flash_addr_len = 3;
	if (!fu_novatek_ts_device_gcm_xfer(self, &xfer, error)) {
		g_prefix_error_literal(error, "Sector Erase GCM fail: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_novatek_ts_device_page_program_gcm(FuNovatekTsDevice *self,
				      guint32 flash_addr,
				      guint16 data_len,
				      guint8 *data,
				      GError **error)
{
	FuNovatekTsGcmXfer xfer = {0};
	guint16 checksum = 0;
	guint32 i = 0;

	/* calculate checksum */
	checksum = (flash_addr & FU_NOVATEK_TS_MASK_CONST_BYTE_MASK);
	checksum += ((flash_addr >> 8) & FU_NOVATEK_TS_MASK_CONST_BYTE_MASK);
	checksum += ((flash_addr >> 16) & FU_NOVATEK_TS_MASK_CONST_BYTE_MASK);
	checksum += ((data_len + 3) & FU_NOVATEK_TS_MASK_CONST_BYTE_MASK);
	checksum += (((data_len + 3) >> 8) & FU_NOVATEK_TS_MASK_CONST_BYTE_MASK);
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
	xfer.flash_checksum = checksum & FU_NOVATEK_TS_MASK_CONST_CHECKSUM_MASK;
	if (!fu_novatek_ts_device_gcm_xfer(self, &xfer, error)) {
		g_prefix_error_literal(error, "Page Program GCM fail: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_novatek_ts_device_check_flash_checksum_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuNovatekTsDevice *self = FU_NOVATEK_TS_DEVICE(device);
	FuNovatekTsChecksumCtx *ctx = (FuNovatekTsChecksumCtx *)user_data;

	ctx->buf[0] = FU_NOVATEK_TS_VALUE_CONST_ZERO;
	if (!fu_novatek_ts_device_hid_read(self, ctx->flash_cksum_status_addr, ctx->buf, 1, error))
		return FALSE;
	if (ctx->buf[0] == FU_NOVATEK_TS_CHECKSUM_STATUS_CONST_READY)
		return TRUE;
	if (ctx->buf[0] == FU_NOVATEK_TS_CHECKSUM_STATUS_CONST_ERROR) {
		if (ctx->allow_retry_once) {
			*ctx->retry_needed = TRUE;
			ctx->allow_retry_once = FALSE;
			return TRUE;
		}
		ctx->checksum_error = TRUE;
		return TRUE;
	}
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "checksum not ready");
	return FALSE;
}

static gboolean
fu_novatek_ts_device_check_flash_checksum(FuNovatekTsDevice *self,
					  guint32 flash_cksum_status_addr,
					  guint8 *buf,
					  gboolean allow_retry_once,
					  gboolean *retry_needed,
					  GError **error)
{
	FuNovatekTsChecksumCtx ctx = {0};

	*retry_needed = FALSE;
	ctx.flash_cksum_status_addr = flash_cksum_status_addr;
	ctx.buf = buf;
	ctx.allow_retry_once = allow_retry_once;
	ctx.retry_needed = retry_needed;
	ctx.checksum_error = FALSE;

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_novatek_ts_device_check_flash_checksum_cb,
				  20,
				  1,
				  &ctx,
				  error))
		return FALSE;

	if (ctx.checksum_error) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "check flash checksum status error");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_novatek_ts_device_page_program_retry_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuNovatekTsPageProgramCtx *ctx = (FuNovatekTsPageProgramCtx *)user_data;
	gboolean retry_page = FALSE;

	/* write enable */
	if (!fu_novatek_ts_device_write_enable_gcm(ctx->self, error))
		return FALSE;
	/* write page: FLASH_PAGE_SIZE bytes */
	if (!fu_novatek_ts_device_page_program_gcm(ctx->self,
						   ctx->flash_address,
						   (guint16)fu_chunk_get_data_sz(ctx->chk),
						   (guint8 *)fu_chunk_get_data(ctx->chk),
						   error))
		return FALSE;

	/* check flash checksum status */
	if (!fu_novatek_ts_device_check_flash_checksum(ctx->self,
						       ctx->flash_cksum_status_addr,
						       ctx->buf,
						       ctx->allow_retry_once,
						       &retry_page,
						       error))
		return FALSE;
	if (retry_page) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "page program retry");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_novatek_ts_device_get_checksum_gcm(FuNovatekTsDevice *self,
				      guint32 flash_addr,
				      guint32 data_len,
				      guint16 *checksum,
				      GError **error)
{
	FuNovatekTsGcmXfer xfer = {0};
	guint8 buf[2] = {0};

	xfer.flash_cmd = self->flash_read_data_cmd;
	xfer.flash_addr = flash_addr;
	xfer.flash_addr_len = 3;
	xfer.pem_byte_len = self->flash_read_pem_byte_len;
	xfer.dummy_byte_len = self->flash_read_dummy_byte_len;
	xfer.rx_len = data_len;
	if (!fu_novatek_ts_device_gcm_xfer(self, &xfer, error)) {
		g_prefix_error_literal(error, "Get Checksum GCM fail: ");
		return FALSE;
	}

	buf[0] = FU_NOVATEK_TS_VALUE_CONST_ZERO;
	buf[1] = FU_NOVATEK_TS_VALUE_CONST_ZERO;
	if (!fu_novatek_ts_device_hid_read(
		self,
		fu_novatek_ts_mem_map_get_read_flash_checksum_addr(self->mmap),
		buf,
		2,
		error)) {
		g_prefix_error_literal(error, "Get checksum error: ");
		return FALSE;
	}
	*checksum = (buf[1] << 8) | buf[0];

	return TRUE;
}

static gboolean
fu_novatek_ts_device_switch_gcm(FuNovatekTsDevice *self, guint8 enable, GError **error)
{
	FuNovatekTsSwitchGcmCtx ctx = {0};

	ctx.enable = enable;
	if (!fu_device_retry(FU_DEVICE(self), fu_novatek_ts_device_switch_gcm_cb, 3, &ctx, error)) {
		if (enable)
			g_prefix_error_literal(error, "enable gcm failed: ");
		else
			g_prefix_error_literal(error, "disable gcm failed: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_novatek_ts_device_resume_pd_gcm(FuNovatekTsDevice *self, GError **error)
{
	FuNovatekTsGcmXfer xfer = {0};

	xfer.flash_cmd = FU_NOVATEK_TS_FLASH_CMD_CONST_RESUME_PD;
	if (!fu_novatek_ts_device_gcm_xfer(self, &xfer, error)) {
		g_prefix_error_literal(error, "Resume PD failed: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_novatek_ts_device_erase_flash_gcm(FuNovatekTsDevice *self,
				     guint32 flash_start_addr,
				     guint32 bin_size,
				     GError **error)
{
	guint8 status = 0;
	guint32 i = 0;
	guint32 flash_address = 0;
	guint32 erase_length = 0;
	FuNovatekTsStatusReadyCtx status_ctx = {0};
	g_autoptr(GPtrArray) chunks = NULL;
	FuChunk *chk = NULL;

	if (flash_start_addr % FLASH_SECTOR_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "flash_start_addr should be n*%d",
			    FLASH_SECTOR_SIZE);
		return FALSE;
	}

	erase_length = bin_size;

	/* write enable */
	if (!fu_novatek_ts_device_write_enable_gcm(self, error)) {
		g_prefix_error_literal(error, "Write Enable error: ");
		return FALSE;
	}

	/* read status */
	status_ctx.status = &status;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_novatek_ts_device_wait_status_ready_cb,
				  100,
				  5,
				  &status_ctx,
				  error)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "read status failed, status = 0x%02X",
			    status);
		return FALSE;
	}
	g_debug("read status register ok, status = 0x%02X", status);

	chunks = fu_chunk_array_new(NULL,
				    erase_length,
				    flash_start_addr,
				    FU_CHUNK_PAGESZ_NONE,
				    FLASH_SECTOR_SIZE);
	for (i = 0; i < chunks->len; i++) {
		guint32 page = 0;
		chk = g_ptr_array_index(chunks, i);
		flash_address = (guint32)fu_chunk_get_address(chk);
		page = flash_address / FLASH_SECTOR_SIZE;
		/* write enable */
		if (!fu_novatek_ts_device_write_enable_gcm(self, error)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "Write enable error, page at = %d",
				    page);
			return FALSE;
		}

		/* sector erase */
		if (!fu_novatek_ts_device_sector_erase_gcm(self, flash_address, error)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "Sector erase error, page at = %d",
				    page);
			return FALSE;
		}
		fu_device_sleep(FU_DEVICE(self), 25);

		status_ctx.status = &status;
		if (!fu_device_retry_full(FU_DEVICE(self),
					  fu_novatek_ts_device_wait_status_ready_cb,
					  100,
					  5,
					  &status_ctx,
					  error)) {
			g_prefix_error_literal(error, "wait sector erase timeout: ");
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
fu_novatek_ts_device_ensure_prog_flash_method(FuNovatekTsDevice *self, GError **error)
{
	self->flash_prog_data_cmd = FU_NOVATEK_TS_FLASH_CMD_CONST_PROGRAM_PAGE;
	g_debug("flash program cmd = 0x%02X", self->flash_prog_data_cmd);
	return TRUE;
}

static gboolean
fu_novatek_ts_device_write_flash_gcm(FuNovatekTsDevice *self,
				     const guint8 *bin_data,
				     guint32 bin_size,
				     guint32 flash_start_addr,
				     FuProgress *progress,
				     GError **error)
{
	guint8 buf[1] = {0};
	guint32 flash_address = 0;
	guint32 flash_cksum_status_addr =
	    fu_novatek_ts_mem_map_get_flash_cksum_status_addr(self->mmap);
	guint32 offset = 0;
	guint32 i = 0;
	guint8 status = 0;
	g_autoptr(GPtrArray) chunks = NULL;
	FuChunk *chk = NULL;
	FuNovatekTsStatusReadyCtx status_ctx = {0};
	FuNovatekTsPageProgramCtx page_ctx = {0};

	if (!fu_novatek_ts_device_ensure_prog_flash_method(self, error))
		return FALSE;

	fu_progress_set_id(progress, G_STRLOC);
	chunks = fu_chunk_array_new(bin_data,
				    bin_size,
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
		flash_address = offset + flash_start_addr;
		page_ctx.flash_address = flash_address;

		if (!fu_device_retry_full(FU_DEVICE(self),
					  fu_novatek_ts_device_page_program_retry_cb,
					  2,
					  1,
					  &page_ctx,
					  error)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "page program error, i= %d",
				    i);
			return FALSE;
		}

		/* read status */
		status_ctx.status = &status;
		if (!fu_device_retry_full(FU_DEVICE(self),
					  fu_novatek_ts_device_wait_status_ready_cb,
					  200,
					  1,
					  &status_ctx,
					  error)) {
			g_prefix_error_literal(error, "wait page program timeout: ");
			return FALSE;
		}

		/* show progress */
		fu_progress_set_percentage_full(progress, i + 1, chunks->len);
	}
	fu_progress_set_percentage(progress, 100);
	return TRUE;
}

static gboolean
fu_novatek_ts_device_ensure_read_flash_method(FuNovatekTsDevice *self, GError **error)
{
	self->flash_read_data_cmd = FU_NOVATEK_TS_FLASH_CMD_CONST_READ_DATA;
	self->flash_read_pem_byte_len = 0;
	self->flash_read_dummy_byte_len = 0;
	g_debug("flash read cmd = 0x%02X", self->flash_read_data_cmd);
	return TRUE;
}

static gboolean
fu_novatek_ts_device_verify_flash_gcm(FuNovatekTsDevice *self,
				      const guint8 *bin_data,
				      guint32 bin_size,
				      guint32 flash_start_addr,
				      GError **error)
{
	guint16 write_checksum = 0;
	guint16 read_checksum = 0;
	guint32 flash_addr = 0;
	guint32 data_len = 0;
	guint32 offset = 0;
	guint32 i = 0;
	g_autoptr(GPtrArray) chunks = NULL;
	FuChunk *chk = NULL;

	if (!fu_novatek_ts_device_ensure_read_flash_method(self, error))
		return FALSE;

	chunks = fu_chunk_array_new(bin_data,
				    bin_size,
				    FU_CHUNK_ADDR_OFFSET_NONE,
				    FU_CHUNK_PAGESZ_NONE,
				    FLASH_SECTOR_SIZE);
	for (i = 0; i < chunks->len; i++) {
		chk = g_ptr_array_index(chunks, i);
		offset = fu_chunk_get_address(chk);
		flash_addr = offset + flash_start_addr;
		data_len = fu_chunk_get_data_sz(chk);
		/* calculate write_checksum of each 4KB block */
		write_checksum = (flash_addr & FU_NOVATEK_TS_MASK_CONST_BYTE_MASK);
		write_checksum += ((flash_addr >> 8) & FU_NOVATEK_TS_MASK_CONST_BYTE_MASK);
		write_checksum += ((flash_addr >> 16) & FU_NOVATEK_TS_MASK_CONST_BYTE_MASK);
		write_checksum += ((data_len)&FU_NOVATEK_TS_MASK_CONST_BYTE_MASK);
		write_checksum += (((data_len) >> 8) & FU_NOVATEK_TS_MASK_CONST_BYTE_MASK);
		write_checksum += fu_sum16(&bin_data[offset], data_len);
		write_checksum = ~write_checksum + 1;

		if (!fu_novatek_ts_device_get_checksum_gcm(self,
							   flash_addr,
							   data_len,
							   &read_checksum,
							   error)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "Get Checksum failed, i = %u",
				    i);
			return FALSE;
		}
		if (write_checksum != read_checksum) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "Verify Failed, i = %u, write_checksum = 0x%04X, "
				    "read_checksum = 0x%04X",
				    i,
				    write_checksum,
				    read_checksum);
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
fu_novatek_ts_device_read_flash_mid_did_gcm(FuNovatekTsDevice *self, GError **error)
{
	guint8 buf[3] = {0};
	FuNovatekTsGcmXfer xfer = {0};
	guint8 flash_mid = 0;
	guint16 flash_did = 0;

	xfer.flash_cmd = FU_NOVATEK_TS_FLASH_CMD_CONST_READ_MID_DID;
	xfer.rx_buf = buf;
	xfer.rx_len = 3;
	if (!fu_novatek_ts_device_gcm_xfer(self, &xfer, error)) {
		g_prefix_error_literal(error, "read flash mid did gcm failed: ");
		return FALSE;
	}

	flash_mid = buf[0];
	flash_did = (buf[1] << 8) | buf[2];
	g_debug("flash mid = 0x%02X, did = 0x%04X", flash_mid, flash_did);
	g_debug("read mid did ok");
	return TRUE;
}

static gboolean
fu_novatek_ts_device_update_firmware(FuNovatekTsDevice *self,
				     const guint8 *bin_data,
				     guint32 bin_size,
				     guint32 flash_start_addr,
				     FuProgress *progress,
				     GError **error)
{
	if (!fu_novatek_ts_device_switch_gcm(self, 1, error)) {
		g_prefix_error_literal(error, "enable gcm failed: ");
		return FALSE;
	}

	if (!fu_novatek_ts_device_resume_pd_gcm(self, error)) {
		g_prefix_error_literal(error, "resume pd failed: ");
		return FALSE;
	}

	if (!fu_novatek_ts_device_read_flash_mid_did_gcm(self, error)) {
		g_prefix_error_literal(error, "read flash id failed: ");
		return FALSE;
	}

	if (!fu_novatek_ts_device_erase_flash_gcm(self, flash_start_addr, bin_size, error)) {
		g_prefix_error_literal(error, "erase flash failed: ");
		return FALSE;
	}

	if (!fu_novatek_ts_device_write_flash_gcm(self,
						  bin_data,
						  bin_size,
						  flash_start_addr,
						  progress,
						  error)) {
		g_prefix_error_literal(error, "program flash failed: ");
		return FALSE;
	}

	if (!fu_novatek_ts_device_verify_flash_gcm(self,
						   bin_data,
						   bin_size,
						   flash_start_addr,
						   error)) {
		g_prefix_error_literal(error, "verify flash failed: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_novatek_ts_device_bootloader_reset(FuNovatekTsDevice *self, GError **error)
{
	guint8 buf[1] = {0};

	buf[0] = FU_NOVATEK_TS_CMD_CONST_BOOT_RESET_CMD;
	if (!fu_novatek_ts_device_hid_write(self,
					    fu_novatek_ts_mem_map_get_swrst_sif_addr(self->mmap),
					    buf,
					    1,
					    error))
		return FALSE;
	g_debug("0x%02X to 0x%06X",
		FU_NOVATEK_TS_CMD_CONST_BOOT_RESET_CMD,
		fu_novatek_ts_mem_map_get_swrst_sif_addr(self->mmap));
	fu_device_sleep(FU_DEVICE(self), 235);
	return TRUE;
}

static gboolean
fu_novatek_ts_device_sw_reset_and_idle(FuNovatekTsDevice *self, GError **error)
{
	guint8 buf[1] = {0};

	buf[0] = FU_NOVATEK_TS_CMD_CONST_SW_RESET_CMD;
	if (!fu_novatek_ts_device_hid_write(self,
					    fu_novatek_ts_mem_map_get_swrst_sif_addr(self->mmap),
					    buf,
					    1,
					    error))
		return FALSE;
	g_debug("0x%02X to 0x%06X",
		FU_NOVATEK_TS_CMD_CONST_SW_RESET_CMD,
		fu_novatek_ts_mem_map_get_swrst_sif_addr(self->mmap));
	fu_device_sleep(FU_DEVICE(self), 50);
	return TRUE;
}

static gboolean
fu_novatek_ts_device_stop_crc_reboot(FuNovatekTsDevice *self, GError **error)
{
	guint8 buf[1] = {0};
	guint32 i = 0;

	g_debug("%s (0x%02X to 0x%06X) 20 times",
		__func__,
		FU_NOVATEK_TS_CMD_CONST_STOP_CRC_CMD,
		fu_novatek_ts_mem_map_get_bld_spe_pups_addr(self->mmap));
	for (i = 0; i < 20; i++) {
		buf[0] = FU_NOVATEK_TS_CMD_CONST_STOP_CRC_CMD;
		if (!fu_novatek_ts_device_hid_write(
			self,
			fu_novatek_ts_mem_map_get_bld_spe_pups_addr(self->mmap),
			buf,
			1,
			error))
			return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), 5);
	return TRUE;
}

static gboolean
fu_novatek_ts_device_update_firmware_reset(FuNovatekTsDevice *self,
					   const guint8 *bin_data,
					   guint32 bin_size,
					   guint32 flash_start_addr,
					   FuProgress *progress,
					   GError **error)
{
	if (!fu_novatek_ts_device_bootloader_reset(self, error))
		return FALSE;
	if (!fu_novatek_ts_device_sw_reset_and_idle(self, error))
		return FALSE;
	if (!fu_novatek_ts_device_stop_crc_reboot(self, error))
		return FALSE;

	if (!fu_novatek_ts_device_update_firmware(self,
						  bin_data,
						  bin_size,
						  flash_start_addr,
						  progress,
						  error))
		return FALSE;

	(void)fu_novatek_ts_device_bootloader_reset(self, error);
	return TRUE;
}

G_DEFINE_TYPE(FuNovatekTsDevice, fu_novatek_ts_device, FU_TYPE_HIDRAW_DEVICE)

static gboolean
fu_novatek_ts_device_probe(FuDevice *device, GError **error)
{
	const gchar *subsystem;

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

static gboolean
fu_novatek_ts_device_clear_fw_reset_state(FuNovatekTsDevice *self, GError **error)
{
	guint8 buf[1] = {0};

	if (!fu_novatek_ts_device_hid_write(
		self,
		fu_novatek_ts_mem_map_get_event_buf_reset_state_addr(self->mmap),
		buf,
		1,
		error))
		return FALSE;
	g_debug("0x%02X to 0x%06X",
		FU_NOVATEK_TS_VALUE_CONST_ZERO,
		fu_novatek_ts_mem_map_get_event_buf_reset_state_addr(self->mmap));

	return TRUE;
}

static gboolean
fu_novatek_ts_device_check_fw_reset_state(FuNovatekTsDevice *self, guint8 state, GError **error)
{
	FuNovatekTsResetStateCtx ctx = {0};

	g_info("checking reset state from address 0x%06X for state 0x%02X",
	       fu_novatek_ts_mem_map_get_event_buf_reset_state_addr(self->mmap),
	       state);

	/* first clear */
	if (!fu_novatek_ts_device_clear_fw_reset_state(self, error))
		return FALSE;

	ctx.state = state;
	ctx.last_state = 0;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_novatek_ts_device_wait_reset_state_cb,
				  100,
				  10,
				  &ctx,
				  error)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "error, reset state buf[0] = 0x%02X",
			    ctx.last_state);
		return FALSE;
	}

	g_info("reset state 0x%02X pass", state);
	return TRUE;
}

static gboolean
fu_novatek_ts_device_get_fw_ver(FuNovatekTsDevice *self, GError **error)
{
	FuNovatekTsFwVerCtx ctx = {0};

	if (!fu_device_retry(FU_DEVICE(self),
			     fu_novatek_ts_device_get_fw_ver_cb,
			     10,
			     &ctx,
			     error)) {
		g_set_error(error,
			    FWUPD_ERROR,
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
fu_novatek_ts_device_read_flash_data_gcm(FuNovatekTsDevice *self,
					 guint32 flash_addr,
					 guint16 len,
					 guint8 *out,
					 GError **error)
{
	FuNovatekTsReadFlashCtx ctx = {0};

	if (out == NULL || len == 0)
		return FALSE;
	/* keep this simple; expand later if you want >256 */
	if (len > 256)
		return FALSE;

	ctx.flash_addr = flash_addr;
	ctx.len = len;
	ctx.out = out;
	if (!fu_device_retry(FU_DEVICE(self),
			     fu_novatek_ts_device_read_flash_data_cb,
			     10,
			     &ctx,
			     error)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "flash read checksum mismatch");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_novatek_ts_device_read_flash_pid_gcm(FuNovatekTsDevice *self, GError **error)
{
	guint8 pid_raw[4] = {0};
	char pid_str[5] = {0};
	guint64 pid64 = 0;

	if (self->fmap == NULL || fu_novatek_ts_flash_map_get_flash_pid_addr(self->fmap) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "flash PID address is not set");
		return FALSE;
	}

	if (!fu_novatek_ts_device_switch_gcm(self, 1, error))
		return FALSE;

	if (!fu_novatek_ts_device_resume_pd_gcm(self, error))
		return FALSE;

	if (!fu_novatek_ts_device_read_flash_mid_did_gcm(self, error))
		return FALSE;

	if (!fu_novatek_ts_device_ensure_read_flash_method(self, error))
		return FALSE;

	if (!fu_novatek_ts_device_read_flash_data_gcm(
		self,
		fu_novatek_ts_flash_map_get_flash_pid_addr(self->fmap),
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
		g_set_error(error,
			    FWUPD_ERROR,
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
	if (self->flash_pid == FU_NOVATEK_TS_VALUE_CONST_PID_INVALID_ZERO ||
	    self->flash_pid == FU_NOVATEK_TS_VALUE_CONST_PID_INVALID_ONES) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "pid in flash should not be 0x0000 or 0xFFFF");
		return FALSE;
	}

	g_info("flash_pid = 0x%04X", self->flash_pid);
	return TRUE;
}

static gboolean
fu_novatek_ts_device_setup(FuDevice *device, GError **error)
{
	FuDeviceClass *parent_class;
	guint8 debug_buf[6] = {0};
	FuNovatekTsDevice *self = FU_NOVATEK_TS_DEVICE(device);

	if (!fu_device_open(device, error))
		return FALSE;

	if (!fu_novatek_ts_device_hid_read(self,
					   fu_novatek_ts_mem_map_get_chip_ver_trim_addr(self->mmap),
					   debug_buf,
					   6,
					   error))
		return FALSE;
	g_info("IC chip id: %02X %02X %02X %02X %02X %02X",
	       debug_buf[0],
	       debug_buf[1],
	       debug_buf[2],
	       debug_buf[3],
	       debug_buf[4],
	       debug_buf[5]);

	if (!fu_novatek_ts_device_check_fw_reset_state(
		self,
		FU_NOVATEK_TS_RESET_STATE_RESET_STATE_NORMAL_RUN,
		error) ||
	    !fu_novatek_ts_device_get_fw_ver(self, error)) {
		g_warning("firmware is not normal running before firmware update, proceed");
		self->fw_ver = 0;
		/* allow update to proceed */
		g_clear_error(error);
	}

	fu_device_set_version_raw(device, self->fw_ver);

	if (!fu_novatek_ts_device_bootloader_reset(self, error))
		return FALSE;
	if (!fu_novatek_ts_device_sw_reset_and_idle(self, error))
		return FALSE;
	if (!fu_novatek_ts_device_stop_crc_reboot(self, error))
		return FALSE;

	/* get pid in flash to build GUID */
	if (!fu_novatek_ts_device_read_flash_pid_gcm(self, error)) {
		g_prefix_error_literal(error, "failed to read flash PID: ");
		return FALSE;
	}

	if (!fu_novatek_ts_device_bootloader_reset(self, error))
		return FALSE;

	fu_device_add_instance_u16(device, "VID", NVT_VID_NUM);
	fu_device_add_instance_u16(device, "PJID", self->flash_pid);
	if (!fu_device_build_instance_id(device, error, "HIDRAW", "VID", "PJID", NULL))
		return FALSE;

	parent_class = FU_DEVICE_CLASS(fu_novatek_ts_device_parent_class);
	if (parent_class->setup != NULL)
		return parent_class->setup(device, error);

	return TRUE;
}

static void
fu_novatek_ts_device_init(FuNovatekTsDevice *self)
{
	FuDevice *device = FU_DEVICE(self);
	fu_device_retry_add_recovery(device,
				     FWUPD_ERROR,
				     FWUPD_ERROR_BUSY,
				     fu_novatek_ts_device_retry_busy_cb);

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);

	fu_device_set_vendor(device, "Novatek");
	fu_device_set_name(device, "Touchscreen");
	fu_device_add_protocol(device, "tw.com.novatek.ts");
	fu_device_set_summary(device, "Novatek touchscreen controller");
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_NUMBER);

	self->mmap = fu_novatek_ts_device_mem_map_new_nt36536();
	self->fmap = fu_novatek_ts_device_flash_map_new_nt36536();
}

static gchar *
fu_novatek_ts_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint16((guint16)version_raw, fu_device_get_version_format(device));
}

typedef struct {
	FuProgress *progress;
	guint8 *bin_data;
	guint32 bin_size;
	guint32 flash_start_addr;
} FuNovatekTsUpdateCtx;

static gboolean
fu_novatek_ts_device_update_firmware_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuNovatekTsUpdateCtx *ctx = (FuNovatekTsUpdateCtx *)user_data;
	FuNovatekTsDevice *self = FU_NOVATEK_TS_DEVICE(device);
	if (!fu_novatek_ts_device_update_firmware_reset(self,
							ctx->bin_data,
							ctx->bin_size,
							ctx->flash_start_addr,
							ctx->progress,
							error)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "Update Normal FW Failed");
		return FALSE;
	}

	return TRUE;
}

static FuFirmware *
fu_novatek_ts_device_prepare_firmware(FuDevice *device,
				      GInputStream *stream,
				      FuProgress *progress,
				      FuFirmwareParseFlags flags,
				      GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_novatek_ts_firmware_new();

	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;
	return g_steal_pointer(&firmware);
}

static gboolean
fu_novatek_ts_device_write_firmware(FuDevice *device,
				    FuFirmware *firmware,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuNovatekTsUpdateCtx ctx = {0};
	FuNovatekTsDevice *self = FU_NOVATEK_TS_DEVICE(device);
	g_autofree guint8 *bin_data = NULL;
	guint32 bin_size = 0;
	guint32 flash_start_addr =
	    fu_novatek_ts_flash_map_get_flash_normal_fw_start_addr(self->fmap);

	g_return_val_if_fail(FU_IS_NOVATEK_TS_FIRMWARE(firmware), FALSE);

	if (!fu_novatek_ts_firmware_prepare_bin(
		FU_NOVATEK_TS_FIRMWARE(firmware),
		&bin_data,
		&bin_size,
		flash_start_addr,
		fu_novatek_ts_flash_map_get_flash_max_size(self->fmap),
		error))
		return FALSE;

	if (!fu_device_open(device, error))
		return FALSE;

	ctx.bin_data = bin_data;
	ctx.bin_size = bin_size;
	ctx.flash_start_addr = flash_start_addr;
	ctx.progress = progress;
	if (!fu_device_retry(device, fu_novatek_ts_device_update_firmware_cb, 3, &ctx, error)) {
		return FALSE;
	}

	if (!(fu_novatek_ts_device_check_fw_reset_state(
		  self,
		  FU_NOVATEK_TS_RESET_STATE_RESET_STATE_NORMAL_RUN,
		  error) &&
	      fu_novatek_ts_device_get_fw_ver(self, error))) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "firmware is not normal running after firmware update");
		self->fw_ver = 0;
		fu_device_set_version_raw(device, self->fw_ver);
		return FALSE;
	}

	fu_device_set_version_raw(device, self->fw_ver);
	return TRUE;
}

static void
fu_novatek_ts_device_finalize(GObject *object)
{
	FuNovatekTsDevice *self = FU_NOVATEK_TS_DEVICE(object);
	g_clear_pointer(&self->mmap, fu_novatek_ts_mem_map_unref);
	g_clear_pointer(&self->fmap, fu_novatek_ts_flash_map_unref);

	G_OBJECT_CLASS(fu_novatek_ts_device_parent_class)->finalize(object);
}

static void
fu_novatek_ts_device_class_init(FuNovatekTsDeviceClass *klass)
{
	FuDeviceClass *device_class;
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_novatek_ts_device_finalize;
	device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_novatek_ts_device_probe;
	device_class->setup = fu_novatek_ts_device_setup;
	device_class->prepare_firmware = fu_novatek_ts_device_prepare_firmware;
	device_class->write_firmware = fu_novatek_ts_device_write_firmware;
	device_class->convert_version = fu_novatek_ts_device_convert_version;
}
