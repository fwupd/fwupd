/*
 * Copyright 2019 GOODIX
 * Copyright 2026 Sunwinon Electronics Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR BSD-3-Clause
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-sunwinon-util-dfu-master.h"

#define HID_REPORT_DATA_LEN 480
/* fw load addr shall align to sector size */
#define FLASH_OP_SECTOR_SIZE	0x1000
#define FW_PATTERN_VALUE	0x4744
#define DFU_SIGN_LEN		856
#define PATTERN_DEADBEEF	0xDEADBEEF
#define PATTERN_SIGN		0x4E474953 /* "SIGN" */
#define PATTERN_DEADBEEF_OFFSET 48
#define PATTERN_SIGN_OFFSET	PATTERN_DEADBEEF_OFFSET + 72
#define ONCE_SIZE		464

struct FuSwDfuMaster {
	const guint8 *fw;
	gsize fw_sz;
	FuDevice *device;
};

typedef struct {
	/* peripheral bootloader information */
	FuSunwinonDfuBootInfo boot_info;
	/* FW image information about the firmware to be upgraded */
	FuSunwinonDfuImageInfo now_img_info;
	/* image information in peripheral APP Info area */
	FuSunwinonDfuImageInfo app_info;
	guint32 dfu_save_addr;
	guint32 file_check_sum;
	gboolean security_mode;
	FuSunwinonFwType fw_type;
} FuDfuInnerState;

typedef struct {
	FuSunwinonDfuCmd cmd_type;
	guint16 data_len; /* inout */
	guint8 *data;
	guint16 check_sum;
} FuDfuReceiveFrame;

static gboolean
fu_sunwinon_util_dfu_master_fast_mode_not_supported(GError **error)
{
	/* fast mode use different flash procedure, but no device support it right now */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "sunwinon-hid: no device support fast dfu mode right now");
	return FALSE;
}

static gboolean
fu_sunwinon_util_dfu_master_check_fw_available(FuSwDfuMaster *self, GError **error)
{
	if (self->fw == NULL || self->fw_sz < DFU_IMAGE_INFO_TAIL_SIZE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "sunwinon-hid: invalid firmware blob");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_sunwinon_util_dfu_master_dfu_get_img_info(FuSwDfuMaster *self,
					     FuSunwinonDfuImageInfo *image_info,
					     GError **error)
{
	if (!fu_sunwinon_util_dfu_master_check_fw_available(self, error))
		return FALSE;
	/* for now, all fw are unsigned unencrypted images */
	if (!fu_memcpy_safe((guint8 *)image_info,
			    sizeof(FuSunwinonDfuImageInfo),
			    0,
			    self->fw,
			    self->fw_sz,
			    self->fw_sz - DFU_IMAGE_INFO_TAIL_SIZE,
			    sizeof(FuSunwinonDfuImageInfo),
			    error))
		return FALSE;
	if (image_info->pattern != FW_PATTERN_VALUE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "sunwinon-hid: invalid firmware pattern");
		return FALSE;
	}
	if (image_info->boot_info.load_addr % FLASH_OP_SECTOR_SIZE != 0U) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "sunwinon-hid: firmware load address not aligned");
		return FALSE;
	}
	if (image_info->boot_info.bin_size + DFU_IMAGE_INFO_TAIL_SIZE > (guint32)self->fw_sz) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "sunwinon-hid: firmware size mismatch");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_sunwinon_util_dfu_master_pre_update_check(FuSwDfuMaster *self,
					     FuDfuInnerState *inner_state,
					     GError **error)
{
	gsize tail_size = 0;
	guint32 fw_pattern_deadbeef = 0;
	guint32 fw_pattern_sign = 0;
	guint32 bootloader_end = 0;
	guint32 bank0_fw_end = 0;

	g_return_val_if_fail(inner_state != NULL, FALSE);

	tail_size = DFU_IMAGE_INFO_TAIL_SIZE;

	/* check if fw is signed */
	if (inner_state->security_mode) {
		tail_size += DFU_SIGN_LEN;
		inner_state->fw_type = FU_SUNWINON_FW_TYPE_SIGNED;
		g_debug("signed firmware (security mode)");
	} else if (self->fw_sz >= (inner_state->now_img_info.boot_info.bin_size +
				   DFU_IMAGE_INFO_TAIL_SIZE + DFU_SIGN_LEN)) {
		/* check fw sign pattern to see if it is signed */
		if (!fu_memcpy_safe((guint8 *)&fw_pattern_deadbeef,
				    sizeof(fw_pattern_deadbeef),
				    0,
				    self->fw,
				    self->fw_sz,
				    inner_state->now_img_info.boot_info.bin_size +
					PATTERN_DEADBEEF_OFFSET,
				    sizeof(fw_pattern_deadbeef),
				    error))
			return FALSE;
		if (!fu_memcpy_safe((guint8 *)&fw_pattern_sign,
				    sizeof(fw_pattern_sign),
				    0,
				    self->fw,
				    self->fw_sz,
				    inner_state->now_img_info.boot_info.bin_size +
					PATTERN_SIGN_OFFSET,
				    sizeof(fw_pattern_sign),
				    error))
			return FALSE;
		if ((fw_pattern_deadbeef == PATTERN_DEADBEEF) &&
		    (fw_pattern_sign == PATTERN_SIGN)) {
			tail_size += DFU_SIGN_LEN;
			inner_state->fw_type = FU_SUNWINON_FW_TYPE_SIGNED;
			g_debug("signed firmware (sign pattern found)");
		}
	} else {
		/* fw is unsigned */
		g_debug("unsigned firmware");
		inner_state->fw_type = FU_SUNWINON_FW_TYPE_NORMAL;
	}

	/* check if the new fw is correctly packed */
	if (self->fw_sz != inner_state->now_img_info.boot_info.bin_size + tail_size) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "sunwinon-hid: firmware size mismatch");
		return FALSE;
	}

	/* check if the new fw would overlap with bootloader */
	bootloader_end =
	    inner_state->boot_info.load_addr + inner_state->boot_info.bin_size + tail_size;
	if (inner_state->dfu_save_addr <= bootloader_end) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "sunwinon-hid: firmware save address 0x%x overlaps with bootloader "
			    "(bootloader_end: 0x%x)",
			    inner_state->dfu_save_addr,
			    bootloader_end);
		return FALSE;
	}

	/* check if the new fw would overlap with current application (bank0) */
	bank0_fw_end = inner_state->app_info.boot_info.load_addr +
		       inner_state->app_info.boot_info.bin_size + tail_size;
	if (inner_state->dfu_save_addr <= bank0_fw_end) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "sunwinon-hid: firmware save address 0x%x overlaps with current app "
			    "(bank0_fw_end: 0x%x)",
			    inner_state->dfu_save_addr,
			    bank0_fw_end);
		return FALSE;
	}

	g_debug("firmware pre-update check passed");

	return TRUE;
}

static void
fu_sunwinon_util_dfu_master_wait(FuSwDfuMaster *self, guint32 ms)
{
	fu_device_sleep(FU_DEVICE(self->device), ms);
}

static gboolean
fu_sunwinon_util_dfu_master_recv_sum_check(FuDfuReceiveFrame *recv_frame)
{
	guint16 calc_check_sum = 0;
	guint8 cmd_type_l = (guint8)(recv_frame->cmd_type & 0xFF);
	guint8 cmd_type_h = (guint8)((recv_frame->cmd_type >> 8) & 0xFF);
	guint8 data_len_l = (guint8)(recv_frame->data_len & 0xFF);
	guint8 data_len_h = (guint8)((recv_frame->data_len >> 8) & 0xFF);

	calc_check_sum += cmd_type_l;
	calc_check_sum += cmd_type_h;
	calc_check_sum += data_len_l;
	calc_check_sum += data_len_h;

	for (guint16 i = 0; i < recv_frame->data_len; i++) {
		calc_check_sum += recv_frame->data[i];
	}

	return (calc_check_sum == recv_frame->check_sum);
}

static guint16
fu_sunwinon_util_dfu_master_cal_send_check_sum(FuSunwinonDfuCmd cmd_type,
					       const guint8 *data,
					       guint16 len)
{
	guint16 check_sum = 0;
	check_sum += (guint8)(cmd_type & 0xFF);
	check_sum += (guint8)((cmd_type >> 8) & 0xFF);
	check_sum += (guint8)(len & 0xFF);
	check_sum += (guint8)((len >> 8) & 0xFF);
	if (data != NULL) {
		for (guint16 i = 0; i < len; i++) {
			check_sum += data[i];
		}
	}
	return check_sum;
}

static gboolean
fu_sunwinon_util_dfu_master_send_frame(FuSwDfuMaster *self,
				       const guint8 *p_data,
				       guint16 len,
				       FuSunwinonDfuCmd cmd_type,
				       GError **error)
{
	gsize total_len = FU_STRUCT_SUNWINON_DFU_FRAME_HEADER_SIZE + len + 2; /* +2 for check_sum */
	guint16 check_sum = 0;
	g_autoptr(FuStructSunwinonHidOut) st_out = fu_struct_sunwinon_hid_out_new();
	g_autoptr(FuStructSunwinonDfuFrameHeader) st_dfu_header =
	    fu_struct_sunwinon_dfu_frame_header_new();

	if (total_len > HID_REPORT_DATA_LEN) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "sunwinon-hid: data length exceeds maximum report size");
		return FALSE;
	}

	fu_struct_sunwinon_dfu_frame_header_set_cmd_type(st_dfu_header, cmd_type);
	fu_struct_sunwinon_dfu_frame_header_set_data_len(st_dfu_header, len);

	if (!fu_struct_sunwinon_hid_out_set_dfu_header(st_out, st_dfu_header, error))
		return FALSE;

	if (p_data != NULL && !fu_struct_sunwinon_hid_out_set_data(st_out, p_data, len, error))
		return FALSE;

	fu_struct_sunwinon_hid_out_set_data_len(st_out, total_len);

	/* write check_sum at the very end of the whole data package */
	check_sum = fu_sunwinon_util_dfu_master_cal_send_check_sum(cmd_type, p_data, len);
	fu_memwrite_uint16(st_out->buf->data + FU_STRUCT_SUNWINON_HID_OUT_OFFSET_DATA + len,
			   check_sum,
			   G_LITTLE_ENDIAN);

	if (!fu_hidraw_device_set_report(FU_HIDRAW_DEVICE(self->device),
					 st_out->buf->data,
					 st_out->buf->len,
					 FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
					 error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_sunwinon_util_dfu_master_recv_frame(FuSwDfuMaster *self,
				       FuDfuReceiveFrame *recv_frame,
				       GError **error)
{
	gsize check_sum_offset = 0;
	gsize buf_len = 0;
	g_autoptr(FuStructSunwinonHidIn) st_in = NULL;
	g_autoptr(FuStructSunwinonDfuFrameHeader) st_dfu_header = NULL;

	g_return_val_if_fail(recv_frame != NULL, FALSE);
	g_return_val_if_fail(recv_frame->data != NULL, FALSE);

	buf_len = recv_frame->data_len;

	st_in = fu_struct_sunwinon_hid_in_new();
	memset(st_in->buf->data, 0, st_in->buf->len);
	/* may not get a full length report here */
	if (!fu_hidraw_device_get_report(FU_HIDRAW_DEVICE(self->device),
					 st_in->buf->data,
					 st_in->buf->len,
					 FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
					 error)) {
		if (!g_error_matches(*error, FWUPD_ERROR, FWUPD_ERROR_READ))
			return FALSE;
		g_clear_error(error);
	}

	fu_dump_raw(G_LOG_DOMAIN, "raw input report", st_in->buf->data, st_in->buf->len);

	if (!fu_struct_sunwinon_hid_in_validate(st_in->buf->data, st_in->buf->len, 0, error))
		return FALSE;

	st_dfu_header = fu_struct_sunwinon_hid_in_get_dfu_header(st_in);
	recv_frame->cmd_type = fu_struct_sunwinon_dfu_frame_header_get_cmd_type(st_dfu_header);
	recv_frame->data_len = fu_struct_sunwinon_dfu_frame_header_get_data_len(st_dfu_header);
	check_sum_offset = FU_STRUCT_SUNWINON_HID_IN_OFFSET_DATA + recv_frame->data_len;
	recv_frame->check_sum =
	    fu_memread_uint16(st_in->buf->data + check_sum_offset, G_LITTLE_ENDIAN);

	if (buf_len > 0 && !fu_memcpy_safe(recv_frame->data,
					   buf_len,
					   0,
					   st_in->buf->data + FU_STRUCT_SUNWINON_HID_IN_OFFSET_DATA,
					   recv_frame->data_len,
					   0,
					   recv_frame->data_len,
					   error))
		return FALSE;

	if (!fu_sunwinon_util_dfu_master_recv_sum_check(recv_frame)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "sunwinon-hid: received frame check sum mismatch");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_sunwinon_util_dfu_master_check_recv_cmd_type(FuSwDfuMaster *self,
						FuDfuReceiveFrame *recv_frame,
						FuSunwinonDfuCmd expected_cmd,
						GError **error)
{
	if (recv_frame->cmd_type != expected_cmd) {
		g_set_error(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INTERNAL,
		    "sunwinon-hid: unexpected command type in response, expected %s, got %s",
		    fu_sunwinon_dfu_cmd_to_string(expected_cmd),
		    fu_sunwinon_dfu_cmd_to_string(recv_frame->cmd_type));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_sunwinon_util_dfu_master_emit_ack_failure(FuSunwinonDfuCmd cmd_type, GError **error)
{
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INTERNAL,
		    "sunwinon-hid: command %s not acked successfully",
		    fu_sunwinon_dfu_cmd_to_string(cmd_type));
	return FALSE;
}

static gboolean
fu_sunwinon_util_dfu_master_plain_ack_recv(FuSwDfuMaster *self,
					   FuSunwinonDfuCmd expected_cmd,
					   GError **error)
{
	FuDfuReceiveFrame recv_frame = {0};
	guint8 ack_byte = 0;

	recv_frame.data = &ack_byte;
	recv_frame.data_len = 1;

	if (!fu_sunwinon_util_dfu_master_recv_frame(self, &recv_frame, error))
		return FALSE;
	if (!fu_sunwinon_util_dfu_master_check_recv_cmd_type(self,
							     &recv_frame,
							     expected_cmd,
							     error))
		return FALSE;
	if (ack_byte != FU_SUNWINON_DFU_ACK_SUCCESS)
		return fu_sunwinon_util_dfu_master_emit_ack_failure(expected_cmd, error);
	return TRUE;
}

static gboolean
fu_sunwinon_util_dfu_master_get_info_cmd(FuSwDfuMaster *self, GError **error)
{
	FuDfuReceiveFrame recv_frame = {0};
	g_autoptr(FuStructSunwinonDfuRspGetInfo) st_get_info = NULL;

	g_debug("GetInfo");

	if (!fu_sunwinon_util_dfu_master_send_frame(self,
						    NULL,
						    0,
						    FU_SUNWINON_DFU_CMD_GET_INFO,
						    error))
		return FALSE;

	st_get_info = fu_struct_sunwinon_dfu_rsp_get_info_new();
	recv_frame.data = st_get_info->buf->data;
	recv_frame.data_len = st_get_info->buf->len;

	if (!fu_sunwinon_util_dfu_master_recv_frame(self, &recv_frame, error))
		return FALSE;
	if (!fu_sunwinon_util_dfu_master_check_recv_cmd_type(self,
							     &recv_frame,
							     FU_SUNWINON_DFU_CMD_GET_INFO,
							     error))
		return FALSE;
	if (!fu_struct_sunwinon_dfu_rsp_get_info_validate(st_get_info->buf->data,
							  st_get_info->buf->len,
							  0,
							  error))
		return FALSE;
	if (fu_struct_sunwinon_dfu_rsp_get_info_get_ack_status(st_get_info) !=
	    FU_SUNWINON_DFU_ACK_SUCCESS)
		return fu_sunwinon_util_dfu_master_emit_ack_failure(FU_SUNWINON_DFU_CMD_GET_INFO,
								    error);
	return TRUE;
}

static gboolean
fu_sunwinon_util_dfu_master_system_info_cmd(FuSwDfuMaster *self,
					    FuSunwinonDfuBootInfo *boot_info,
					    gboolean *security_mode,
					    GError **error)
{
	FuDfuReceiveFrame recv_frame = {0};
	g_autoptr(FuStructSunwinonDfuPayloadSystemInfo) st_sys_info_payload = NULL;
	g_autoptr(FuStructSunwinonDfuRspSystemInfo) st_sys_info = NULL;

	g_return_val_if_fail(boot_info != NULL, FALSE);
	g_return_val_if_fail(security_mode != NULL, FALSE);

	g_debug("SystemInfo");

	st_sys_info_payload = fu_struct_sunwinon_dfu_payload_system_info_new();
	fu_struct_sunwinon_dfu_payload_system_info_set_flash_start_addr(
	    st_sys_info_payload,
	    FU_SUNWINON_DFU_CONFIG_PERIPHERAL_FLASH_START_ADDR);
	if (!fu_sunwinon_util_dfu_master_send_frame(self,
						    st_sys_info_payload->buf->data,
						    st_sys_info_payload->buf->len,
						    FU_SUNWINON_DFU_CMD_SYSTEM_INFO,
						    error))
		return FALSE;

	st_sys_info = fu_struct_sunwinon_dfu_rsp_system_info_new();
	recv_frame.data = st_sys_info->buf->data;
	recv_frame.data_len = st_sys_info->buf->len;

	if (!fu_sunwinon_util_dfu_master_recv_frame(self, &recv_frame, error))
		return FALSE;
	if (!fu_sunwinon_util_dfu_master_check_recv_cmd_type(self,
							     &recv_frame,
							     FU_SUNWINON_DFU_CMD_SYSTEM_INFO,
							     error))
		return FALSE;
	if (!fu_struct_sunwinon_dfu_rsp_system_info_validate(st_sys_info->buf->data,
							     st_sys_info->buf->len,
							     0,
							     error))
		return FALSE;
	if (fu_struct_sunwinon_dfu_rsp_system_info_get_ack_status(st_sys_info) !=
	    FU_SUNWINON_DFU_ACK_SUCCESS)
		return fu_sunwinon_util_dfu_master_emit_ack_failure(FU_SUNWINON_DFU_CMD_SYSTEM_INFO,
								    error);
	if (fu_struct_sunwinon_dfu_rsp_system_info_get_start_addr(st_sys_info) !=
	    FU_SUNWINON_DFU_CONFIG_PERIPHERAL_FLASH_START_ADDR) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "sunwinon-hid: peripheral flash start address mismatch");
		return FALSE;
	}

	if (!fu_memcpy_safe(
		(guint8 *)boot_info,
		sizeof(FuSunwinonDfuBootInfo),
		0,
		fu_struct_sunwinon_dfu_rsp_system_info_get_system_info_raw(st_sys_info, NULL),
		FU_STRUCT_SUNWINON_DFU_RSP_SYSTEM_INFO_N_ELEMENTS_SYSTEM_INFO_RAW,
		0,
		sizeof(FuSunwinonDfuBootInfo),
		error))
		return FALSE;

	*security_mode =
	    (fu_struct_sunwinon_dfu_rsp_system_info_get_opcode(st_sys_info) & 0xF0U) != 0U;

	return TRUE;
}

static gboolean
fu_sunwinon_util_dfu_master_fw_info_get_cmd(FuSwDfuMaster *self,
					    FuSunwinonDfuImageInfo *image_info,
					    guint32 *dfu_save_addr,
					    GError **error)
{
	FuDfuReceiveFrame recv_frame = {0};
	gsize info_size = 0;
	const guint8 *raw_info = NULL;
	g_autoptr(FuStructSunwinonDfuRspFwInfoGet) st_fw_info =
	    fu_struct_sunwinon_dfu_rsp_fw_info_get_new();

	g_return_val_if_fail(image_info != NULL, FALSE);

	g_debug("FwInfoGet");

	if (!fu_sunwinon_util_dfu_master_send_frame(self,
						    NULL,
						    0,
						    FU_SUNWINON_DFU_CMD_FW_INFO_GET,
						    error))
		return FALSE;

	recv_frame.data = st_fw_info->buf->data;
	recv_frame.data_len = st_fw_info->buf->len;
	if (!fu_sunwinon_util_dfu_master_recv_frame(self, &recv_frame, error))
		return FALSE;
	if (!fu_sunwinon_util_dfu_master_check_recv_cmd_type(self,
							     &recv_frame,
							     FU_SUNWINON_DFU_CMD_FW_INFO_GET,
							     error))
		return FALSE;
	if (!fu_struct_sunwinon_dfu_rsp_fw_info_get_validate(st_fw_info->buf->data,
							     st_fw_info->buf->len,
							     0,
							     error))
		return FALSE;
	if (fu_struct_sunwinon_dfu_rsp_fw_info_get_get_ack_status(st_fw_info) !=
	    FU_SUNWINON_DFU_ACK_SUCCESS)
		return fu_sunwinon_util_dfu_master_emit_ack_failure(FU_SUNWINON_DFU_CMD_FW_INFO_GET,
								    error);

	if (dfu_save_addr != NULL)
		*dfu_save_addr =
		    fu_struct_sunwinon_dfu_rsp_fw_info_get_get_dfu_save_addr(st_fw_info);

	raw_info =
	    fu_struct_sunwinon_dfu_rsp_fw_info_get_get_image_info_raw(st_fw_info, &info_size);
	if (!fu_memcpy_safe((guint8 *)image_info,
			    sizeof(FuSunwinonDfuImageInfo),
			    0,
			    raw_info,
			    info_size,
			    0,
			    sizeof(FuSunwinonDfuImageInfo),
			    error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_sunwinon_util_dfu_master_mode_set_cmd(FuSwDfuMaster *self,
					 FuSunwinonDfuUpgradeMode copy_mode,
					 GError **error)
{
	g_debug("ModeSet");
	if (!fu_sunwinon_util_dfu_master_send_frame(self,
						    (const guint8 *)&copy_mode,
						    1,
						    FU_SUNWINON_DFU_CMD_MODE_SET,
						    error))
		return FALSE;

	/* command ModeSet has no response, wait a while for device getting ready */
	fu_sunwinon_util_dfu_master_wait(self, 100);
	return TRUE;
}

static gboolean
fu_sunwinon_util_dfu_master_program_start_cmd(FuSwDfuMaster *self,
					      FuDfuInnerState *inner_state,
					      FuProgress *progress,
					      FuSunwinonFastDfuMode fast_mode,
					      GError **error)
{
	g_autoptr(FuStructSunwinonDfuPayloadProgramStart) st_prog_start = NULL;

	g_return_val_if_fail(inner_state != NULL, FALSE);
	g_return_val_if_fail(progress != NULL, FALSE);

	g_debug("ProgramStart");

	st_prog_start = fu_struct_sunwinon_dfu_payload_program_start_new();
	fu_struct_sunwinon_dfu_payload_program_start_set_mode(st_prog_start,
							      inner_state->fw_type | fast_mode);

	inner_state->now_img_info.boot_info.load_addr = inner_state->dfu_save_addr;
	if (!fu_struct_sunwinon_dfu_payload_program_start_set_image_info_raw(
		st_prog_start,
		(const guint8 *)&inner_state->now_img_info,
		sizeof(FuSunwinonDfuImageInfo),
		error))
		return FALSE;

	if (!fu_sunwinon_util_dfu_master_send_frame(self,
						    st_prog_start->buf->data,
						    st_prog_start->buf->len,
						    FU_SUNWINON_DFU_CMD_PROGRAM_START,
						    error))
		return FALSE;

	if (fast_mode == FU_SUNWINON_FAST_DFU_MODE_DISABLE) {
		if (!fu_sunwinon_util_dfu_master_plain_ack_recv(self,
								FU_SUNWINON_DFU_CMD_PROGRAM_START,
								error))
			return FALSE;
	} else
		return fu_sunwinon_util_dfu_master_fast_mode_not_supported(error);

	fu_progress_set_percentage(progress, 0);

	return TRUE;
}

static gboolean
fu_sunwinon_util_dfu_master_do_update_fast(FuSwDfuMaster *self,
					   FuDfuInnerState *inner_state,
					   FuProgress *progress,
					   GError **error)
{
	(void)self;
	(void)inner_state;
	(void)progress;
	return fu_sunwinon_util_dfu_master_fast_mode_not_supported(error);
}

static gboolean
fu_sunwinon_util_dfu_master_do_update_normal(FuSwDfuMaster *self,
					     FuDfuInnerState *inner_state,
					     FuProgress *progress,
					     GError **error)
{
	g_autoptr(FuStructSunwinonDfuPayloadProgramFlash) st_prog_flash = NULL;
	gsize already_sent = 0;
	guint16 data_len = 0;

	g_return_val_if_fail(inner_state != NULL, FALSE);
	g_return_val_if_fail(progress != NULL, FALSE);

	g_debug("normal DFU update start");

	st_prog_flash = fu_struct_sunwinon_dfu_payload_program_flash_new();
	inner_state->file_check_sum = 0;
	while (already_sent < self->fw_sz) {
		g_debug("programming flash: %zu / %zu", already_sent, self->fw_sz);

		fu_struct_sunwinon_dfu_payload_program_flash_set_dfu_save_addr(
		    st_prog_flash,
		    inner_state->dfu_save_addr + (guint32)already_sent);
		if (self->fw_sz - already_sent >= ONCE_SIZE)
			data_len = ONCE_SIZE;
		else
			data_len = (guint16)(self->fw_sz - already_sent);
		fu_struct_sunwinon_dfu_payload_program_flash_set_data_len(st_prog_flash, data_len);
		if (!fu_struct_sunwinon_dfu_payload_program_flash_set_fw_data(st_prog_flash,
									      self->fw +
										  already_sent,
									      data_len,
									      error))
			return FALSE;

		if (!fu_sunwinon_util_dfu_master_send_frame(self,
							    st_prog_flash->buf->data,
							    st_prog_flash->buf->len -
								(ONCE_SIZE - data_len),
							    FU_SUNWINON_DFU_CMD_PROGRAM_FLASH,
							    error))
			return FALSE;

		if (!fu_sunwinon_util_dfu_master_plain_ack_recv(self,
								FU_SUNWINON_DFU_CMD_PROGRAM_FLASH,
								error))
			return FALSE;

		/* update file check sum */
		for (guint16 i = 0; i < data_len; i++) {
			inner_state->file_check_sum += self->fw[already_sent + i];
		}
		already_sent += data_len;

		fu_progress_set_percentage(progress, (guint)((already_sent * 100) / self->fw_sz));
	}

	return TRUE;
}

static gboolean
fu_sunwinon_util_dfu_master_program_end_cmd_fast(FuSwDfuMaster *self,
						 FuDfuInnerState *inner_state,
						 FuProgress *progress,
						 GError **error)
{
	(void)self;
	(void)inner_state;
	(void)progress;
	return fu_sunwinon_util_dfu_master_fast_mode_not_supported(error);
}

static gboolean
fu_sunwinon_util_dfu_master_program_end_cmd_normal(FuSwDfuMaster *self,
						   FuDfuInnerState *inner_state,
						   FuProgress *progress,
						   GError **error)
{
	g_autoptr(FuStructSunwinonDfuPayloadProgramEnd) st_prog_end_payload = NULL;

	g_return_val_if_fail(inner_state != NULL, FALSE);
	g_return_val_if_fail(progress != NULL, FALSE);

	g_debug("ProgramEnd");

	st_prog_end_payload = fu_struct_sunwinon_dfu_payload_program_end_new();
	fu_struct_sunwinon_dfu_payload_program_end_set_file_check_sum(st_prog_end_payload,
								      inner_state->file_check_sum);

	if (!fu_sunwinon_util_dfu_master_send_frame(self,
						    st_prog_end_payload->buf->data,
						    st_prog_end_payload->buf->len,
						    FU_SUNWINON_DFU_CMD_PROGRAM_END,
						    error))
		return FALSE;

	if (!fu_sunwinon_util_dfu_master_plain_ack_recv(self,
							FU_SUNWINON_DFU_CMD_PROGRAM_END,
							error))
		return FALSE;

	fu_progress_set_percentage(progress, 100);

	return TRUE;
}

static gboolean
fu_sunwinon_util_dfu_master_handshake(FuSwDfuMaster *self,
				      FuDfuInnerState *inner_state,
				      FuSunwinonFastDfuMode fast_mode,
				      FuSunwinonDfuUpgradeMode copy_mode,
				      GError **error)
{
	g_return_val_if_fail(inner_state != NULL, FALSE);

	/* GetInfo -> SystemInfo -> FwInfoGet -> ModeSet */

	if (!fu_sunwinon_util_dfu_master_get_info_cmd(self, error))
		return FALSE;
	if (!fu_sunwinon_util_dfu_master_system_info_cmd(self,
							 &inner_state->boot_info,
							 &inner_state->security_mode,
							 error))
		return FALSE;
	if (!fu_sunwinon_util_dfu_master_fw_info_get_cmd(self,
							 &inner_state->app_info,
							 &inner_state->dfu_save_addr,
							 error))
		return FALSE;
	/* no command sent during checking */
	if (!fu_sunwinon_util_dfu_master_pre_update_check(self, inner_state, error))
		return FALSE;
	if (!fu_sunwinon_util_dfu_master_mode_set_cmd(self, copy_mode, error))
		return FALSE;

	return TRUE;
}

FuSwDfuMaster *
fu_sunwinon_util_dfu_master_new(const guint8 *fw, gsize fw_sz, FuDevice *device, GError **error)
{
	g_autoptr(FuSwDfuMaster) self = NULL;

	if (device == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "sunwinon-hid: missing device parameter");
		return NULL;
	}
	if (fw != NULL && fw_sz < DFU_IMAGE_INFO_TAIL_SIZE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "sunwinon-hid: firmware too small");
		return NULL;
	}
	self = g_new0(FuSwDfuMaster, 1);
	self->fw = fw;
	self->fw_sz = fw_sz;
	self->device = device;
	return g_steal_pointer(&self);
}

void
fu_sunwinon_util_dfu_master_free(FuSwDfuMaster *self)
{
	g_free(self);
}

gboolean
fu_sunwinon_util_dfu_master_fetch_fw_version(FuSwDfuMaster *self,
					     FuSunwinonDfuImageInfo *image_info,
					     GError **error)
{
	return fu_sunwinon_util_dfu_master_fw_info_get_cmd(self, image_info, NULL, error);
}

gboolean
fu_sunwinon_util_dfu_master_write_firmware(FuSwDfuMaster *self,
					   FuProgress *progress,
					   FuSunwinonFastDfuMode fast_mode,
					   FuSunwinonDfuUpgradeMode copy_mode,
					   GError **error)
{
	FuDfuInnerState inner_state = {0};

	if (!fu_sunwinon_util_dfu_master_dfu_get_img_info(self, &inner_state.now_img_info, error))
		return FALSE;
	if (!fu_sunwinon_util_dfu_master_handshake(self, &inner_state, fast_mode, copy_mode, error))
		return FALSE;
	if (!fu_sunwinon_util_dfu_master_program_start_cmd(self,
							   &inner_state,
							   progress,
							   fast_mode,
							   error))
		return FALSE;
	if (fast_mode == FU_SUNWINON_FAST_DFU_MODE_DISABLE) {
		if (!fu_sunwinon_util_dfu_master_do_update_normal(self,
								  &inner_state,
								  progress,
								  error))
			return FALSE;
		if (!fu_sunwinon_util_dfu_master_program_end_cmd_normal(self,
									&inner_state,
									progress,
									error))
			return FALSE;
	} else {
		if (!fu_sunwinon_util_dfu_master_do_update_fast(self,
								&inner_state,
								progress,
								error))
			return FALSE;
		if (!fu_sunwinon_util_dfu_master_program_end_cmd_fast(self,
								      &inner_state,
								      progress,
								      error))
			return FALSE;
	}

	return TRUE;
}
