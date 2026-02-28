/*
 * Copyright 2026 Novatekmsp <novatekmsp@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-novatek-ts-device.h"
#include "fu-novatek-ts-firmware.h"
#include "fu-novatek-ts-struct.h"

struct _FuNovatekTsDevice {
	FuHidrawDevice parent_instance;
	FuCfiDevice *cfi_device;

	/* mmap */
	guint32 chip_ver_trim_addr;
	guint32 swrst_sif_addr;
	guint32 event_buf_cmd_addr;
	guint32 event_buf_hs_sub_cmd_addr;
	guint32 event_buf_reset_state_addr;
	guint32 event_map_fwinfo_addr;
	guint32 read_flash_checksum_addr;
	guint32 rw_flash_data_addr;
	guint32 enb_casc_addr;
	guint32 hid_i2c_eng_addr;
	guint32 gcm_code_addr;
	guint32 gcm_flag_addr;
	guint32 flash_cmd_addr;
	guint32 flash_cmd_issue_addr;
	guint32 flash_cksum_status_addr;
	guint32 bld_spe_pups_addr;

	/* pmap */
	guint32 flash_start_addr; /* FIXME to string */
	guint32 flash_pid_addr;
};

G_DEFINE_TYPE(FuNovatekTsDevice, fu_novatek_ts_device, FU_TYPE_HIDRAW_DEVICE)

#define NVT_TS_REPORT_ID 0x0B
#define NVT_TRANSFER_LEN 256
#define FLASH_PAGE_SIZE	 256
#define FLASH_SECTOR_SIZE (1024 * 4)

#define FU_NOVATEK_TS_CODE_ENABLE  0x55FFAA
#define FU_NOVATEK_TS_CODE_DISABLE 0xAA55FF

static void
fu_novatek_ts_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuNovatekTsDevice *self = FU_NOVATEK_TS_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "ChipVerTrimAddr", self->chip_ver_trim_addr);
	fwupd_codec_string_append_hex(str, idt, "SwrstSifAddr", self->swrst_sif_addr);
	fwupd_codec_string_append_hex(str, idt, "EventBufCmdAddr", self->event_buf_cmd_addr);
	fwupd_codec_string_append_hex(str,
				      idt,
				      "EventBufHsSubCmdAddr",
				      self->event_buf_hs_sub_cmd_addr);
	fwupd_codec_string_append_hex(str,
				      idt,
				      "EventBufResetStateAddr",
				      self->event_buf_reset_state_addr);
	fwupd_codec_string_append_hex(str, idt, "EventMapFwinfoAddr", self->event_map_fwinfo_addr);
	fwupd_codec_string_append_hex(str,
				      idt,
				      "ReadFlashChecksumAddr",
				      self->read_flash_checksum_addr);
	fwupd_codec_string_append_hex(str, idt, "RwFlashDataAddr", self->rw_flash_data_addr);
	fwupd_codec_string_append_hex(str, idt, "EnbCascAddr", self->enb_casc_addr);
	fwupd_codec_string_append_hex(str, idt, "HidI2cEngAddr", self->hid_i2c_eng_addr);
	fwupd_codec_string_append_hex(str, idt, "GcmCodeAddr", self->gcm_code_addr);
	fwupd_codec_string_append_hex(str, idt, "GcmFlagAddr", self->gcm_flag_addr);
	fwupd_codec_string_append_hex(str, idt, "FlashCmdAddr", self->flash_cmd_addr);
	fwupd_codec_string_append_hex(str, idt, "FlashCmdIssueAddr", self->flash_cmd_issue_addr);
	fwupd_codec_string_append_hex(str,
				      idt,
				      "FlashCksumStatusAddr",
				      self->flash_cksum_status_addr);
	fwupd_codec_string_append_hex(str, idt, "BldSpePupsAddr", self->bld_spe_pups_addr);
	fwupd_codec_string_append_hex(str, idt, "FlashStartAddr", self->flash_start_addr);
	fwupd_codec_string_append_hex(str, idt, "FlashPidAddr", self->flash_pid_addr);
	fu_device_add_string(FU_DEVICE(self->cfi_device), idt + 1, str);
}

typedef struct {
	guint8 flash_cmd;
	guint32 flash_addr;
	guint16 flash_checksum;
	guint8 flash_addr_len;
	guint8 pem_byte_len;
	guint8 dummy_byte_len;
	guint8 *tx_buf;
	guint16 tx_len;
	guint8 *rx_buf;
	guint16 rx_len;
} FuNovatekTsGcmXfer;

static gboolean
fu_novatek_ts_device_hid_read_dev(FuNovatekTsDevice *self,
				  guint32 addr,
				  guint8 *buf,
				  gsize bufsz,
				  GError **error)
{
	g_autoptr(GByteArray) buf_get = g_byte_array_new();
	g_autoptr(FuStructNovatekTsHidReadReq) st_req = NULL;

	g_return_val_if_fail(bufsz != 0, FALSE);
	g_debug("read addr=0x%08x bufsz=%zu", addr, bufsz);

	/* set feature */
	st_req = fu_struct_novatek_ts_hid_read_req_new();
	fu_struct_novatek_ts_hid_read_req_set_i2c_hid_eng_report_id(st_req, NVT_TS_REPORT_ID);
	fu_struct_novatek_ts_hid_read_req_set_i2c_eng_addr(st_req, self->hid_i2c_eng_addr);
	fu_struct_novatek_ts_hid_read_req_set_target_addr(st_req, addr);
	fu_struct_novatek_ts_hid_read_req_set_len(st_req, (guint16)(bufsz + 3));
	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  st_req->buf->data,
					  st_req->buf->len,
					  FU_IOCTL_FLAG_RETRY,
					  error)) {
		g_prefix_error_literal(error, "set feature failed: ");
		return FALSE;
	}

	/* get feature */
	fu_byte_array_set_size(buf_get, bufsz + 1, 0x0);
	buf_get->data[0] = NVT_TS_REPORT_ID;
	if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(self),
					  buf_get->data,
					  buf_get->len,
					  FU_IOCTL_FLAG_RETRY,
					  error)) {
		g_prefix_error_literal(error, "get feature failed: ");
		return FALSE;
	}
	if (!fu_memcpy_safe(buf,
			    bufsz,
			    0, /* dst */
			    buf_get->data,
			    buf_get->len,
			    0x1, /* src */
			    bufsz,
			    error)) {
		g_prefix_error_literal(error, "copying feature buf failed: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_novatek_ts_device_hid_write_dev(FuNovatekTsDevice *self,
				   guint32 addr,
				   guint8 *buf,
				   gsize bufsz,
				   GError **error)
{
	gsize write_len;
	g_autoptr(GByteArray) buf_set = g_byte_array_new();
	g_autoptr(FuStructNovatekTsHidWriteHdr) st_hdr = NULL;

	g_return_val_if_fail(bufsz != 0, FALSE);

	g_debug("write addr=0x%08x bufsz=%zu, buf:", addr, bufsz);
	fu_dump_raw(G_LOG_DOMAIN, "write-buf", buf, bufsz);

	write_len = bufsz + 5;
	fu_byte_array_set_size(buf_set, write_len + 1, 0x0);

	st_hdr = fu_struct_novatek_ts_hid_write_hdr_new();
	fu_struct_novatek_ts_hid_write_hdr_set_i2c_hid_eng_report_id(st_hdr, NVT_TS_REPORT_ID);
	fu_struct_novatek_ts_hid_write_hdr_set_write_len(st_hdr, (guint16)write_len);
	fu_struct_novatek_ts_hid_write_hdr_set_target_addr(st_hdr, addr);

	if (!fu_memcpy_safe(buf_set->data,
			    buf_set->len,
			    0, /* dst */
			    st_hdr->buf->data,
			    st_hdr->buf->len,
			    0, /* src */
			    st_hdr->buf->len,
			    error)) {
		g_prefix_error_literal(error, "copying write header failed: ");
		return FALSE;
	}

	if (!fu_memcpy_safe(buf_set->data,
			    buf_set->len,
			    6, /* dst */
			    buf,
			    bufsz,
			    0, /* src */
			    bufsz,
			    error)) {
		g_prefix_error_literal(error, "copying write buffer failed: ");
		return FALSE;
	}

	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  buf_set->data,
					  buf_set->len,
					  FU_IOCTL_FLAG_RETRY,
					  error)) {
		g_prefix_error_literal(error, "set feature failed: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_novatek_ts_device_hid_read(FuNovatekTsDevice *self,
			      guint32 addr,
			      guint8 *buf,
			      gsize bufsz,
			      GError **error)
{
	if (!fu_novatek_ts_device_hid_read_dev(self, addr, buf, bufsz, error)) {
		g_prefix_error_literal(error, "HID read failed: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_novatek_ts_device_hid_write(FuNovatekTsDevice *self,
			       guint32 addr,
			       guint8 *buf,
			       gsize bufsz,
			       GError **error)
{
	if (!fu_novatek_ts_device_hid_write_dev(self, addr, buf, bufsz, error)) {
		g_prefix_error_literal(error, "HID write failed: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_novatek_ts_device_gcm_xfer_tx_chunk(FuNovatekTsDevice *self,
				       FuChunk *chk,
				       guint8 *buf,
				       gsize bufsz,
				       guint32 base_addr,
				       GError **error)
{
	if (!fu_memcpy_safe(buf,
			    bufsz,
			    0, /* dst */
			    fu_chunk_get_data(chk),
			    fu_chunk_get_data_sz(chk),
			    0, /* src */
			    fu_chunk_get_data_sz(chk),
			    error)) {
		g_prefix_error_literal(error, "copying tx buffer failed: ");
		return FALSE;
	}
	if (!fu_novatek_ts_device_hid_write(self,
					    base_addr + fu_chunk_get_address(chk),
					    buf,
					    fu_chunk_get_data_sz(chk),
					    error)) {
		g_prefix_error_literal(error, "write tx buf failed: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_novatek_ts_device_gcm_xfer_rx_chunk(FuNovatekTsDevice *self,
				       FuChunk *chk,
				       guint8 *buf,
				       gsize bufsz,
				       guint32 base_addr,
				       guint8 *rx_buf,
				       gsize rx_len,
				       GError **error)
{
	gsize len = fu_chunk_get_data_sz(chk);
	guint32 addr = base_addr + fu_chunk_get_address(chk);

	if (!fu_novatek_ts_device_hid_read(self, addr, buf, len, error)) {
		g_prefix_error_literal(error, "read rx buf fail error: ");
		return FALSE;
	}
	if (!fu_memcpy_safe(rx_buf,
			    rx_len,
			    fu_chunk_get_address(chk), /* dst */
			    buf,
			    bufsz,
			    0, /* src */
			    len,
			    error)) {
		g_prefix_error_literal(error, "copying rx buffer failed: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

typedef struct {
	guint32 addr;
	GByteArray *buf;
	guint8 flash_cmd;
} FuNovatekTsCmdIssueCtx;

static gboolean
fu_novatek_ts_device_wait_cmd_issue_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuNovatekTsDevice *self = FU_NOVATEK_TS_DEVICE(device);
	FuNovatekTsCmdIssueCtx *ctx = (FuNovatekTsCmdIssueCtx *)user_data;

	if (!fu_novatek_ts_device_hid_read(self, ctx->addr, ctx->buf->data, 1, error))
		return FALSE;
	if (ctx->buf->data[0] != 0x00) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_BUSY,
			    "write gcm cmd 0x%02x not ready",
			    ctx->flash_cmd);
		return FALSE;
	}
	return TRUE;
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
	gint32 write_len = 0;
	g_autoptr(FuStructNovatekTsGcmCmd) st_cmd = fu_struct_novatek_ts_gcm_cmd_new();
	g_autoptr(GByteArray) buf = g_byte_array_new();
	FuNovatekTsCmdIssueCtx cmd_issue_ctx = {.addr = self->flash_cmd_issue_addr,
						.flash_cmd = xfer->flash_cmd,
						.buf = buf};

	fu_byte_array_set_size(buf, 64 + xfer->tx_len + xfer->rx_len, 0x0);

	if (xfer->tx_len > 0 && xfer->tx_buf != NULL) {
		g_autoptr(GPtrArray) chunks_tx = NULL;
		chunks_tx = fu_chunk_array_new(xfer->tx_buf,
					       xfer->tx_len,
					       FU_CHUNK_ADDR_OFFSET_NONE,
					       FU_CHUNK_PAGESZ_NONE,
					       NVT_TRANSFER_LEN);
		for (guint i = 0; i < chunks_tx->len; i++) {
			FuChunk *chk = g_ptr_array_index(chunks_tx, i);
			if (!fu_novatek_ts_device_gcm_xfer_tx_chunk(self,
								    chk,
								    buf->data,
								    buf->len,
								    self->rw_flash_data_addr,
								    error))
				return FALSE;
		}
	}

	memset(buf->data, 0, buf->len);
	fu_struct_novatek_ts_gcm_cmd_set_flash_cmd(st_cmd, xfer->flash_cmd);
	if (xfer->flash_addr_len > 0)
		fu_struct_novatek_ts_gcm_cmd_set_flash_addr(st_cmd, xfer->flash_addr);
	else
		fu_struct_novatek_ts_gcm_cmd_set_flash_addr(st_cmd, 0);
	write_len = xfer->flash_addr_len + xfer->pem_byte_len + xfer->dummy_byte_len + xfer->tx_len;
	fu_struct_novatek_ts_gcm_cmd_set_write_len(st_cmd, (guint16)write_len);
	fu_struct_novatek_ts_gcm_cmd_set_read_len(st_cmd, (guint16)xfer->rx_len);
	fu_struct_novatek_ts_gcm_cmd_set_flash_checksum(st_cmd, xfer->flash_checksum);
	if (!fu_novatek_ts_device_hid_write(self,
					    self->flash_cmd_addr,
					    st_cmd->buf->data,
					    st_cmd->buf->len,
					    error)) {
		g_prefix_error_literal(error, "write enter gcm error: ");
		return FALSE;
	}
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_novatek_ts_device_wait_cmd_issue_cb,
				  2000,
				  1,
				  &cmd_issue_ctx,
				  error)) {
		g_prefix_error(error, "write gcm cmd 0x%02x failed: ", xfer->flash_cmd);
		return FALSE;
	}

	if (xfer->rx_len > 0 && xfer->rx_buf != NULL) {
		g_autoptr(GPtrArray) chunks_rx = NULL;

		memset(buf->data, 0, xfer->rx_len);
		chunks_rx = fu_chunk_array_new(NULL,
					       xfer->rx_len,
					       FU_CHUNK_ADDR_OFFSET_NONE,
					       FU_CHUNK_PAGESZ_NONE,
					       NVT_TRANSFER_LEN);
		for (guint i = 0; i < chunks_rx->len; i++) {
			FuChunk *chk = g_ptr_array_index(chunks_rx, i);
			if (!fu_novatek_ts_device_gcm_xfer_rx_chunk(self,
								    chk,
								    buf->data,
								    buf->len,
								    self->rw_flash_data_addr,
								    xfer->rx_buf,
								    xfer->rx_len,
								    error))
				return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_novatek_ts_device_gcm_write_enable(FuNovatekTsDevice *self, GError **error)
{
	FuNovatekTsGcmXfer xfer = {0};

	if (!fu_cfi_device_get_cmd(self->cfi_device,
				   FU_CFI_DEVICE_CMD_WRITE_EN,
				   &xfer.flash_cmd,
				   error))
		return FALSE;
	if (!fu_novatek_ts_device_gcm_xfer(self, &xfer, error)) {
		g_prefix_error_literal(error, "write enable failed: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_novatek_ts_device_gcm_read_status(FuNovatekTsDevice *self, guint8 *status, GError **error)
{
	FuNovatekTsGcmXfer xfer = {0};

	if (!fu_cfi_device_get_cmd(self->cfi_device,
				   FU_CFI_DEVICE_CMD_READ_STATUS,
				   &xfer.flash_cmd,
				   error))
		return FALSE;
	xfer.rx_len = 1;
	xfer.rx_buf = status;
	if (!fu_novatek_ts_device_gcm_xfer(self, &xfer, error)) {
		g_prefix_error_literal(error, "read status gcm fail: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_novatek_ts_device_wait_status_ready_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuNovatekTsDevice *self = FU_NOVATEK_TS_DEVICE(device);
	guint8 *status = (guint8 *)user_data;

	if (!fu_novatek_ts_device_gcm_read_status(self, status, error))
		return FALSE;
	if ((*status & 0x01) != 0x00) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "device busy");
		return FALSE;
	}

	/* success */
	return TRUE;
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

	if (!fu_novatek_ts_device_hid_read(self, self->event_buf_reset_state_addr, buf, 1, error))
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

	if (!fu_novatek_ts_device_hid_read(self, self->event_map_fwinfo_addr, ctx->buf, 2, error))
		return FALSE;
	if ((guint8)(ctx->buf[0] + ctx->buf[1]) == 0xFF)
		return TRUE;
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "fw info not ready");
	return FALSE;
}

typedef struct {
	guint32 flash_addr;
	guint16 bufsz;
	guint8 *buf;
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

	calc += (ctx->flash_addr >> 16) & 0xFF;
	calc += (ctx->flash_addr >> 8) & 0xFF;
	calc += (ctx->flash_addr >> 0) & 0xFF;
	calc += (ctx->bufsz >> 8) & 0xFF;
	calc += (ctx->bufsz >> 0) & 0xFF;

	if (!fu_cfi_device_get_cmd(self->cfi_device,
				   FU_CFI_DEVICE_CMD_READ_DATA,
				   &xfer.flash_cmd,
				   error))
		return FALSE;
	xfer.flash_addr = ctx->flash_addr;
	xfer.flash_addr_len = 3;
	xfer.pem_byte_len = 0;	 /* TODO: get from FuCfiDevice if needed */
	xfer.dummy_byte_len = 0; /* TODO: get from FuCfiDevice if needed */
	xfer.rx_buf = ctx->buf;
	xfer.rx_len = ctx->bufsz;

	if (!fu_novatek_ts_device_gcm_xfer(self, &xfer, error))
		return FALSE;
	if (!fu_novatek_ts_device_hid_read(self, self->read_flash_checksum_addr, buf, 2, error))
		return FALSE;

	rd_checksum = (guint16)(buf[1] << 8 | buf[0]);
	calc += fu_sum8(ctx->buf, ctx->bufsz);
	/* 0xFFFF - sum + 1 */
	calc = 65535 - calc + 1;
	if (rd_checksum != calc) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_BUSY,
				    "flash read checksum mismatch");
		return FALSE;
	}

	/* success */
	return TRUE;
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
fu_novatek_ts_device_gcm_sector_erase(FuNovatekTsDevice *self, guint32 flash_addr, GError **error)
{
	FuNovatekTsGcmXfer xfer = {0};

	if (!fu_cfi_device_get_cmd(self->cfi_device,
				   FU_CFI_DEVICE_CMD_SECTOR_ERASE,
				   &xfer.flash_cmd,
				   error))
		return FALSE;
	xfer.flash_addr = flash_addr;
	xfer.flash_addr_len = 3;
	if (!fu_novatek_ts_device_gcm_xfer(self, &xfer, error)) {
		g_prefix_error_literal(error, "sector erase gcm fail: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_novatek_ts_device_page_program_gcm(FuNovatekTsDevice *self,
				      guint32 flash_addr,
				      guint16 bufsz,
				      guint8 *buf,
				      GError **error)
{
	FuNovatekTsGcmXfer xfer = {0};
	guint16 checksum = 0;

	/* calculate checksum */
	checksum = (flash_addr & 0xFF);
	checksum += ((flash_addr >> 8) & 0xFF);
	checksum += ((flash_addr >> 16) & 0xFF);
	checksum += ((bufsz + 3) & 0xFF);
	checksum += (((bufsz + 3) >> 8) & 0xFF);
	for (guint i = 0; i < bufsz; i++)
		checksum += buf[i];
	checksum = ~checksum + 1;

	/* prepare gcm command transfer */
	if (!fu_cfi_device_get_cmd(self->cfi_device,
				   FU_CFI_DEVICE_CMD_PAGE_PROG,
				   &xfer.flash_cmd,
				   error))
		return FALSE;
	xfer.flash_addr = flash_addr;
	xfer.flash_addr_len = 3;
	xfer.tx_buf = buf;
	xfer.tx_len = bufsz;
	xfer.flash_checksum = checksum & 0xFFFF;
	if (!fu_novatek_ts_device_gcm_xfer(self, &xfer, error)) {
		g_prefix_error_literal(error, "page program gcm fail: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_novatek_ts_device_check_flash_checksum_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuNovatekTsDevice *self = FU_NOVATEK_TS_DEVICE(device);
	FuNovatekTsChecksumCtx *ctx = (FuNovatekTsChecksumCtx *)user_data;

	ctx->buf[0] = 0x00;
	if (!fu_novatek_ts_device_hid_read(self, ctx->flash_cksum_status_addr, ctx->buf, 1, error))
		return FALSE;
	if (ctx->buf[0] == FU_NOVATEK_TS_CHECKSUM_STATUS_READY)
		return TRUE;
	if (ctx->buf[0] == FU_NOVATEK_TS_CHECKSUM_STATUS_ERROR) {
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
	if (!fu_novatek_ts_device_gcm_write_enable(ctx->self, error))
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
fu_novatek_ts_device_gcm_get_checksum(FuNovatekTsDevice *self,
				      guint32 flash_addr,
				      guint32 bufsz,
				      guint16 *checksum,
				      GError **error)
{
	FuNovatekTsGcmXfer xfer = {0};
	guint8 buf[2] = {0};

	if (!fu_cfi_device_get_cmd(self->cfi_device,
				   FU_CFI_DEVICE_CMD_READ_DATA,
				   &xfer.flash_cmd,
				   error))
		return FALSE;
	xfer.flash_addr = flash_addr;
	xfer.flash_addr_len = 3;
	xfer.pem_byte_len = 0;	 /* TODO: get from FuCfiDevice if needed */
	xfer.dummy_byte_len = 0; /* TODO: get from FuCfiDevice if needed */
	xfer.rx_len = bufsz;
	if (!fu_novatek_ts_device_gcm_xfer(self, &xfer, error)) {
		g_prefix_error_literal(error, "get checksum gcm fail: ");
		return FALSE;
	}

	if (!fu_novatek_ts_device_hid_read(self, self->read_flash_checksum_addr, buf, 2, error)) {
		g_prefix_error_literal(error, "get checksum error: ");
		return FALSE;
	}
	*checksum = fu_memread_uint16(buf, G_LITTLE_ENDIAN);

	return TRUE;
}

static gboolean
fu_novatek_ts_device_gcm_switch_enable_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuNovatekTsDevice *self = FU_NOVATEK_TS_DEVICE(device);
	guint8 buf[3] = {0};

	fu_memwrite_uint24(buf, FU_NOVATEK_TS_CODE_ENABLE, G_BIG_ENDIAN);
	if (!fu_novatek_ts_device_hid_write(self, self->gcm_code_addr, buf, 3, error))
		return FALSE;
	if (!fu_novatek_ts_device_hid_read(self, self->gcm_flag_addr, buf, 1, error))
		return FALSE;
	if ((buf[0] & 0x01) != 0x01) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "enable not ready");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_novatek_ts_device_gcm_switch_enable(FuNovatekTsDevice *self, GError **error)
{
	return fu_device_retry(FU_DEVICE(self),
			       fu_novatek_ts_device_gcm_switch_enable_cb,
			       3,
			       NULL,
			       error);
}

static gboolean
fu_novatek_ts_device_gcm_resume_pd(FuNovatekTsDevice *self, GError **error)
{
	FuNovatekTsGcmXfer xfer = {0};

	if (!fu_cfi_device_get_cmd(self->cfi_device,
				   FU_CFI_DEVICE_CMD_RELEASE_PD,
				   &xfer.flash_cmd,
				   error))
		return FALSE;
	if (!fu_novatek_ts_device_gcm_xfer(self, &xfer, error)) {
		g_prefix_error_literal(error, "resume pd failed: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_novatek_ts_device_gcm_erase_flash(FuNovatekTsDevice *self,
				     guint32 bin_size,
				     GError **error)
{
	guint8 status = 0;
	g_autoptr(GPtrArray) chunks = NULL;

	if (self->flash_start_addr % FLASH_SECTOR_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "flash_start_addr should be n*%d",
			    FLASH_SECTOR_SIZE);
		return FALSE;
	}

	/* write enable */
	if (!fu_novatek_ts_device_gcm_write_enable(self, error)) {
		g_prefix_error_literal(error, "write enable error: ");
		return FALSE;
	}

	/* read status */
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_novatek_ts_device_wait_status_ready_cb,
				  100,
				  5,
				  &status,
				  error)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "read status failed, status = 0x%02x",
			    status);
		return FALSE;
	}
	g_debug("read status register ok, status = 0x%02x", status);

	chunks = fu_chunk_array_new(NULL,
				    bin_size,
				    self->flash_start_addr,
				    FU_CHUNK_PAGESZ_NONE,
				    FLASH_SECTOR_SIZE);
	for (guint32 i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		guint32 flash_address = (guint32)fu_chunk_get_address(chk);
		guint32 page = flash_address / FLASH_SECTOR_SIZE;

		/* write enable */
		if (!fu_novatek_ts_device_gcm_write_enable(self, error)) {
			g_prefix_error(error, "write enable error, page %u: ", page);
			return FALSE;
		}

		/* sector erase */
		if (!fu_novatek_ts_device_gcm_sector_erase(self, flash_address, error)) {
			g_prefix_error(error, "sector erase error, page %u: ", page);
			return FALSE;
		}
		fu_device_sleep(FU_DEVICE(self), 25);

		if (!fu_device_retry_full(FU_DEVICE(self),
					  fu_novatek_ts_device_wait_status_ready_cb,
					  100,
					  5,
					  &status,
					  error)) {
			g_prefix_error_literal(error, "wait sector erase timeout: ");
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_novatek_ts_device_gcm_write_flash(FuNovatekTsDevice *self,
				     GBytes *blob,
				     FuProgress *progress,
				     GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;

	chunks = fu_chunk_array_new_from_bytes(blob,
					       FU_CHUNK_ADDR_OFFSET_NONE,
					       FU_CHUNK_PAGESZ_NONE,
					       FLASH_PAGE_SIZE);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		guint8 buf[1] = {0};
		guint32 flash_cksum_status_addr = self->flash_cksum_status_addr;
		guint8 status = 0;
		FuNovatekTsPageProgramCtx page_ctx = {0};
		g_autoptr(FuChunk) chk = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		page_ctx.self = self;
		page_ctx.chk = chk;
		page_ctx.flash_cksum_status_addr = flash_cksum_status_addr;
		page_ctx.buf = buf;
		page_ctx.allow_retry_once = TRUE;
		page_ctx.flash_address = fu_chunk_get_address(chk) + self->flash_start_addr;

		if (!fu_device_retry_full(FU_DEVICE(self),
					  fu_novatek_ts_device_page_program_retry_cb,
					  2,
					  1,
					  &page_ctx,
					  error)) {
			g_prefix_error(error, "page program error, i=%u: ", i);
			return FALSE;
		}

		/* read status */
		if (!fu_device_retry_full(FU_DEVICE(self),
					  fu_novatek_ts_device_wait_status_ready_cb,
					  200,
					  1,
					  &status,
					  error)) {
			g_prefix_error_literal(error, "wait page program timeout: ");
			return FALSE;
		}

		/* show progress */
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_novatek_ts_device_gcm_verify_flash(FuNovatekTsDevice *self,
				      GBytes *blob,
				      FuProgress *progress,
				      GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;

	chunks = fu_chunk_array_new_from_bytes(blob,
					       FU_CHUNK_ADDR_OFFSET_NONE,
					       FU_CHUNK_PAGESZ_NONE,
					       FLASH_SECTOR_SIZE);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		guint16 write_checksum = 0;
		guint16 read_checksum = 0;
		guint32 bufsz;
		guint32 flash_addr;
		const guint8 *buf;
		g_autoptr(FuChunk) chk = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		flash_addr = fu_chunk_get_address(chk) + self->flash_start_addr;
		buf = fu_chunk_get_data(chk);
		bufsz = fu_chunk_get_data_sz(chk);

		/* calculate write_checksum of each 4KB block */
		write_checksum = (flash_addr & 0xFF);
		write_checksum += ((flash_addr >> 8) & 0xFF);
		write_checksum += ((flash_addr >> 16) & 0xFF);
		write_checksum += ((bufsz) & 0xFF);
		write_checksum += (((bufsz) >> 8) & 0xFF);
		write_checksum += fu_sum16(buf, bufsz);
		write_checksum = ~write_checksum + 1;
		if (!fu_novatek_ts_device_gcm_get_checksum(self,
							   flash_addr,
							   bufsz,
							   &read_checksum,
							   error)) {
			g_prefix_error(error, "get checksum failed, i = %u: ", i);
			return FALSE;
		}
		if (write_checksum != read_checksum) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "verify failed, i = %u, write_checksum = 0x%04x, "
				    "read_checksum = 0x%04x",
				    i,
				    write_checksum,
				    read_checksum);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_novatek_ts_device_gcm_read_flash_mid_did(FuNovatekTsDevice *self, GError **error)
{
	guint8 buf[3] = {0};
	FuNovatekTsGcmXfer xfer = {
	    .rx_buf = buf,
	    .rx_len = sizeof(buf),
	};
	g_autofree gchar *flash_id = NULL;

	if (!fu_cfi_device_get_cmd(self->cfi_device,
				   FU_CFI_DEVICE_CMD_READ_ID,
				   &xfer.flash_cmd,
				   error))
		return FALSE;
	if (!fu_novatek_ts_device_gcm_xfer(self, &xfer, error)) {
		g_prefix_error_literal(error, "read flash mid did gcm failed: ");
		return FALSE;
	}

	/* get updated constants */
	flash_id = g_strdup_printf("%02X%02X%02X", buf[0], buf[1], buf[2]);
	fu_cfi_device_set_flash_id(self->cfi_device, flash_id);
	return fu_device_setup(FU_DEVICE(self->cfi_device), error);
}

static gboolean
fu_novatek_ts_device_bootloader_reset(FuNovatekTsDevice *self, GError **error)
{
	guint8 buf[1] = {FU_NOVATEK_TS_CMD_BOOT_RESET};

	if (!fu_novatek_ts_device_hid_write(self, self->swrst_sif_addr, buf, sizeof(buf), error))
		return FALSE;

	/* success */
	fu_device_sleep(FU_DEVICE(self), 235);
	return TRUE;
}

static gboolean
fu_novatek_ts_device_sw_reset_and_idle(FuNovatekTsDevice *self, GError **error)
{
	guint8 buf[1] = {FU_NOVATEK_TS_CMD_SW_RESET};

	if (!fu_novatek_ts_device_hid_write(self, self->swrst_sif_addr, buf, sizeof(buf), error))
		return FALSE;

	/* success */
	fu_device_sleep(FU_DEVICE(self), 50);
	return TRUE;
}

static gboolean
fu_novatek_ts_device_stop_crc_reboot(FuNovatekTsDevice *self, GError **error)
{
	for (guint i = 0; i < 20; i++) {
		guint8 buf[] = {FU_NOVATEK_TS_CMD_STOP_CRC};
		if (!fu_novatek_ts_device_hid_write(self,
						    self->bld_spe_pups_addr,
						    buf,
						    sizeof(buf),
						    error))
			return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), 5);
	return TRUE;
}

static gboolean
fu_novatek_ts_device_update_firmware_reset(FuNovatekTsDevice *self,
					   GBytes *blob,
					   FuProgress *progress,
					   GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 5, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, NULL);

	/* reset */
	if (!fu_novatek_ts_device_bootloader_reset(self, error))
		return FALSE;
	if (!fu_novatek_ts_device_sw_reset_and_idle(self, error))
		return FALSE;
	if (!fu_novatek_ts_device_stop_crc_reboot(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* update */
	if (!fu_novatek_ts_device_gcm_switch_enable(self, error)) {
		g_prefix_error_literal(error, "enable gcm failed: ");
		return FALSE;
	}
	if (!fu_novatek_ts_device_gcm_resume_pd(self, error)) {
		g_prefix_error_literal(error, "resume pd failed: ");
		return FALSE;
	}
	if (!fu_novatek_ts_device_gcm_read_flash_mid_did(self, error)) {
		g_prefix_error_literal(error, "read flash id failed: ");
		return FALSE;
	}
	if (!fu_novatek_ts_device_gcm_erase_flash(self,
						  g_bytes_get_size(blob),
						  error)) {
		g_prefix_error_literal(error, "erase flash failed: ");
		return FALSE;
	}
	if (!fu_novatek_ts_device_gcm_write_flash(self,
						  blob,
						  fu_progress_get_child(progress),
						  error)) {
		g_prefix_error_literal(error, "program flash failed: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* verify */
	if (!fu_novatek_ts_device_gcm_verify_flash(self,
						   blob,
						   fu_progress_get_child(progress),
						   error)) {
		g_prefix_error_literal(error, "verify flash failed: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* reset */
	if (!fu_novatek_ts_device_bootloader_reset(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

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
	return fu_novatek_ts_device_hid_write(self,
					      self->event_buf_reset_state_addr,
					      buf,
					      sizeof(buf),
					      error);
}

static gboolean
fu_novatek_ts_device_check_fw_reset_state(FuNovatekTsDevice *self, guint8 state, GError **error)
{
	FuNovatekTsResetStateCtx ctx = {.state = state, .last_state = 0};

	g_info("checking reset state from address 0x%06X for state 0x%02x",
	       self->event_buf_reset_state_addr,
	       state);

	/* first clear */
	if (!fu_novatek_ts_device_clear_fw_reset_state(self, error))
		return FALSE;

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_novatek_ts_device_wait_reset_state_cb,
				  100,
				  10,
				  &ctx,
				  error)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "error, reset state buf[0] = 0x%02x",
			    ctx.last_state);
		return FALSE;
	}

	g_info("reset state 0x%02x pass", state);
	return TRUE;
}

static gboolean
fu_novatek_ts_device_ensure_fw_ver(FuNovatekTsDevice *self, GError **error)
{
	FuNovatekTsFwVerCtx ctx = {0};

	if (!fu_device_retry(FU_DEVICE(self),
			     fu_novatek_ts_device_get_fw_ver_cb,
			     10,
			     &ctx,
			     error)) {
		g_prefix_error(error,
			       "fw info is broken, fw_ver=0x%02x, ~fw_ver=0x%02x: ",
			       ctx.buf[0],
			       ctx.buf[1]);
		return FALSE;
	}

	/* success */
	fu_device_set_version_raw(FU_DEVICE(self), ctx.buf[0]);
	return TRUE;
}

static gboolean
fu_novatek_ts_device_gcm_read_flash_data(FuNovatekTsDevice *self,
					 guint32 flash_addr,
					 guint8 *buf,
					 guint16 bufsz,
					 GError **error)
{
	FuNovatekTsReadFlashCtx ctx = {
	    .flash_addr = flash_addr,
	    .bufsz = bufsz,
	    .buf = buf,
	};

	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(bufsz != 0, FALSE);

	/* keep this simple; expand later if you want >256 */
	if (bufsz > 256) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "read length > 256 not supported");
		return FALSE;
	}
	if (!fu_device_retry(FU_DEVICE(self),
			     fu_novatek_ts_device_read_flash_data_cb,
			     10,
			     &ctx,
			     error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_novatek_ts_device_ensure_flash_pid(FuNovatekTsDevice *self, GError **error)
{
	gchar pid_str[5] = {0};
	guint64 flash_pid = 0;
	guint8 pid_raw[4] = {0};

	if (self->flash_pid_addr == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "flash PID address is not set");
		return FALSE;
	}
	if (!fu_novatek_ts_device_gcm_switch_enable(self, error))
		return FALSE;
	if (!fu_novatek_ts_device_gcm_resume_pd(self, error))
		return FALSE;
	if (!fu_novatek_ts_device_gcm_read_flash_mid_did(self, error))
		return FALSE;
	if (!fu_novatek_ts_device_gcm_read_flash_data(self,
						      self->flash_pid_addr,
						      pid_raw,
						      sizeof(pid_raw),
						      error))
		return FALSE;

	/* pid_str: [2][3][0][1] */
	pid_str[0] = (char)pid_raw[2];
	pid_str[1] = (char)pid_raw[3];
	pid_str[2] = (char)pid_raw[0];
	pid_str[3] = (char)pid_raw[1];
	pid_str[4] = '\0';
	if (!fu_strtoull(pid_str, &flash_pid, 0, G_MAXUINT16, FU_INTEGER_BASE_16, error)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "invalid pid read from flash: '%s' (%02x %02x %02x %02x): ",
			    pid_str,
			    pid_raw[0],
			    pid_raw[1],
			    pid_raw[2],
			    pid_raw[3]);
		return FALSE;
	}
	if (flash_pid == 0x0000 || flash_pid == G_MAXUINT16) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "pid in flash should not be 0x0000 or 0xFFFF");
		return FALSE;
	}
	fu_device_add_instance_u16(FU_DEVICE(self), "PJID", (guint16)flash_pid);
	return fu_device_build_instance_id(FU_DEVICE(self), error, "HIDRAW", "VEN", "PJID", NULL);
}

static gboolean
fu_novatek_ts_device_setup(FuDevice *device, GError **error)
{
	FuNovatekTsDevice *self = FU_NOVATEK_TS_DEVICE(device);
	FuDeviceClass *parent_class = FU_DEVICE_CLASS(fu_novatek_ts_device_parent_class);
	guint8 debug_buf[6] = {0};
	g_autoptr(GError) error_local = NULL;

	if (!fu_novatek_ts_device_hid_read(self,
					   self->chip_ver_trim_addr,
					   debug_buf,
					   sizeof(debug_buf),
					   error))
		return FALSE;
	g_info("IC chip id: %02x %02x %02x %02x %02x %02x",
	       debug_buf[0],
	       debug_buf[1],
	       debug_buf[2],
	       debug_buf[3],
	       debug_buf[4],
	       debug_buf[5]);

	if (!fu_novatek_ts_device_check_fw_reset_state(
		self,
		FU_NOVATEK_TS_RESET_STATE_RESET_STATE_NORMAL_RUN,
		&error_local)) {
		g_warning("firmware is not normal running: %s", error_local->message);
	}
	if (!fu_novatek_ts_device_ensure_fw_ver(self, error))
		return FALSE;

	if (!fu_novatek_ts_device_bootloader_reset(self, error))
		return FALSE;
	if (!fu_novatek_ts_device_sw_reset_and_idle(self, error))
		return FALSE;
	if (!fu_novatek_ts_device_stop_crc_reboot(self, error))
		return FALSE;

	/* get pid in flash to build GUID */
	if (!fu_novatek_ts_device_ensure_flash_pid(self, error)) {
		g_prefix_error_literal(error, "failed to read flash PID: ");
		return FALSE;
	}

	/* back to runtime */
	if (!fu_novatek_ts_device_bootloader_reset(self, error))
		return FALSE;

	/* FuHidrawDevice */
	return parent_class->setup(device, error);
}

static gchar *
fu_novatek_ts_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint16((guint16)version_raw, fu_device_get_version_format(device));
}

typedef struct {
	FuProgress *progress;
	GBytes *blob;
} FuNovatekTsUpdateCtx;

static gboolean
fu_novatek_ts_device_update_firmware_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuNovatekTsUpdateCtx *ctx = (FuNovatekTsUpdateCtx *)user_data;
	FuNovatekTsDevice *self = FU_NOVATEK_TS_DEVICE(device);
	if (!fu_novatek_ts_device_update_firmware_reset(self, ctx->blob, ctx->progress, error)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "update normal fw failed");
		return FALSE;
	}

	return TRUE;
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
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) blob_offset = NULL;

	g_return_val_if_fail(FU_IS_NOVATEK_TS_FIRMWARE(firmware), FALSE);

	/* always use FLASH_NORMAL start (0x2000) */
	if (self->flash_start_addr < FLASH_SECTOR_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "flash start addr too low: 0x%X",
			    self->flash_start_addr);
		return FALSE;
	}

	/* drop leading header region so data starts at self->flash_start_addr */
	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL)
		return FALSE;
	blob_offset = fu_bytes_new_offset(blob,
					  self->flash_start_addr,
					  g_bytes_get_size(blob) - self->flash_start_addr,
					  error);
	if (blob_offset == NULL)
		return FALSE;

	ctx.blob = blob_offset;
	ctx.progress = progress;
	if (!fu_device_retry(device, fu_novatek_ts_device_update_firmware_cb, 3, &ctx, error))
		return FALSE;

	if (!fu_novatek_ts_device_check_fw_reset_state(
		self,
		FU_NOVATEK_TS_RESET_STATE_RESET_STATE_NORMAL_RUN,
		error)) {
		g_prefix_error_literal(error, "not normal running after firmware update: ");
		return FALSE;
	}
	if (!fu_novatek_ts_device_ensure_fw_ver(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_novatek_ts_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_novatek_ts_device_init(FuNovatekTsDevice *self)
{
	/* these can be set from quirks in the future if required */
	fu_device_set_firmware_size_max(FU_DEVICE(self), 0x3E000);
	self->flash_start_addr = 0x2000;
	self->flash_pid_addr = 0x3F004;

	self->chip_ver_trim_addr = FU_NOVATEK_TS_MEM_MAP_REG_CHIP_VER_TRIM_ADDR;
	self->swrst_sif_addr = FU_NOVATEK_TS_MEM_MAP_REG_SWRST_SIF_ADDR;
	self->event_buf_cmd_addr = FU_NOVATEK_TS_MEM_MAP_REG_EVENT_BUF_CMD_ADDR;
	self->event_buf_hs_sub_cmd_addr = FU_NOVATEK_TS_MEM_MAP_REG_EVENT_BUF_HS_SUB_CMD_ADDR;
	self->event_buf_reset_state_addr = FU_NOVATEK_TS_MEM_MAP_REG_EVENT_BUF_RESET_STATE_ADDR;
	self->event_map_fwinfo_addr = FU_NOVATEK_TS_MEM_MAP_REG_EVENT_MAP_FWINFO_ADDR;
	self->read_flash_checksum_addr = FU_NOVATEK_TS_MEM_MAP_REG_READ_FLASH_CHECKSUM_ADDR;
	self->rw_flash_data_addr = FU_NOVATEK_TS_MEM_MAP_REG_RW_FLASH_DATA_ADDR;
	self->enb_casc_addr = FU_NOVATEK_TS_MEM_MAP_REG_ENB_CASC_ADDR;
	self->hid_i2c_eng_addr = FU_NOVATEK_TS_MEM_MAP_REG_HID_I2C_ENG_ADDR;
	self->gcm_code_addr = FU_NOVATEK_TS_MEM_MAP_REG_GCM_CODE_ADDR;
	self->gcm_flag_addr = FU_NOVATEK_TS_MEM_MAP_REG_GCM_FLAG_ADDR;
	self->flash_cmd_addr = FU_NOVATEK_TS_MEM_MAP_REG_FLASH_CMD_ADDR;
	self->flash_cmd_issue_addr = FU_NOVATEK_TS_MEM_MAP_REG_FLASH_CMD_ISSUE_ADDR;
	self->flash_cksum_status_addr = FU_NOVATEK_TS_MEM_MAP_REG_FLASH_CKSUM_STATUS_ADDR;
	self->bld_spe_pups_addr = FU_NOVATEK_TS_MEM_MAP_REG_BLD_SPE_PUPS_ADDR;

	fu_device_retry_add_recovery(FU_DEVICE(self),
				     FWUPD_ERROR,
				     FWUPD_ERROR_BUSY,
				     fu_novatek_ts_device_retry_busy_cb);

	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);

	fu_device_set_name(FU_DEVICE(self), "Touchscreen");
	fu_device_add_protocol(FU_DEVICE(self), "tw.com.novatek.ts");
	fu_device_set_summary(FU_DEVICE(self), "Novatek touchscreen controller");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_NUMBER);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_NOVATEK_TS_FIRMWARE);
}

static void
fu_novatek_ts_device_constructed(GObject *object)
{
	FuNovatekTsDevice *self = FU_NOVATEK_TS_DEVICE(object);
	self->cfi_device = fu_cfi_device_new(FU_DEVICE(self), NULL);
}

static void
fu_novatek_ts_device_finalize(GObject *object)
{
	FuNovatekTsDevice *self = FU_NOVATEK_TS_DEVICE(object);
	g_object_unref(self->cfi_device);
	G_OBJECT_CLASS(fu_novatek_ts_device_parent_class)->finalize(object);
}

static void
fu_novatek_ts_device_class_init(FuNovatekTsDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_novatek_ts_device_finalize;
	object_class->constructed = fu_novatek_ts_device_constructed;
	device_class->probe = fu_novatek_ts_device_probe;
	device_class->setup = fu_novatek_ts_device_setup;
	device_class->write_firmware = fu_novatek_ts_device_write_firmware;
	device_class->convert_version = fu_novatek_ts_device_convert_version;
	device_class->to_string = fu_novatek_ts_device_to_string;
	device_class->set_progress = fu_novatek_ts_device_set_progress;
}
