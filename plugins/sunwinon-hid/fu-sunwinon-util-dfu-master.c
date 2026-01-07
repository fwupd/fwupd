/*
 * SPDX-License-Identifier: LGPL-2.1-or-later OR BSD-3-Clause
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-sunwinon-util-dfu-master.h"

enum {
	/* CMD header low byte */
	CMD_FRAME_HEADER_L = 0x44U,
	/* CMD header high byte */
	CMD_FRAME_HEADER_H = 0x47U,
	/* get info cmd */
	GET_INFO = 0x01U,
	/* program start cmd */
	PROGRAM_START = 0x23U,
	/* program flash cmd */
	PROGRAM_FLASH = 0x24U,
	/* program end cmd */
	PROGRAM_END = 0x25U,
	/* system information cmd */
	SYSTEM_INFO = 0x27U,
	/* DFU mode set cmd */
	DFU_MODE_SET = 0x41U,
	/* DFU fw info get cmd */
	DFU_FW_INFO_GET = 0x42U,
	/* fast DFU mode successfully end cmd */
	DFU_FAST_DFU_FLASH_SUCCESS = 0xFFU,
};

/* CMD ack success */
#define ACK_SUCCESS 0x01U
/* CMD ack error */
#define ACK_ERROR 0x02U

/* firmware flags */
#define FU_SUNWINON_DFU_FW_ENC_OR_SIGN_PATTERN 0xDEADBEEFU
#define FU_SUNWINON_DFU_FW_SIGN_PATTERN	       0x4E474953U /* "SIGN" */

/* flash sector size */
#define FLASH_OP_SECTOR_SIZE 0x1000U
/* pattern value */
#define PATTERN_VALUE 0x4744U

/* firmware sign flag offset */
#define FW_SIGN_FLAG_OFFSET 72U
/* sign Firmware Type */
#define SIGN_FW_TYPE 0x10U
/* normal Firmware Type */
#define NORMAL_FW_TYPE 0x00U

/* firmware signature length */
#define DFU_SIGN_LEN 856U
/* image information length */
#define DFU_IMAGE_INFO_LEN 48U

/* get system information command data length */
#define DFU_CMD_GET_SYSTEM_INFO_DATA_LEN 7U
/* get system information command data length low byte position */
#define DFU_CMD_GET_SYSTEM_INFO_LEN_L_POS 5U
/* get system information command length high byte position */
#define DFU_CMD_GET_SYSTEM_INFO_LEN_H_POS 6U
/* DFU mode set command data length */
#define DFU_CMD_MODE_SET_DATA_LEN 1U
/* program start command data length */
#define DFU_CMD_PRO_START_DATA_LEN 41U
/* program end command data length */
#define DFU_CMD_PRO_END_DATA_LEN 5U
/* program flash command header length */
#define DFU_CMD_PRO_FLASH_HEAD_LEN 7U
/* program flash command data length low byte position */
#define DFU_CMD_PRO_FLASH_LEN_L_POS 5U
/* program flash command data length high byte position */
#define DFU_CMD_PRO_FLASH_LEN_H_POS 6U
/* get info command response DFU version position */
#define DFU_RSP_DFU_VERSION_POS 17U
/* get system info command response operation position */
#define DFU_RSP_SYS_INFO_OP_POS 1U
/* get system info command response data position */
#define DFU_RSP_SYS_INFO_DATA_POS 8U
/* get firmware info command response firmware run position position */
#define DFU_RSP_RUN_POSITION_POS 5U
/* get firmware info command response image info position */
#define DFU_RSP_IMG_INFO_POS 6U
/* program start command response erased sector position in fast mode */
#define DFU_RSP_ERASE_POS 6U

#define DFU_FRAME_HRD_L_POS  0U /* DFU frame header low byte position */
#define DFU_FRAME_HRD_H_POS  1U /* DFU frame header high byte position */
#define DFU_FRAME_TYPE_L_POS 2U /* DFU frame type low byte position */
#define DFU_FRAME_TYPE_H_POS 3U /* DFU frame type high byte position */
#define DFU_FRAME_LEN_L_POS  4U /* DFU frame length low byte position */
#define DFU_FRAME_LEN_H_POS  5U /* DFU frame length high byte position */
#define DFU_FRAME_DATA_POS   6U /* DFU frame data position */

typedef enum {
	CHECK_FRAME_L_STATE = 0x00,
	CHECK_FRAME_H_STATE,
	RECEIVE_CMD_TYPE_L_STATE,
	RECEIVE_CMD_TYPE_H_STATE,
	RECEIVE_LEN_L_STATE,
	RECEIVE_LEN_H_STATE,
	RECEIVE_DATA_STATE,
	RECEIVE_CHECK_SUM_L_STATE,
	RECEIVE_CHECK_SUM_H_STATE,
} FuCmdParseState;

typedef struct {
	guint16 cmd_type;
	guint16 data_len;
	guint8 data[FU_SUNWINON_DFU_FRAME_MAX_RX - DFU_FRAME_DATA_POS];
	guint16 check_sum;
} FuReceiveFrame;

typedef struct {
	guint8 dfu_tx_buf[FU_SUNWINON_DFU_FRAME_MAX_TX];
	FuReceiveFrame receive_frame;
	gboolean cmd_receive_flag;
	guint16 receive_data_count;
	guint16 receive_check_sum;
	/* peripheral bootloader information */
	FuSunwinonDfuBootInfo boot_info;
	/* FW image information about the firmware to be upgraded */
	FuSunwinonDfuImageInfo now_img_info;
	/* image information in peripheral APP Info area */
	FuSunwinonDfuImageInfo app_info;
	guint32 img_data_addr;
	guint32 all_check_sum;
	guint32 file_size;
	guint32 programed_size;
	gboolean run_fw_flag;
	FuSunwinonDfuCallback func_cfg;
	guint16 once_size;
	guint16 sent_len;
	guint16 all_send_len;
	FuCmdParseState parse_state;
	gboolean sec_flag;
	gboolean new_version_flag;
	guint16 erase_sectors;
	guint8 ble_fast_send_cplt_flag;
	guint8 fast_dfu_mode;
	/* new FW save address in peripheral */
	guint32 dfu_save_addr;
	guint32 dfu_timeout_start_time;
	gboolean dfu_timeout_started;
} FuDfuMasterState;

/* opaque instance type holding state */
struct FuDfuMaster {
	FuDfuMasterState state;
};

static gboolean
fu_sunwinon_util_dfu_master_get_img_info(FuDfuMaster *self,
					 FuSunwinonDfuImageInfo *img_info,
					 GError **error)
{
	FuDfuMasterState *dfu_state = &self->state;
	if (dfu_state->func_cfg.dfu_m_get_img_info == NULL)
		return TRUE;
	return dfu_state->func_cfg.dfu_m_get_img_info(dfu_state->func_cfg.user_data,
						      img_info,
						      error);
}

static gboolean
fu_sunwinon_util_dfu_master_get_img_data(FuDfuMaster *self,
					 guint32 addr,
					 guint8 *data,
					 guint16 len,
					 GError **error)
{
	FuDfuMasterState *dfu_state = &self->state;
	if (dfu_state->func_cfg.dfu_m_get_img_data == NULL)
		return TRUE;
	return dfu_state->func_cfg.dfu_m_get_img_data(dfu_state->func_cfg.user_data,
						      addr,
						      data,
						      len,
						      error);
}

static gboolean
fu_sunwinon_util_dfu_master_send_data(FuDfuMaster *self, guint8 *data, guint16 len, GError **error)
{
	FuDfuMasterState *dfu_state = &self->state;
	if (dfu_state->func_cfg.dfu_m_send_data == NULL)
		return TRUE;
	return dfu_state->func_cfg.dfu_m_send_data(dfu_state->func_cfg.user_data, data, len, error);
}

static void
fu_sunwinon_util_dfu_master_event_handler(FuDfuMaster *self,
					  FuSunwinonDfuEvent event,
					  guint8 progress)
{
	FuDfuMasterState *dfu_state = &self->state;
	if (dfu_state->func_cfg.dfu_m_event_handler != NULL)
		dfu_state->func_cfg.dfu_m_event_handler(dfu_state->func_cfg.user_data,
							event,
							progress);
}

static guint32
fu_sunwinon_util_dfu_master_get_time(FuDfuMaster *self)
{
	FuDfuMasterState *dfu_state = &self->state;
	if (dfu_state->func_cfg.dfu_m_get_time != NULL)
		return dfu_state->func_cfg.dfu_m_get_time(dfu_state->func_cfg.user_data);
	return 0;
}

static void
fu_sunwinon_util_dfu_master_cmd_check(FuDfuMaster *self)
{
	FuDfuMasterState *dfu_state = &self->state;
	for (guint16 i = 0; i < dfu_state->receive_frame.data_len; i++) {
		dfu_state->receive_check_sum += dfu_state->receive_frame.data[i];
	}

	if (dfu_state->receive_check_sum == dfu_state->receive_frame.check_sum)
		dfu_state->cmd_receive_flag = TRUE;
	else {
		dfu_state->cmd_receive_flag = FALSE;
		fu_sunwinon_util_dfu_master_event_handler(self,
							  FU_SUNWINON_DFU_EVENT_FRAME_CHECK_ERROR,
							  0);
	}
}

static gboolean
fu_sunwinon_util_dfu_master_send(FuDfuMaster *self, guint16 len, GError **error)
{
	FuDfuMasterState *dfu_state = &self->state;
	dfu_state->all_send_len = len;
	if (len >= dfu_state->once_size)
		dfu_state->sent_len = dfu_state->once_size;
	else
		dfu_state->sent_len = len;

	if (!fu_sunwinon_util_dfu_master_send_data(self,
						   dfu_state->dfu_tx_buf,
						   dfu_state->sent_len,
						   error))
		return FALSE;
	return TRUE;
}

static void
fu_sunwinon_util_dfu_master_wait(FuDfuMaster *self, guint32 ms)
{
	FuDfuMasterState *dfu_state = &self->state;
	if (dfu_state->func_cfg.dfu_m_wait != NULL)
		dfu_state->func_cfg.dfu_m_wait(dfu_state->func_cfg.user_data, ms);
}

static gboolean
fu_sunwinon_util_dfu_master_send_frame(FuDfuMaster *self,
				       const guint8 *p_data,
				       guint16 len,
				       guint16 cmd_type,
				       GError **error)
{
	FuDfuMasterState *dfu_state = &self->state;

	guint16 i;
	guint16 check_sum = 0;
	/* printf("Send CMD Type: 0x%04X, Len: %d\n", cmd_type, len); */
	dfu_state->dfu_tx_buf[DFU_FRAME_HRD_L_POS] = CMD_FRAME_HEADER_L;
	dfu_state->dfu_tx_buf[DFU_FRAME_HRD_H_POS] = CMD_FRAME_HEADER_H;
	dfu_state->dfu_tx_buf[DFU_FRAME_TYPE_L_POS] = (guint8)cmd_type;
	dfu_state->dfu_tx_buf[DFU_FRAME_TYPE_H_POS] = (guint8)(cmd_type >> 8);
	dfu_state->dfu_tx_buf[DFU_FRAME_LEN_L_POS] = (guint8)len;
	dfu_state->dfu_tx_buf[DFU_FRAME_LEN_H_POS] = (guint8)(len >> 8);

	for (i = DFU_FRAME_TYPE_L_POS; i < DFU_FRAME_DATA_POS; i++) {
		check_sum += dfu_state->dfu_tx_buf[i];
	}
	if (len > 0 && p_data != NULL)
		if (!fu_memcpy_safe(dfu_state->dfu_tx_buf,
				    sizeof(dfu_state->dfu_tx_buf),
				    DFU_FRAME_DATA_POS,
				    p_data,
				    len,
				    0,
				    len,
				    error))
			return FALSE;

	for (i = 0; i < len; i++) {
		check_sum += dfu_state->dfu_tx_buf[DFU_FRAME_DATA_POS + i];
	}
	dfu_state->dfu_tx_buf[len + DFU_FRAME_DATA_POS] = (guint8)check_sum;
	dfu_state->dfu_tx_buf[len + DFU_FRAME_DATA_POS + 1U] = (guint8)(check_sum >> 8);
	if (!fu_sunwinon_util_dfu_master_send(self, len + DFU_FRAME_DATA_POS + 2U, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_sunwinon_util_dfu_master_program_flash(FuDfuMaster *self, guint16 len, GError **error)
{
	FuDfuMasterState *dfu_state = &self->state;
	guint8 data[FU_SUNWINON_DFU_CONFIG_ONCE_PROGRAM_LEN + DFU_CMD_PRO_FLASH_HEAD_LEN];
	dfu_state->programed_size += len;

	if (!fu_sunwinon_util_dfu_master_get_img_data(self,
						      dfu_state->img_data_addr,
						      &data[DFU_CMD_PRO_FLASH_HEAD_LEN],
						      len,
						      error))
		return FALSE;
	for (guint32 i = 0; i < len; i++) {
		dfu_state->all_check_sum += data[i + DFU_CMD_PRO_FLASH_HEAD_LEN];
	}
	data[0] = 0x01U; /* write flash base on image Info */

	fu_memwrite_uint32(&data[1], dfu_state->dfu_save_addr, G_LITTLE_ENDIAN);

	data[DFU_CMD_PRO_FLASH_LEN_L_POS] = (guint8)len;
	data[DFU_CMD_PRO_FLASH_LEN_H_POS] = (guint8)(len >> 8);

	if (!fu_sunwinon_util_dfu_master_send_frame(self,
						    data,
						    len + DFU_CMD_PRO_FLASH_HEAD_LEN,
						    PROGRAM_FLASH,
						    error))
		return FALSE;
	dfu_state->dfu_save_addr += len;
	dfu_state->img_data_addr += len;
	return TRUE;
}

static void
fu_sunwinon_util_dfu_master_fast_program_flash(FuDfuMaster *self)
{
	FuDfuMasterState *dfu_state = &self->state;
	guint16 remain;
	guint16 i = 0U;
	guint8 progress = 0U;
	while (dfu_state->programed_size != dfu_state->file_size) {
		if ((dfu_state->ble_fast_send_cplt_flag != 0U) ||
		    (dfu_state->programed_size == 0U)) {
			dfu_state->ble_fast_send_cplt_flag = 0U;
			if (!fu_sunwinon_util_dfu_master_get_img_data(self,
								      dfu_state->img_data_addr,
								      &dfu_state->dfu_tx_buf[0],
								      dfu_state->once_size,
								      NULL)) {
				fu_sunwinon_util_dfu_master_event_handler(
				    self,
				    FU_SUNWINON_DFU_EVENT_FAST_DFU_FLASH_FAIL,
				    0);
				return;
			}

			if ((dfu_state->programed_size + dfu_state->once_size) >
			    dfu_state->file_size) {
				remain =
				    (guint16)(dfu_state->file_size - dfu_state->programed_size);
				(void)fu_sunwinon_util_dfu_master_send(self, remain, NULL);
				for (i = 0U; i < remain; i++) {
					dfu_state->all_check_sum += dfu_state->dfu_tx_buf[i];
				}
				dfu_state->programed_size += remain;
			} else {
				dfu_state->programed_size += dfu_state->once_size;
				(void)fu_sunwinon_util_dfu_master_send(self,
								       dfu_state->once_size,
								       NULL);
				for (i = 0U; i < dfu_state->once_size; i++) {
					dfu_state->all_check_sum += dfu_state->dfu_tx_buf[i];
				}
			}

			progress =
			    (guint8)((dfu_state->programed_size * 100U) / dfu_state->file_size);
			fu_sunwinon_util_dfu_master_event_handler(
			    self,
			    FU_SUNWINON_DFU_EVENT_FAST_DFU_PRO_FLASH_SUCCESS,
			    progress);
			dfu_state->img_data_addr += dfu_state->once_size;
		}
	}
}

static gboolean
fu_sunwinon_util_dfu_master_program_start(FuDfuMaster *self,
					  gboolean security,
					  gboolean run_fw,
					  GError **error)
{
	FuDfuMasterState *dfu_state = &self->state;
	guint8 data[DFU_CMD_PRO_START_DATA_LEN] = {0};
	guint32 bin_size;
	guint32 tail_size;
	guint32 fw_sign_flag0 = 0;
	guint32 fw_sign_flag1 = 0;
	guint32 flag_addr;
	guint32 bootloader_end;
	guint32 bank0_fw_end;
	g_debug(__FUNCTION__);
	dfu_state->run_fw_flag = run_fw;
	dfu_state->all_check_sum = 0U;
	dfu_state->programed_size = 0U;

	data[0] = 0;
	if (!fu_sunwinon_util_dfu_master_get_img_info(self, &dfu_state->now_img_info, error))
		return FALSE;
	dfu_state->img_data_addr = dfu_state->now_img_info.boot_info.load_addr;
	if ((dfu_state->now_img_info.pattern != PATTERN_VALUE) ||
	    ((dfu_state->now_img_info.boot_info.load_addr % FLASH_OP_SECTOR_SIZE) != 0U)) {
		fu_sunwinon_util_dfu_master_event_handler(self,
							  FU_SUNWINON_DFU_EVENT_IMG_INFO_CHECK_FAIL,
							  0);
		return FALSE;
	}

	dfu_state->now_img_info.boot_info.load_addr = dfu_state->dfu_save_addr;

	bin_size = dfu_state->now_img_info.boot_info.bin_size + DFU_IMAGE_INFO_LEN;
	tail_size = DFU_IMAGE_INFO_LEN;
	if (security) /* security mode */
	{
		bin_size += DFU_SIGN_LEN;
		tail_size += DFU_SIGN_LEN;
	} else {
		flag_addr = dfu_state->img_data_addr + bin_size;
		if (!fu_sunwinon_util_dfu_master_get_img_data(self,
							      flag_addr,
							      (guint8 *)&fw_sign_flag0,
							      (guint16)sizeof(fw_sign_flag0),
							      error))
			return FALSE;
		flag_addr = dfu_state->img_data_addr + bin_size + FW_SIGN_FLAG_OFFSET;
		if (!fu_sunwinon_util_dfu_master_get_img_data(self,
							      flag_addr,
							      (guint8 *)&fw_sign_flag1,
							      (guint16)sizeof(fw_sign_flag1),
							      error))
			return FALSE;
		if (((fw_sign_flag0 == FU_SUNWINON_DFU_FW_ENC_OR_SIGN_PATTERN)) &&
		    (fw_sign_flag1 == FU_SUNWINON_DFU_FW_SIGN_PATTERN)) {
			bin_size += DFU_SIGN_LEN;
			tail_size += DFU_SIGN_LEN;
			data[0] = SIGN_FW_TYPE;
		} else
			data[0] = NORMAL_FW_TYPE;
	}

	/* the new FW cannot overlap with app bootloder */
	bootloader_end = dfu_state->boot_info.load_addr + dfu_state->boot_info.bin_size + tail_size;
	if (dfu_state->dfu_save_addr <= (bootloader_end)) {
		fu_sunwinon_util_dfu_master_event_handler(
		    self,
		    FU_SUNWINON_DFU_EVENT_DFU_FW_SAVE_ADDR_CONFLICT,
		    0);
		return FALSE;
	}

	/* the new FW cannot overlap with bank0 FW */
	bank0_fw_end = dfu_state->app_info.boot_info.load_addr +
		       dfu_state->app_info.boot_info.bin_size + tail_size;
	if (dfu_state->dfu_save_addr <= bank0_fw_end) {
		fu_sunwinon_util_dfu_master_event_handler(
		    self,
		    FU_SUNWINON_DFU_EVENT_DFU_FW_SAVE_ADDR_CONFLICT,
		    0);
		return FALSE;
	}
	dfu_state->file_size = bin_size;
	data[0] |= dfu_state->fast_dfu_mode;
	if (!fu_memcpy_safe(data,
			    sizeof(data),
			    1,
			    (const guint8 *)&dfu_state->now_img_info,
			    sizeof(dfu_state->now_img_info),
			    0,
			    sizeof(dfu_state->now_img_info),
			    error))
		return FALSE;
	if (!fu_sunwinon_util_dfu_master_send_frame(self,
						    data,
						    DFU_CMD_PRO_START_DATA_LEN,
						    PROGRAM_START,
						    error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_sunwinon_util_dfu_master_get_info(FuDfuMaster *self, GError **error)
{
	g_debug(__FUNCTION__);
	if (!fu_sunwinon_util_dfu_master_send_frame(self, NULL, 0, GET_INFO, error))
		return FALSE;
	g_debug("DFU Master Get Info Sent");
	return TRUE;
}

static gboolean
fu_sunwinon_util_dfu_master_dfu_mode_set(FuDfuMaster *self, guint8 dfu_mode, GError **error)
{
	g_debug(__FUNCTION__);
	return fu_sunwinon_util_dfu_master_send_frame(self,
						      &dfu_mode,
						      DFU_CMD_MODE_SET_DATA_LEN,
						      DFU_MODE_SET,
						      error);
}

static gboolean
fu_sunwinon_util_dfu_master_fw_info_get(FuDfuMaster *self, GError **error)
{
	g_debug(__FUNCTION__);
	return fu_sunwinon_util_dfu_master_send_frame(self, NULL, 0, DFU_FW_INFO_GET, error);
}

static gboolean
fu_sunwinon_util_dfu_master_system_info_get(FuDfuMaster *self, GError **error)
{
	guint8 data[DFU_CMD_GET_SYSTEM_INFO_DATA_LEN] = {0};
	guint32 addr;
	g_debug(__FUNCTION__);
	data[0] = 0x00;
	addr = FU_SUNWINON_DFU_CONFIG_PERIPHERAL_FLASH_START_ADDR;
	fu_memwrite_uint32(&data[1], addr, G_LITTLE_ENDIAN);
	data[DFU_CMD_GET_SYSTEM_INFO_LEN_L_POS] = DFU_IMAGE_INFO_LEN;
	data[DFU_CMD_GET_SYSTEM_INFO_LEN_H_POS] = 0U;

	return fu_sunwinon_util_dfu_master_send_frame(self,
						      data,
						      DFU_CMD_GET_SYSTEM_INFO_DATA_LEN,
						      SYSTEM_INFO,
						      error);
}

static gboolean
fu_sunwinon_util_dfu_master_program_end(FuDfuMaster *self, GError **error)
{
	FuDfuMasterState *dfu_state = &self->state;
	guint8 data[DFU_CMD_PRO_END_DATA_LEN] = {0};
	g_debug(__FUNCTION__);
	data[0] = (guint8)dfu_state->run_fw_flag;
	fu_memwrite_uint32(&data[1], dfu_state->all_check_sum, G_LITTLE_ENDIAN);
	return fu_sunwinon_util_dfu_master_send_frame(self,
						      data,
						      DFU_CMD_PRO_END_DATA_LEN,
						      PROGRAM_END,
						      error);
}

static gboolean
fu_sunwinon_util_dfu_master_get_sec_flag(FuDfuMaster *self)
{
	FuDfuMasterState *dfu_state = &self->state;
	return dfu_state->sec_flag;
}

static void
fu_sunwinon_util_dfu_master_timeout_schedule(FuDfuMaster *self)
{
	FuDfuMasterState *dfu_state = &self->state;
	if (!dfu_state->dfu_timeout_started)
		return;

	if ((fu_sunwinon_util_dfu_master_get_time(self) - dfu_state->dfu_timeout_start_time) >
	    FU_SUNWINON_DFU_CONFIG_ACK_WAIT_TIMEOUT) {
		dfu_state->dfu_timeout_started = FALSE;
		fu_sunwinon_util_dfu_master_event_handler(self,
							  FU_SUNWINON_DFU_EVENT_DFU_ACK_TIMEOUT,
							  0);
	}
}

guint32
fu_sunwinon_util_dfu_master_get_program_size(FuDfuMaster *self)
{
	FuDfuMasterState *dfu_state = &self->state;
	return dfu_state->programed_size;
}

void
fu_sunwinon_util_dfu_master_send_data_cmpl_process(FuDfuMaster *self)
{
	FuDfuMasterState *dfu_state = &self->state;
	guint16 remain = dfu_state->all_send_len - dfu_state->sent_len;

	if (remain >= dfu_state->once_size) {
		(void)fu_sunwinon_util_dfu_master_send_data(
		    self,
		    &dfu_state->dfu_tx_buf[dfu_state->sent_len],
		    dfu_state->once_size,
		    NULL);
		dfu_state->sent_len += dfu_state->once_size;
	} else if (remain > 0U) {
		(void)fu_sunwinon_util_dfu_master_send_data(
		    self,
		    &dfu_state->dfu_tx_buf[dfu_state->sent_len],
		    remain,
		    NULL);
		dfu_state->sent_len += remain;
	}
	/* else nothing to do */
}

void
fu_sunwinon_util_dfu_master_fast_send_data_cmpl_process(FuDfuMaster *self)
{
	FuDfuMasterState *dfu_state = &self->state;
	dfu_state->ble_fast_send_cplt_flag = 1;
}

void
fu_sunwinon_util_dfu_master_cmd_parse(FuDfuMaster *self, const guint8 *data, guint16 len)
{
	FuDfuMasterState *dfu_state = &self->state;
	guint16 i = 0;

	if (!dfu_state->cmd_receive_flag) {
		for (i = 0; i < len; i++) {
			switch (dfu_state->parse_state) {
			case CHECK_FRAME_L_STATE:
				dfu_state->receive_check_sum = 0;
				if (data[i] == CMD_FRAME_HEADER_L)
					dfu_state->parse_state = CHECK_FRAME_H_STATE;
				break;

			case CHECK_FRAME_H_STATE:
				if (data[i] == CMD_FRAME_HEADER_H)
					dfu_state->parse_state = RECEIVE_CMD_TYPE_L_STATE;
				else if (data[i] == CMD_FRAME_HEADER_L)
					dfu_state->parse_state = CHECK_FRAME_H_STATE;
				else
					dfu_state->parse_state = CHECK_FRAME_L_STATE;
				break;

			case RECEIVE_CMD_TYPE_L_STATE:
				dfu_state->receive_frame.cmd_type = data[i];
				dfu_state->receive_check_sum += data[i];
				dfu_state->parse_state = RECEIVE_CMD_TYPE_H_STATE;
				break;

			case RECEIVE_CMD_TYPE_H_STATE:
				dfu_state->receive_frame.cmd_type |= ((guint16)data[i] << 8);
				dfu_state->receive_check_sum += data[i];
				dfu_state->parse_state = RECEIVE_LEN_L_STATE;
				break;

			case RECEIVE_LEN_L_STATE:
				dfu_state->receive_frame.data_len = data[i];
				dfu_state->receive_check_sum += data[i];
				dfu_state->parse_state = RECEIVE_LEN_H_STATE;
				break;

			case RECEIVE_LEN_H_STATE:
				dfu_state->receive_frame.data_len |= ((guint16)data[i] << 8);
				dfu_state->receive_check_sum += data[i];
				if (dfu_state->receive_frame.data_len == 0U)
					dfu_state->parse_state = RECEIVE_CHECK_SUM_L_STATE;
				else if (dfu_state->receive_frame.data_len >=
					 FU_SUNWINON_DFU_FRAME_MAX_RX)
					dfu_state->parse_state = CHECK_FRAME_L_STATE;
				else {
					dfu_state->receive_data_count = 0;
					dfu_state->parse_state = RECEIVE_DATA_STATE;
				}
				break;

			case RECEIVE_DATA_STATE:
				dfu_state->receive_frame.data[dfu_state->receive_data_count] =
				    data[i];
				if (++dfu_state->receive_data_count ==
				    dfu_state->receive_frame.data_len)
					dfu_state->parse_state = RECEIVE_CHECK_SUM_L_STATE;
				break;

			case RECEIVE_CHECK_SUM_L_STATE:
				dfu_state->receive_frame.check_sum = data[i];
				dfu_state->parse_state = RECEIVE_CHECK_SUM_H_STATE;
				break;

			case RECEIVE_CHECK_SUM_H_STATE:
				dfu_state->receive_frame.check_sum |= ((guint16)data[i] << 8);
				dfu_state->parse_state = CHECK_FRAME_L_STATE;
				fu_sunwinon_util_dfu_master_cmd_check(self);
				break;

			default:
				dfu_state->parse_state = CHECK_FRAME_L_STATE;
				break;
			}
		}
	}
}

FuDfuMaster *
fu_sunwinon_util_dfu_master_new(const FuSunwinonDfuCallback *dfu_m_func_cfg, guint16 once_send_size)
{
	g_return_val_if_fail(dfu_m_func_cfg != NULL, NULL);
	g_autoptr(FuDfuMaster) self = g_new0(FuDfuMaster, 1);
	FuDfuMasterState *dfu_state = &self->state;
	dfu_state->fast_dfu_mode = FU_SUNWINON_FAST_DFU_MODE_DISABLE;
	dfu_state->parse_state = CHECK_FRAME_L_STATE;
	if (once_send_size != 0U)
		dfu_state->once_size = once_send_size;
	if (dfu_m_func_cfg != NULL)
		/* copy function table to local managed state */
		dfu_state->func_cfg = *dfu_m_func_cfg;
	return g_steal_pointer(&self);
}

gboolean
fu_sunwinon_util_dfu_master_start(FuDfuMaster *self, GError **error)
{
	FuDfuMasterState *dfu_state = &self->state;
	g_debug("DFU Master Start");
	if (!fu_sunwinon_util_dfu_master_get_info(self, error))
		return FALSE;
	dfu_state->dfu_timeout_started = TRUE;
	dfu_state->dfu_timeout_start_time = fu_sunwinon_util_dfu_master_get_time(self);
	return TRUE;
}

void
fu_sunwinon_util_dfu_master_parse_state_reset(FuDfuMaster *self)
{
	FuDfuMasterState *dfu_state = &self->state;
	dfu_state->parse_state = CHECK_FRAME_L_STATE;
	dfu_state->cmd_receive_flag = FALSE;
	dfu_state->receive_data_count = 0;
	dfu_state->receive_check_sum = 0;
	dfu_state->dfu_timeout_started = FALSE;
}

void
fu_sunwinon_util_dfu_master_free(FuDfuMaster *self)
{
	g_free(self);
}

void
fu_sunwinon_util_dfu_master_fast_dfu_mode_set(FuDfuMaster *self, guint8 setting)
{
	FuDfuMasterState *dfu_state = &self->state;
	dfu_state->fast_dfu_mode = setting;
}

guint8
fu_sunwinon_util_dfu_master_fast_dfu_mode_get(FuDfuMaster *self)
{
	FuDfuMasterState *dfu_state = &self->state;
	return dfu_state->fast_dfu_mode;
}

#define DFU_ERASE_STATUS_REGION_NOT_ALIGNED 0x00U
#define DFU_ERASE_STATUS_START_SUCCESS	    0x01U
#define DFU_ERASE_STATUS_SUCCESS	    0x02U
#define DFU_ERASE_STATUS_END_SUCCESS	    0x03U
#define DFU_ERASE_STATUS_REGIONS_OVERLAP    0x04U
#define DFU_ERASE_STATUS_FAIL		    0x05U
#define DFU_ERASE_STATUS_REGIONS_NOT_EXIST  0x06U

static gboolean
fu_sunwinon_util_dfu_master_schedule_program_start(FuDfuMaster *self, GError **error)
{
	FuDfuMasterState *dfu_state = &self->state;
	guint16 erased_sectors = 0;
	guint8 progress = 0;
	if (FU_SUNWINON_FAST_DFU_MODE_DISABLE == dfu_state->fast_dfu_mode) {
		if (!fu_sunwinon_util_dfu_master_program_flash(
			self,
			(guint16)FU_SUNWINON_DFU_CONFIG_ONCE_PROGRAM_LEN,
			error))
			return FALSE;
		fu_sunwinon_util_dfu_master_event_handler(self,
							  FU_SUNWINON_DFU_EVENT_PRO_START_SUCCESS,
							  0);
	} else if (FU_SUNWINON_FAST_DFU_MODE_ENABLE == dfu_state->fast_dfu_mode) {
		switch (dfu_state->receive_frame.data[1]) {
		case DFU_ERASE_STATUS_START_SUCCESS:
			dfu_state->erase_sectors =
			    (guint16)(dfu_state->receive_frame.data[DFU_RSP_ERASE_POS]);
			dfu_state->erase_sectors |=
			    (((guint16)dfu_state->receive_frame.data[DFU_RSP_ERASE_POS + 1] << 8) &
			     0xff00U);
			fu_sunwinon_util_dfu_master_event_handler(
			    self,
			    FU_SUNWINON_DFU_EVENT_ERASE_START_SUCCESS,
			    0);
			break;

		case DFU_ERASE_STATUS_SUCCESS:
			erased_sectors =
			    (guint16)(dfu_state->receive_frame.data[DFU_RSP_ERASE_POS]);
			erased_sectors |=
			    (((guint16)dfu_state->receive_frame.data[DFU_RSP_ERASE_POS + 1] << 8) &
			     0xff00U);
			progress = (guint8)((erased_sectors * 100U) / dfu_state->erase_sectors);
			fu_sunwinon_util_dfu_master_event_handler(
			    self,
			    FU_SUNWINON_DFU_EVENT_ERASE_SUCCESS,
			    progress);
			break;

		case DFU_ERASE_STATUS_END_SUCCESS:
			fu_sunwinon_util_dfu_master_event_handler(
			    self,
			    FU_SUNWINON_DFU_EVENT_ERASE_END_SUCCESS,
			    0);
			fu_sunwinon_util_dfu_master_fast_program_flash(self);
			break;

		case DFU_ERASE_STATUS_REGION_NOT_ALIGNED:
			fu_sunwinon_util_dfu_master_event_handler(
			    self,
			    FU_SUNWINON_DFU_EVENT_ERASE_REGION_NOT_ALIGNED,
			    0);
			break;

		case DFU_ERASE_STATUS_REGIONS_OVERLAP:
			fu_sunwinon_util_dfu_master_event_handler(
			    self,
			    FU_SUNWINON_DFU_EVENT_ERASE_REGION_OVERLAP,
			    0);
			break;

		case DFU_ERASE_STATUS_FAIL:
			fu_sunwinon_util_dfu_master_event_handler(
			    self,
			    FU_SUNWINON_DFU_EVENT_ERASE_FLASH_FAIL,
			    0);
			break;

		case DFU_ERASE_STATUS_REGIONS_NOT_EXIST:
			fu_sunwinon_util_dfu_master_event_handler(
			    self,
			    FU_SUNWINON_DFU_EVENT_ERASE_REGION_NOT_EXIST,
			    0);
			break;

		default:
			/* nothing to do */
			break;
		}
	}
	return TRUE;
}

static void
fu_sunwinon_util_dfu_master_schedule_program_end(FuDfuMaster *self)
{
	FuDfuMasterState *dfu_state = &self->state;
	if (dfu_state->fast_dfu_mode == FU_SUNWINON_FAST_DFU_MODE_ENABLE) {
		guint32 check_sum =
		    fu_memread_uint32(&dfu_state->receive_frame.data[1], G_LITTLE_ENDIAN);
		if (check_sum == dfu_state->all_check_sum) {
			dfu_state->dfu_timeout_started = FALSE;
			fu_sunwinon_util_dfu_master_event_handler(
			    self,
			    FU_SUNWINON_DFU_EVENT_PRO_END_SUCCESS,
			    0);
		} else {
			dfu_state->dfu_timeout_started = FALSE;
			fu_sunwinon_util_dfu_master_event_handler(
			    self,
			    FU_SUNWINON_DFU_EVENT_PRO_END_FAIL,
			    0);
		}
	} else if (dfu_state->fast_dfu_mode == FU_SUNWINON_FAST_DFU_MODE_DISABLE) {
		dfu_state->dfu_timeout_started = FALSE;
		fu_sunwinon_util_dfu_master_event_handler(self,
							  FU_SUNWINON_DFU_EVENT_PRO_END_SUCCESS,
							  0);
	}
}

gboolean
fu_sunwinon_util_dfu_master_schedule(FuDfuMaster *self, GError **error)
{
	FuDfuMasterState *dfu_state = &self->state;
	guint8 progress = 0;
	if (dfu_state->cmd_receive_flag) {
		if (dfu_state->dfu_timeout_started)
			dfu_state->dfu_timeout_start_time =
			    fu_sunwinon_util_dfu_master_get_time(self);

		switch (dfu_state->receive_frame.cmd_type) {
		case GET_INFO:
			dfu_state->dfu_save_addr = 0;
			if (dfu_state->receive_frame.data[0] == ACK_SUCCESS) {
				if (dfu_state->receive_frame.data[DFU_RSP_DFU_VERSION_POS] ==
				    FU_SUNWINON_DFU_VERSION)
					dfu_state->new_version_flag = TRUE; /* new dfu version */
				else
					dfu_state->new_version_flag = FALSE; /* old dfu version */

				if (!fu_sunwinon_util_dfu_master_system_info_get(self, error))
					return FALSE;
			} else
				fu_sunwinon_util_dfu_master_event_handler(
				    self,
				    FU_SUNWINON_DFU_EVENT_GET_INFO_FAIL,
				    0);
			break;

		case SYSTEM_INFO:
			if (dfu_state->receive_frame.data[0] == ACK_SUCCESS) {
				/* security mode */
				if (0U != (dfu_state->receive_frame.data[DFU_RSP_SYS_INFO_OP_POS] &
					   0xF0U))
					dfu_state->sec_flag = TRUE;
				else
					dfu_state->sec_flag = FALSE;

				if (!fu_memcpy_safe((guint8 *)&dfu_state->boot_info,
						    sizeof(dfu_state->boot_info),
						    0,
						    dfu_state->receive_frame.data,
						    dfu_state->receive_frame.data_len,
						    DFU_RSP_SYS_INFO_DATA_POS,
						    sizeof(FuSunwinonDfuBootInfo),
						    error))
					return FALSE;

				if (dfu_state->new_version_flag &&
				    !fu_sunwinon_util_dfu_master_fw_info_get(self, error))
					return FALSE;
			}
			break;

		case DFU_FW_INFO_GET:
			if (dfu_state->receive_frame.data[0] == ACK_SUCCESS) {
				dfu_state->dfu_save_addr =
				    fu_memread_uint32(&dfu_state->receive_frame.data[1],
						      G_LITTLE_ENDIAN);

				if (!fu_memcpy_safe((guint8 *)&dfu_state->app_info,
						    sizeof(dfu_state->app_info),
						    0,
						    dfu_state->receive_frame.data,
						    dfu_state->receive_frame.data_len,
						    DFU_RSP_IMG_INFO_POS,
						    sizeof(FuSunwinonDfuImageInfo),
						    error)) {
					g_warning("dfu: copy app info failed");
					return FALSE;
				}

				if (!fu_sunwinon_util_dfu_master_dfu_mode_set(
					self,
					FU_SUNWINON_DFU_UPGRADE_MODE_COPY,
					error))
					return FALSE;

				/* The command DFU_MODE_SET has no response. Wait at least 100ms */
				/* before sending the next command */
				fu_sunwinon_util_dfu_master_wait(self, 100U);
				if (!fu_sunwinon_util_dfu_master_program_start(
					self,
					fu_sunwinon_util_dfu_master_get_sec_flag(self),
					TRUE,
					error))
					return FALSE; /* run new fw after DFU */
			} else
				g_debug("DFU_FW_INFO_GET ERROR");
			break;

		case PROGRAM_START:
			if (dfu_state->receive_frame.data[0] == ACK_SUCCESS) {
				if (!fu_sunwinon_util_dfu_master_schedule_program_start(self,
											error))
					return FALSE;
			} else
				fu_sunwinon_util_dfu_master_event_handler(
				    self,
				    FU_SUNWINON_DFU_EVENT_PRO_START_ERROR,
				    0);

			break;

		case PROGRAM_FLASH:
			if (dfu_state->receive_frame.data[0] == ACK_SUCCESS) {
				progress = (guint8)((dfu_state->programed_size * 100U) /
						    dfu_state->file_size);
				fu_sunwinon_util_dfu_master_event_handler(
				    self,
				    FU_SUNWINON_DFU_EVENT_PRO_FLASH_SUCCESS,
				    progress);
				if (dfu_state->programed_size == dfu_state->file_size) {
					if (!fu_sunwinon_util_dfu_master_program_end(self, error))
						return FALSE;
				} else if ((dfu_state->programed_size +
					    (guint32)FU_SUNWINON_DFU_CONFIG_ONCE_PROGRAM_LEN) >
					   dfu_state->file_size) {
					if (!fu_sunwinon_util_dfu_master_program_flash(
						self,
						(guint16)(dfu_state->file_size -
							  dfu_state->programed_size),
						error))
						return FALSE;
				} else if (!fu_sunwinon_util_dfu_master_program_flash(
					       self,
					       (guint16)FU_SUNWINON_DFU_CONFIG_ONCE_PROGRAM_LEN,
					       error))
					return FALSE;
			} else
				fu_sunwinon_util_dfu_master_event_handler(
				    self,
				    FU_SUNWINON_DFU_EVENT_PRO_FLASH_FAIL,
				    progress);

			break;

		case PROGRAM_END:
			if (dfu_state->receive_frame.data[0] == ACK_SUCCESS)
				fu_sunwinon_util_dfu_master_schedule_program_end(self);
			else
				fu_sunwinon_util_dfu_master_event_handler(
				    self,
				    FU_SUNWINON_DFU_EVENT_PRO_END_FAIL,
				    0);

			break;

		case DFU_FAST_DFU_FLASH_SUCCESS:
			if (dfu_state->receive_frame.data[0] == ACK_SUCCESS) {
				dfu_state->receive_frame.data[0] = (guint8)dfu_state->run_fw_flag;
				fu_memwrite_uint32(&dfu_state->receive_frame.data[1],
						   dfu_state->all_check_sum,
						   G_LITTLE_ENDIAN);
				if (!fu_sunwinon_util_dfu_master_send_frame(
					self,
					dfu_state->receive_frame.data,
					DFU_CMD_PRO_END_DATA_LEN,
					PROGRAM_END,
					error))
					return FALSE;
			} else
				fu_sunwinon_util_dfu_master_event_handler(
				    self,
				    FU_SUNWINON_DFU_EVENT_FAST_DFU_FLASH_FAIL,
				    0);

			break;

		default:
			/* nothing to do */
			break;
		}

		dfu_state->cmd_receive_flag = FALSE;
	}

	fu_sunwinon_util_dfu_master_timeout_schedule(self);
	return TRUE;
}

gboolean
fu_sunwinon_util_dfu_master_send_fw_info_get(FuDfuMaster *self, GError **error)
{
	return fu_sunwinon_util_dfu_master_send_frame(self, NULL, 0, DFU_FW_INFO_GET, error);
}

gboolean
fu_sunwinon_util_dfu_master_parse_fw_info(FuDfuMaster *self,
					  FuSunwinonDfuImageInfo *img_info,
					  const guint8 *data,
					  guint16 len,
					  GError **error)
{
	FuDfuMasterState *dfu_state = &self->state;
	fu_sunwinon_util_dfu_master_cmd_parse(self, data, len);
	if (!dfu_state->cmd_receive_flag) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "Frame check error");
		return FALSE;
	}
	if (!fu_memcpy_safe((guint8 *)img_info,
			    sizeof(FuSunwinonDfuImageInfo),
			    0,
			    data,
			    len,
			    DFU_FRAME_DATA_POS + DFU_RSP_IMG_INFO_POS,
			    sizeof(FuSunwinonDfuImageInfo),
			    error))
		return FALSE;
	return TRUE;
}
