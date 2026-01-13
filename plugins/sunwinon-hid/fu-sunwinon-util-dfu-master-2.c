/*
 * SPDX-License-Identifier: LGPL-2.1-or-later OR BSD-3-Clause
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-sunwinon-hid-struct.h"
#include "fu-sunwinon-util-dfu-master.h"

#define HID_REPORT_DATA_LEN 480
/* there is 8 bytes reserved in all fw blob (from file and on device) */
#define DFU_IMAGE_INFO_TAIL_SIZE 48
/* fw load addr shall align to sector size */
#define FLASH_OP_SECTOR_SIZE 0x1000
/* fw pattern value */
#define PATTERN_VALUE		0x4744
#define DFU_SIGN_LEN		856
#define PATTERN_DEADBEEF	0xDEADBEEF
#define PATTERN_SIGN		0x4E474953 /* "SIGN" */
#define PATTERN_DEADBEEF_OFFSET 48
#define PATTERN_SIGN_OFFSET	72

struct FuSwDfuMaster {
	guint32 img_data_addr;
	guint32 all_check_sum;
	guint32 file_size;
	guint32 programed_size;
	gboolean run_fw_flag;
	guint16 once_size;
	guint16 sent_len;
	guint16 all_send_len;
	guint16 erase_sectors;
	guint8 ble_fast_send_cplt_flag;
	guint8 fast_dfu_mode;
	/* new FW save address in peripheral */
	guint32 dfu_save_addr;
	guint32 dfu_timeout_start_time;
	gboolean dfu_timeout_started;
	/* V2 API args */
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
	gboolean security_mode;
	guint32 dfu_save_addr;
} FuDfuInnerState;

typedef struct {
	FuSunwinonDfuCmd cmd_type;
	guint16 data_len; /* inout */
	guint8 *data;
	guint16 check_sum;
} FuDfuReceiveFrame;

static gboolean
fu_sunwinon_util_dfu_master_2_check_fw_available(FuSwDfuMaster *self, GError **error)
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
fu_sunwinon_util_dfu_master_2_dfu_get_img_info(FuSwDfuMaster *self,
					       FuSunwinonDfuImageInfo *image_info,
					       GError **error)
{
	if (!fu_sunwinon_util_dfu_master_2_check_fw_available(self, error))
		return FALSE;
	/* fw info stored near the tail of blob */
	if (!fu_memcpy_safe((guint8 *)image_info,
			    sizeof(FuSunwinonDfuImageInfo),
			    0,
			    self->fw,
			    self->fw_sz,
			    self->fw_sz - DFU_IMAGE_INFO_TAIL_SIZE,
			    sizeof(FuSunwinonDfuImageInfo),
			    error))
		return FALSE;
	if (image_info->pattern != PATTERN_VALUE) {
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

const char str[] = {0x4E, 0x47, 0x49, 0x53}; // "NGIS"

static gboolean
fu_sunwinon_util_dfu_master_2_pre_update_check(FuSwDfuMaster *self,
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
			g_debug("signed firmware (sign pattern found)");
		}
	}
	/* else fw is always unsigned */
	g_debug("unsigned firmware");

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

/*
static gboolean
fu_sunwinon_util_dfu_master_dfu_get_img_data(FuSwDfuMaster *self,
					guint32 addr,
					guint8 *data,
					guint16 len,
					GError **error)
{
	guint32 off = 0;
	gsize avail = 0;

	if (addr >= self->fw_save_addr)
		off = addr - self->fw_save_addr;
	if ((guint64)off + len > self->fw_sz)
		len = (guint16)MAX((gint)0, (gint)(self->fw_sz > off ? self->fw_sz - off : 0));
	{
		avail = (self->fw_sz > off) ? (self->fw_sz - off) : 0;
		return fu_memcpy_safe(data,
				      len,
				      0,
				      (const guint8 *)(self->fw + off),
				      avail,
				      0,
				      len,
				      error);
	}
}
*/
static guint32
fu_sunwinon_util_dfu_master_2_get_time(void)
{
	return (guint32)(g_get_monotonic_time() / 1000);
}

static void
fu_sunwinon_util_dfu_master_2_wait(FuSwDfuMaster *self, guint32 ms)
{
	fu_device_sleep(FU_DEVICE(self->device), ms);
}
/*
static void
fu_sunwinon_util_dfu_master_dfu_event_handler(FuSwDfuMaster *self, FuSunwinonDfuEvent event,
guint8 progress)
{
	switch (event) {
	case FU_SUNWINON_DFU_EVENT_PRO_START_SUCCESS:
		break;
	case FU_SUNWINON_DFU_EVENT_PRO_FLASH_SUCCESS:
	case FU_SUNWINON_DFU_EVENT_FAST_DFU_PRO_FLASH_SUCCESS:
		if (self->progress != NULL)
			fu_progress_set_percentage(self->progress, progress);
		break;
	case FU_SUNWINON_DFU_EVENT_PRO_END_SUCCESS:
		if (self->progress != NULL)
			fu_progress_set_percentage(self->progress, 100);
		self->done = TRUE;
		break;
	case FU_SUNWINON_DFU_EVENT_DFU_ACK_TIMEOUT:
		g_clear_pointer(&self->fail_reason, g_free);
		self->fail_reason = g_strdup("dfu ack timeout");
		self->failed = TRUE;
		self->done = TRUE;
		break;
	case FU_SUNWINON_DFU_EVENT_PRO_END_FAIL:
		g_clear_pointer(&self->fail_reason, g_free);
		self->fail_reason = g_strdup("program end failed");
		self->failed = TRUE;
		self->done = TRUE;
		break;
	default:
		break;
	}
}
*/

static gboolean
fu_sunwinon_util_dfu_master_2_parse_and_progress(FuSwDfuMaster *self,
						 guint16 cmd_type,
						 guint16 check_sum,
						 const guint8 *data,
						 guint16 len,
						 GError **error)
{
	/* TODO */
	return TRUE;
}

static gboolean
fu_sunwinon_util_dfu_master_2_recv_sum_check(FuDfuReceiveFrame *recv_frame)
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
fu_sunwinon_util_dfu_master_2_cal_send_check_sum(FuSunwinonDfuCmd cmd_type,
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
fu_sunwinon_util_dfu_master_2_send_frame(FuSwDfuMaster *self,
					 const guint8 *p_data,
					 guint16 len,
					 FuSunwinonDfuCmd cmd_type,
					 GError **error)
{
	gsize total_len = FU_STRUCT_SUNWINON_DFU_FRAME_HEADER_SIZE + len + 2; /* +2 for check_sum */
	guint16 check_sum = 0;
	g_autoptr(FuStructSunwinonHidOutV2) st_out = fu_struct_sunwinon_hid_out_v2_new();
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

	if (!fu_struct_sunwinon_hid_out_v2_set_dfu_header(st_out, st_dfu_header, error))
		return FALSE;

	if (p_data != NULL && !fu_struct_sunwinon_hid_out_v2_set_data(st_out, p_data, len, error))
		return FALSE;

	fu_struct_sunwinon_hid_out_v2_set_data_len(st_out, total_len);

	/* write check_sum at the very end of the whole data package */
	check_sum = fu_sunwinon_util_dfu_master_2_cal_send_check_sum(cmd_type, p_data, len);
	fu_memwrite_uint16(st_out->buf->data + FU_STRUCT_SUNWINON_HID_OUT_V2_OFFSET_DATA + len,
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
fu_sunwinon_util_dfu_master_2_recv_frame(FuSwDfuMaster *self,
					 FuDfuReceiveFrame *recv_frame,
					 GError **error)
{
	gsize check_sum_offset = 0;
	gsize buf_len = 0;
	g_autoptr(FuStructSunwinonHidInV2) st_in = NULL;
	g_autoptr(FuStructSunwinonDfuFrameHeader) st_dfu_header = NULL;

	g_return_val_if_fail(recv_frame != NULL, FALSE);
	g_return_val_if_fail(recv_frame->data != NULL, FALSE);

	buf_len = recv_frame->data_len;

	st_in = fu_struct_sunwinon_hid_in_v2_new();
	memset(st_in->buf->data, 0, st_in->buf->len);
	/* may not get a full length report here */
	(void)fu_hidraw_device_get_report(FU_HIDRAW_DEVICE(self->device),
					  st_in->buf->data,
					  st_in->buf->len,
					  FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
					  NULL);

	fu_dump_raw(G_LOG_DOMAIN, "raw input report", st_in->buf->data, st_in->buf->len);

	if (!fu_struct_sunwinon_hid_in_v2_validate(st_in->buf->data, st_in->buf->len, 0, error))
		return FALSE;

	st_dfu_header = fu_struct_sunwinon_hid_in_v2_get_dfu_header(st_in);
	recv_frame->cmd_type = fu_struct_sunwinon_dfu_frame_header_get_cmd_type(st_dfu_header);
	recv_frame->data_len = fu_struct_sunwinon_dfu_frame_header_get_data_len(st_dfu_header);
	check_sum_offset = FU_STRUCT_SUNWINON_HID_IN_V2_OFFSET_DATA + recv_frame->data_len;
	recv_frame->check_sum =
	    fu_memread_uint16(st_in->buf->data + check_sum_offset, G_LITTLE_ENDIAN);

	if (buf_len > 0 &&
	    !fu_memcpy_safe(recv_frame->data,
			    buf_len,
			    0,
			    st_in->buf->data + FU_STRUCT_SUNWINON_HID_IN_V2_OFFSET_DATA,
			    recv_frame->data_len,
			    0,
			    recv_frame->data_len,
			    error))
		return FALSE;

	if (!fu_sunwinon_util_dfu_master_2_recv_sum_check(recv_frame)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "sunwinon-hid: received frame check sum mismatch");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_sunwinon_util_dfu_master_2_check_recv_cmd_type(FuSwDfuMaster *self,
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
fu_sunwinon_util_dfu_master_2_emit_ack_failure(FuSunwinonDfuCmd cmd_type, GError **error)
{
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INTERNAL,
		    "sunwinon-hid: command %s not acked successfully",
		    fu_sunwinon_dfu_cmd_to_string(cmd_type));
	return FALSE;
}

static gboolean
fu_sunwinon_util_dfu_master_2_get_info(FuSwDfuMaster *self, GError **error)
{
	FuDfuReceiveFrame recv_frame = {0};
	g_autoptr(FuStructSunwinonDfuRspGetInfo) st_get_info = NULL;

	g_debug("GetInfo");

	if (!fu_sunwinon_util_dfu_master_2_send_frame(self,
						      NULL,
						      0,
						      FU_SUNWINON_DFU_CMD_GET_INFO,
						      error))
		return FALSE;

	st_get_info = fu_struct_sunwinon_dfu_rsp_get_info_new();
	recv_frame.data = st_get_info->buf->data;
	recv_frame.data_len = st_get_info->buf->len;

	if (!fu_sunwinon_util_dfu_master_2_recv_frame(self, &recv_frame, error))
		return FALSE;
	if (!fu_sunwinon_util_dfu_master_2_check_recv_cmd_type(self,
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
		return fu_sunwinon_util_dfu_master_2_emit_ack_failure(FU_SUNWINON_DFU_CMD_GET_INFO,
								      error);
	return TRUE;
}

static gboolean
fu_sunwinon_util_dfu_master_2_system_info(FuSwDfuMaster *self,
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
	if (!fu_sunwinon_util_dfu_master_2_send_frame(self,
						      st_sys_info_payload->buf->data,
						      st_sys_info_payload->buf->len,
						      FU_SUNWINON_DFU_CMD_SYSTEM_INFO,
						      error))
		return FALSE;

	st_sys_info = fu_struct_sunwinon_dfu_rsp_system_info_new();
	recv_frame.data = st_sys_info->buf->data;
	recv_frame.data_len = st_sys_info->buf->len;

	if (!fu_sunwinon_util_dfu_master_2_recv_frame(self, &recv_frame, error))
		return FALSE;
	if (!fu_sunwinon_util_dfu_master_2_check_recv_cmd_type(self,
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
		return fu_sunwinon_util_dfu_master_2_emit_ack_failure(
		    FU_SUNWINON_DFU_CMD_SYSTEM_INFO,
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
fu_sunwinon_util_dfu_master_2_fw_info_get(FuSwDfuMaster *self,
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

	if (!fu_sunwinon_util_dfu_master_2_send_frame(self,
						      NULL,
						      0,
						      FU_SUNWINON_DFU_CMD_FW_INFO_GET,
						      error))
		return FALSE;

	recv_frame.data = st_fw_info->buf->data;
	recv_frame.data_len = st_fw_info->buf->len;
	if (!fu_sunwinon_util_dfu_master_2_recv_frame(self, &recv_frame, error))
		return FALSE;
	if (!fu_sunwinon_util_dfu_master_2_check_recv_cmd_type(self,
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
		return fu_sunwinon_util_dfu_master_2_emit_ack_failure(
		    FU_SUNWINON_DFU_CMD_FW_INFO_GET,
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
fu_sunwinon_util_dfu_master_2_mode_set(FuSwDfuMaster *self, guint8 mode_setting, GError **error)
{
	if (!fu_sunwinon_util_dfu_master_2_send_frame(self,
						      &mode_setting,
						      1,
						      FU_SUNWINON_DFU_CMD_MODE_SET,
						      error))
		return FALSE;

	/* command ModeSet has no response, wait a while for device getting ready */
	fu_sunwinon_util_dfu_master_2_wait(self, 100);
	return TRUE;
}

static gboolean
fu_sunwinon_util_dfu_master_2_handshake(FuSwDfuMaster *self,
					guint8 mode_setting,
					FuDfuInnerState *inner_state,
					GError **error)
{
	g_return_val_if_fail(inner_state != NULL, FALSE);

	/* GetInfo -> SystemInfo -> FwInfoGet */

	if (!fu_sunwinon_util_dfu_master_2_get_info(self, error))
		return FALSE;
	if (!fu_sunwinon_util_dfu_master_2_system_info(self,
						       &inner_state->boot_info,
						       &inner_state->security_mode,
						       error))
		return FALSE;
	if (!fu_sunwinon_util_dfu_master_2_fw_info_get(self,
						       &inner_state->app_info,
						       &inner_state->dfu_save_addr,
						       error))
		return FALSE;

	if (!fu_sunwinon_util_dfu_master_2_pre_update_check(self, inner_state, error))
		return FALSE;

	/* start update */

	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "sunwinon-hid: not implemented");
	return FALSE;

	if (!fu_sunwinon_util_dfu_master_2_mode_set(self, mode_setting, error))
		return FALSE;
	return TRUE;
}

FuSwDfuMaster *
fu_sunwinon_util_dfu_master_2_new(const guint8 *fw, gsize fw_sz, FuDevice *device)
{
	g_autoptr(FuSwDfuMaster) self = NULL;

	g_return_val_if_fail(device != NULL, NULL);
	self = g_new0(FuSwDfuMaster, 1);
	self->fast_dfu_mode = FU_SUNWINON_FAST_DFU_MODE_DISABLE;
	self->once_size = HID_REPORT_DATA_LEN;
	self->fw = fw;
	self->fw_sz = fw_sz;
	self->device = device;
	return g_steal_pointer(&self);
}

void
fu_sunwinon_util_dfu_master_2_free(FuSwDfuMaster *self)
{
	g_free(self);
}

gboolean
fu_sunwinon_util_dfu_master_2_fetch_fw_version(FuSwDfuMaster *self,
					       FuSunwinonDfuImageInfo *image_info,
					       GError **error)
{
	return fu_sunwinon_util_dfu_master_2_fw_info_get(self, image_info, NULL, error);
}

gboolean
fu_sunwinon_util_dfu_master_2_write_firmware(FuSwDfuMaster *self,
					     FuProgress *progress,
					     guint8 mode_setting,
					     GError **error)
{
	FuDfuInnerState inner_state = {0};
	(void)self;
	(void)progress;
	(void)error;

	if (!fu_sunwinon_util_dfu_master_2_dfu_get_img_info(self, &inner_state.now_img_info, error))
		return FALSE;

	if (!fu_sunwinon_util_dfu_master_2_handshake(self, mode_setting, &inner_state, error))
		return FALSE;

	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "sunwinon-hid: not implemented");
	return FALSE;
}
