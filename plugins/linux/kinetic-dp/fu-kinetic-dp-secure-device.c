/*
 * Copyright 2021 Jeffrey Lin <jlin@kinet-ic.com>
 * Copyright 2022 Hai Su <hsu@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-kinetic-dp-secure-device.h"
#include "fu-kinetic-dp-secure-firmware.h"
#include "fu-kinetic-dp-struct.h"

struct _FuKineticDpSecureDevice {
	FuKineticDpDevice parent_instance;
	guint16 read_flash_prog_time;
	guint16 flash_id;
	guint16 flash_size;
	gboolean isp_secure_auth_mode;
	FuKineticDpBank flash_bank;
};

G_DEFINE_TYPE(FuKineticDpSecureDevice, fu_kinetic_dp_secure_device, FU_TYPE_KINETIC_DP_DEVICE)

/* Kinetic proprietary DPCD fields for JaguarMustang, for both application and ISP driver */
#define DPCD_ADDR_CMD_STATUS_REG 0x0050D
#define DPCD_ADDR_PARAM_REG	 0x0050E

/* DPCD registers are used while running ISP driver */
#define DPCD_ADDR_ISP_REPLY_LEN_REG 0x00513
#define DPCD_SIZE_ISP_REPLY_LEN_REG 1

#define DPCD_ADDR_ISP_REPLY_DATA_REG 0x00514 /* during ISP driver */
#define DPCD_SIZE_ISP_REPLY_DATA_REG 12	     /* 0x00514 ~ 0x0051F*/

#define DPCD_ADDR_KT_AUX_WIN	 0x80000ul
#define DPCD_SIZE_KT_AUX_WIN	 0x8000ul /* 0x80000ul ~ 0x87FFF, 32 KB */
#define DPCD_ADDR_KT_AUX_WIN_END (DPCD_ADDR_KT_AUX_WIN + DPCD_SIZE_KT_AUX_WIN - 1)

#define DPCD_KT_CONFIRMATION_BIT 0x80
#define DPCD_KT_COMMAND_MASK	 0x7F

/* polling interval to check the status of installing FW images */
#define INSTALL_IMAGE_POLL_INTERVAL_MS 50

static void
fu_kinetic_dp_secure_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuKineticDpSecureDevice *self = FU_KINETIC_DP_SECURE_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "ReadFlashProgTime", self->read_flash_prog_time);
	fwupd_codec_string_append_hex(str, idt, "FlashId", self->flash_id);
	fwupd_codec_string_append_hex(str, idt, "FlashSize", self->flash_size);
	fwupd_codec_string_append_hex(str, idt, "IspSecureAuthMode", self->isp_secure_auth_mode);
	fwupd_codec_string_append(str,
				  idt,
				  "FlashBank",
				  fu_kinetic_dp_bank_to_string(self->flash_bank));
}

static gboolean
fu_kinetic_dp_secure_device_read_param_reg(FuKineticDpSecureDevice *self,
					   guint8 *dpcd_val,
					   GError **error)
{
	if (!fu_dpaux_device_read(FU_DPAUX_DEVICE(self),
				  DPCD_ADDR_PARAM_REG,
				  dpcd_val,
				  1,
				  FU_KINETIC_DP_DEVICE_TIMEOUT,
				  error)) {
		g_prefix_error(error, "failed to read DPCD_KT_PARAM_REG: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_kinetic_dp_secure_device_write_kt_prop_cmd(FuKineticDpSecureDevice *self,
					      guint8 cmd_id,
					      GError **error)
{
	cmd_id |= DPCD_KT_CONFIRMATION_BIT;

	if (!fu_dpaux_device_write(FU_DPAUX_DEVICE(self),
				   DPCD_ADDR_CMD_STATUS_REG,
				   &cmd_id,
				   sizeof(cmd_id),
				   FU_KINETIC_DP_DEVICE_TIMEOUT,
				   error)) {
		g_prefix_error(error, "failed to write DPCD_KT_CMD_STATUS_REG: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_kinetic_dp_secure_device_clear_kt_prop_cmd(FuKineticDpSecureDevice *self, GError **error)
{
	guint8 cmd_id = FU_KINETIC_DP_DPCD_CMD_STS_NONE;

	if (!fu_dpaux_device_write(FU_DPAUX_DEVICE(self),
				   DPCD_ADDR_CMD_STATUS_REG,
				   &cmd_id,
				   sizeof(cmd_id),
				   FU_KINETIC_DP_DEVICE_TIMEOUT,
				   error)) {
		g_prefix_error(error, "failed to write DPCD_KT_CMD_STATUS_REG: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_kinetic_dp_secure_device_send_kt_prop_cmd_cb(FuDevice *device,
						gpointer user_data,
						GError **error)
{
	FuKineticDpSecureDevice *self = FU_KINETIC_DP_SECURE_DEVICE(device);
	guint8 status = (guint8)FU_KINETIC_DP_DPCD_CMD_STS_NONE;
	guint8 cmd_id = GPOINTER_TO_UINT(user_data);

	if (!fu_dpaux_device_read(FU_DPAUX_DEVICE(self),
				  DPCD_ADDR_CMD_STATUS_REG,
				  &status,
				  sizeof(status),
				  FU_KINETIC_DP_DEVICE_TIMEOUT,
				  error)) {
		g_prefix_error(error, "failed to read DPCD_ADDR_CMD_STATUS_REG: ");
		return FALSE;
	}

	/* target responded */
	if (status != (cmd_id | (guint8)DPCD_KT_CONFIRMATION_BIT)) {
		if (status != cmd_id) {
			status &= DPCD_KT_COMMAND_MASK;
			if (status == (guint8)FU_KINETIC_DP_DPCD_STS_CRC_FAILURE) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_DATA,
						    "chunk data CRC failed: ");
				return FALSE;
			}
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "Invalid value in DPCD_KT_CMD_STATUS_REG: 0x%x",
				    status);
			return FALSE;
		}
		/* confirmation bit is cleared by sink, means sent command is processed */
		return TRUE;
	}

	/* failed */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_DATA,
		    "waiting for prop cmd, got %s",
		    fu_kinetic_dp_dpcd_to_string(status));
	return FALSE;
}

static gboolean
fu_kinetic_dp_secure_device_send_kt_prop_cmd(FuKineticDpSecureDevice *self,
					     guint8 cmd_id,
					     guint32 max_time_ms,
					     guint16 poll_interval_ms,
					     guint8 *status,
					     GError **error)
{
	if (!fu_kinetic_dp_secure_device_write_kt_prop_cmd(self, cmd_id, error))
		return FALSE;

	/* wait for the sent proprietary command to be processed */
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_kinetic_dp_secure_device_send_kt_prop_cmd_cb,
				  max_time_ms / poll_interval_ms,
				  poll_interval_ms,
				  GUINT_TO_POINTER(cmd_id),
				  error)) {
		g_prefix_error(error, "timeout waiting for prop command: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_kinetic_dp_secure_device_read_dpcd_reply_data_reg(FuKineticDpSecureDevice *self,
						     guint8 *buf,
						     gsize bufsz,
						     guint8 *read_len,
						     GError **error)
{
	guint8 read_data_len;

	/* set the output to 0 */
	*read_len = 0;
	if (!fu_dpaux_device_read(FU_DPAUX_DEVICE(self),
				  DPCD_ADDR_ISP_REPLY_LEN_REG,
				  &read_data_len,
				  1,
				  FU_KINETIC_DP_DEVICE_TIMEOUT,
				  error)) {
		g_prefix_error(error, "failed to read DPCD_ISP_REPLY_DATA_LEN_REG: ");
		return FALSE;
	}

	if (bufsz < read_data_len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "buffer size [%u] is not enough to read DPCD_ISP_REPLY_DATA_REG [%u]",
			    (guint)bufsz,
			    read_data_len);
		return FALSE;
	}

	if (read_data_len > 0) {
		if (!fu_dpaux_device_read(FU_DPAUX_DEVICE(self),
					  DPCD_ADDR_ISP_REPLY_DATA_REG,
					  buf,
					  read_data_len,
					  FU_KINETIC_DP_DEVICE_TIMEOUT,
					  error)) {
			g_prefix_error(error, "failed to read DPCD_ISP_REPLY_DATA_REG: ");
			return FALSE;
		}
		*read_len = read_data_len;
	}

	return TRUE;
}

static gboolean
fu_kinetic_dp_secure_device_write_dpcd_reply_data_reg(FuKineticDpSecureDevice *self,
						      const guint8 *buf,
						      gsize len,
						      GError **error)
{
	guint8 len_u8 = len;

	if (len > DPCD_SIZE_ISP_REPLY_DATA_REG) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "length bigger than DPCD_SIZE_ISP_REPLY_DATA_REG [%u]",
			    (guint)len);
		return FALSE;
	}

	if (!fu_dpaux_device_write(FU_DPAUX_DEVICE(self),
				   DPCD_ADDR_ISP_REPLY_DATA_REG,
				   buf,
				   len,
				   FU_KINETIC_DP_DEVICE_TIMEOUT,
				   error)) {
		g_prefix_error(error, "failed to write DPCD_KT_REPLY_DATA_REG: ");
		return FALSE;
	}
	return fu_dpaux_device_write(FU_DPAUX_DEVICE(self),
				     DPCD_ADDR_ISP_REPLY_LEN_REG,
				     &len_u8,
				     sizeof(len_u8),
				     FU_KINETIC_DP_DEVICE_TIMEOUT,
				     error);
}

static gboolean
fu_kinetic_dp_secure_device_write_mca_oui(FuKineticDpSecureDevice *self, GError **error)
{
	guint8 mca_oui[DPCD_SIZE_IEEE_OUI] = {MCA_OUI_BYTE_0, MCA_OUI_BYTE_1, MCA_OUI_BYTE_2};
	return fu_kinetic_dp_device_dpcd_write_oui(FU_KINETIC_DP_DEVICE(self), mca_oui, error);
}

static gboolean
fu_kinetic_dp_secure_device_enter_code_loading_mode(FuKineticDpSecureDevice *self,
						    guint32 code_size,
						    GError **error)
{
	guint8 status = 0x0;
	guint8 buf[0x4] = {0x0};

	if (fu_kinetic_dp_device_get_fw_state(FU_KINETIC_DP_DEVICE(self)) ==
	    FU_KINETIC_DP_FW_STATE_APP) {
		/* make DPCD 514 writable */
		if (!fu_kinetic_dp_secure_device_send_kt_prop_cmd(
			self,
			FU_KINETIC_DP_DPCD_CMD_PREPARE_FOR_ISP_MODE,
			500,
			10,
			&status,
			error))
			return FALSE;
	}

	/* update payload size to DPCD reply data reg first */
	fu_memwrite_uint32(buf, code_size, G_LITTLE_ENDIAN);
	if (!fu_kinetic_dp_secure_device_write_dpcd_reply_data_reg(self, buf, sizeof(buf), error))
		return FALSE;
	if (!fu_kinetic_dp_secure_device_send_kt_prop_cmd(
		self,
		FU_KINETIC_DP_DPCD_CMD_ENTER_CODE_LOADING_MODE,
		500,
		10,
		&status,
		error))
		return FALSE;

	/* success */
	return TRUE;
}

/**
 * fu_kinetic_dp_secure_device_crc16:
 * @buf: memory buffer
 * @bufsz: size of @buf
 *
 * Returns the cyclic redundancy check value for the given memory buffer.
 * This is a proprietary implementation only can be used in Kinetic's
 * Secure AUX-ISP protocol
 *
 * Returns: CRC value
 **/
static guint16
fu_kinetic_dp_secure_device_crc16(const guint8 *buf, gsize bufsize)
{
	guint16 crc = 0x1021;
	for (gsize i = 0; i < bufsize; i++) {
		guint16 crc_tmp = crc;
		guint8 data = buf[i];

		for (guint8 j = 8; j; j--) {
			guint8 flag = data ^ (crc_tmp >> 8);
			crc_tmp <<= 1;
			if (flag & 0x80)
				crc_tmp ^= 0x1021;
			data <<= 1;
		}
		crc = crc_tmp;
	}
	return crc;
}

static gboolean
fu_kinetic_dp_secure_device_send_chunk(FuKineticDpSecureDevice *self,
				       GBytes *fw,
				       FuProgress *progress,
				       GError **error)
{
	g_autoptr(FuChunkArray) chunks =
	    fu_chunk_array_new_from_bytes(fw, FU_CHUNK_ADDR_OFFSET_NONE, FU_CHUNK_PAGESZ_NONE, 16);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_dpaux_device_write(FU_DPAUX_DEVICE(self),
					   DPCD_ADDR_KT_AUX_WIN + fu_chunk_get_address(chk),
					   fu_chunk_get_data(chk),
					   fu_chunk_get_data_sz(chk),
					   FU_KINETIC_DP_DEVICE_TIMEOUT,
					   error)) {
			g_prefix_error(error, "failed at 0x%x: ", (guint)fu_chunk_get_address(chk));
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_kinetic_dp_secure_device_send_payload(FuKineticDpSecureDevice *self,
					 GBytes *fw,
					 guint32 wait_time_ms,
					 gint32 wait_interval_ms,
					 FuProgress *progress,
					 GError **error)
{
	g_autoptr(FuChunkArray) chunks = fu_chunk_array_new_from_bytes(fw,
								       FU_CHUNK_ADDR_OFFSET_NONE,
								       FU_CHUNK_PAGESZ_NONE,
								       DPCD_SIZE_KT_AUX_WIN);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GBytes) fw_chk = NULL;
		guint8 buf_crc16[0x4] = {0x0};
		guint8 status = 0;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		/* send a maximum 32KB chunk of payload to AUX window */
		fw_chk = fu_chunk_get_bytes(chk);
		if (!fu_kinetic_dp_secure_device_send_chunk(self,
							    fw_chk,
							    fu_progress_get_child(progress),
							    error)) {
			g_prefix_error(error,
				       "failed to AUX write at 0x%x: ",
				       (guint)fu_chunk_get_address(chk));
			return FALSE;
		}

		/* send the CRC16 of current 32KB chunk to DPCD_REPLY_DATA_REG */
		fu_memwrite_uint32(buf_crc16,
				   fu_kinetic_dp_secure_device_crc16(fu_chunk_get_data(chk),
								     fu_chunk_get_data_sz(chk)),
				   G_LITTLE_ENDIAN);
		if (!fu_kinetic_dp_secure_device_write_dpcd_reply_data_reg(self,
									   buf_crc16,
									   sizeof(buf_crc16),
									   error)) {
			g_prefix_error(error, "failed to send CRC16 to reply data register: ");
			return FALSE;
		}

		/* notify that a chunk of payload has been sent to AUX window */
		if (!fu_kinetic_dp_secure_device_send_kt_prop_cmd(
			self,
			FU_KINETIC_DP_DPCD_CMD_CHUNK_DATA_PROCESSED,
			wait_time_ms,
			wait_interval_ms,
			&status,
			error)) {
			g_prefix_error(error, "target failed to process payload chunk: ");
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_kinetic_dp_secure_device_wait_dpcd_cmd_cleared_cb(FuDevice *device,
						     gpointer user_data,
						     GError **error)
{
	FuKineticDpSecureDevice *self = FU_KINETIC_DP_SECURE_DEVICE(device);
	guint8 status = FU_KINETIC_DP_DPCD_CMD_STS_NONE;

	if (!fu_dpaux_device_read(FU_DPAUX_DEVICE(self),
				  DPCD_ADDR_CMD_STATUS_REG,
				  &status,
				  sizeof(status),
				  FU_KINETIC_DP_DEVICE_TIMEOUT,
				  error))
		return FALSE;

	/* status is cleared by sink */
	if (status == FU_KINETIC_DP_DPCD_CMD_STS_NONE)
		return TRUE;

	if ((status & DPCD_KT_CONFIRMATION_BIT) > 0) {
		/* status is not cleared but confirmation bit is cleared,
		 * it means that target responded with failure */
		if (status == FU_KINETIC_DP_DPCD_STS_INVALID_IMAGE) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "invalid ISP driver");
			return FALSE;
		}
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to execute ISP driver");
		return FALSE;
	}

	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "waiting for sink to clear status");
	return FALSE;
}

static gboolean
fu_kinetic_dp_secure_device_wait_dpcd_cmd_cleared(FuKineticDpSecureDevice *self,
						  guint16 wait_time_ms,
						  guint16 poll_interval_ms,
						  GError **error)
{
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_kinetic_dp_secure_device_wait_dpcd_cmd_cleared_cb,
				  wait_time_ms / poll_interval_ms,
				  poll_interval_ms,
				  NULL,
				  error)) {
		g_prefix_error(error, "timeout waiting for DPCD_ISP_SINK_STATUS_REG: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_kinetic_dp_secure_device_execute_isp_drv(FuKineticDpSecureDevice *self, GError **error)
{
	guint8 status;
	guint8 read_len;
	guint8 reply_data[6] = {0};
	g_autoptr(GByteArray) st = NULL;

	/* in Jaguar, it takes about FU_KINETIC_DP_DEVICE_TIMEOUT ms to boot up and initialize */
	self->flash_id = 0;
	self->flash_size = 0;
	self->read_flash_prog_time = 10;

	if (!fu_kinetic_dp_secure_device_write_kt_prop_cmd(self,
							   FU_KINETIC_DP_DPCD_CMD_EXECUTE_RAM_CODE,
							   error))
		return FALSE;

	if (!fu_kinetic_dp_secure_device_wait_dpcd_cmd_cleared(self, 1500, 100, error))
		return FALSE;
	if (!fu_kinetic_dp_secure_device_read_param_reg(self, &status, error))
		return FALSE;

	if (status != FU_KINETIC_DP_DPCD_STS_SECURE_ENABLED &&
	    status != FU_KINETIC_DP_DPCD_STS_SECURE_DISABLED) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_TIMED_OUT,
				    "waiting for ISP driver ready failed!");
		return FALSE;
	}
	self->isp_secure_auth_mode = (status == FU_KINETIC_DP_DPCD_STS_SECURE_ENABLED);

	if (!fu_kinetic_dp_secure_device_read_dpcd_reply_data_reg(self,
								  reply_data,
								  sizeof(reply_data),
								  &read_len,
								  error)) {
		g_prefix_error(error, "failed to read flash ID and size: ");
		return FALSE;
	}
	st = fu_struct_kinetic_dp_flash_info_parse(reply_data, sizeof(reply_data), 0x0, error);
	self->flash_id = fu_struct_kinetic_dp_flash_info_get_id(st);
	self->flash_size = fu_struct_kinetic_dp_flash_info_get_size(st);
	self->read_flash_prog_time = fu_struct_kinetic_dp_flash_info_get_erase_time(st);
	if (self->read_flash_prog_time == 0)
		self->read_flash_prog_time = 10;

	/* one bank size in Jaguar is 1024KB */
	if (self->flash_size >= 2048)
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	if (self->flash_size == 0) {
		if (self->flash_id > 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "SPI flash not supported");
			return FALSE;
		}
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "SPI flash not connected");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_kinetic_dp_secure_device_send_isp_drv(FuKineticDpSecureDevice *self,
					 GBytes *fw,
					 FuProgress *progress,
					 GError **error)
{
	if (!fu_kinetic_dp_secure_device_enter_code_loading_mode(self,
								 g_bytes_get_size(fw),
								 error)) {
		g_prefix_error(error, "enabling code-loading mode failed: ");
		return FALSE;
	}

	if (!fu_kinetic_dp_secure_device_send_payload(self, fw, 10000, 50, progress, error)) {
		g_prefix_error(error, "sending ISP driver payload failed: ");
		return FALSE;
	}

	g_debug("sending ISP driver payload...");
	if (!fu_kinetic_dp_secure_device_execute_isp_drv(self, error)) {
		g_prefix_error(error, "ISP driver booting up failed: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_kinetic_dp_secure_device_enable_fw_update_mode(FuKineticDpSecureFirmware *firmware,
						  FuKineticDpSecureDevice *self,
						  GError **error)
{
	guint8 status;
	guint8 buf[12] = {0};

	/* send payload size to DPCD_MCA_REPLY_DATA_REG */
	if (!fu_memwrite_uint32_safe(buf,
				     sizeof(buf),
				     0,
				     fu_kinetic_dp_secure_firmware_get_esm_payload_size(firmware),
				     G_LITTLE_ENDIAN,
				     error))
		return FALSE;

	if (!fu_memwrite_uint32_safe(buf,
				     sizeof(buf),
				     4,
				     fu_kinetic_dp_secure_firmware_get_arm_app_code_size(firmware),
				     G_LITTLE_ENDIAN,
				     error))
		return FALSE;

	if (!fu_memwrite_uint16_safe(buf,
				     sizeof(buf),
				     8,
				     fu_kinetic_dp_secure_firmware_get_app_init_data_size(firmware),
				     G_LITTLE_ENDIAN,
				     error))
		return FALSE;

	if (!fu_memwrite_uint16_safe(
		buf,
		sizeof(buf),
		10,
		(fu_kinetic_dp_secure_firmware_get_esm_xip_enabled(firmware) ? (1 << 15) : 0) |
		    fu_kinetic_dp_secure_firmware_get_cmdb_block_size(firmware),
		G_LITTLE_ENDIAN,
		error))
		return FALSE;

	if (!fu_kinetic_dp_secure_device_write_dpcd_reply_data_reg(self, buf, sizeof(buf), error)) {
		g_prefix_error(error, "send payload size failed: ");
		return FALSE;
	}

	if (!fu_kinetic_dp_secure_device_send_kt_prop_cmd(
		self,
		FU_KINETIC_DP_DPCD_CMD_ENTER_FW_UPDATE_MODE,
		200000,
		500,
		&status,
		error)) {
		g_prefix_error(error, "entering F/W update mode failed: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_kinetic_dp_secure_device_send_app_fw(FuKineticDpSecureDevice *self,
					FuKineticDpSecureFirmware *firmware,
					GBytes *fw,
					FuProgress *progress,
					GError **error)
{
	g_autoptr(GBytes) fw_app = NULL;
	g_autoptr(GBytes) fw_app_init = NULL;
	g_autoptr(GBytes) fw_app_data = NULL;
	g_autoptr(GBytes) fw_esm = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "send-sigs");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 38, "send-esm");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 57, "send-app");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 3, "send-initialized");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "send-app-id");

	/* send ESM and App Certificates & RSA Signatures */
	if (self->isp_secure_auth_mode) {
		g_autoptr(GBytes) fw_crt = NULL;

		fw_crt =
		    fu_bytes_new_offset(fw,
					0x0,
					FW_CERTIFICATE_SIZE * 2 + FW_RSA_SIGNATURE_BLOCK_SIZE * 2,
					error);
		if (fw_crt == NULL)
			return FALSE;
		if (!fu_kinetic_dp_secure_device_send_payload(self,
							      fw_crt,
							      10000,
							      200,
							      fu_progress_get_child(progress),
							      error)) {
			g_prefix_error(error, "failed to send certificates: ");
			return FALSE;
		}
	}
	fu_progress_step_done(progress);

	/* send ESM code */
	fw_esm = fu_bytes_new_offset(fw,
				     SPI_ESM_PAYLOAD_START,
				     fu_kinetic_dp_secure_firmware_get_esm_payload_size(firmware),
				     error);
	if (fw_esm == NULL)
		return FALSE;
	if (!fu_kinetic_dp_secure_device_send_payload(self,
						      fw_esm,
						      10000,
						      200,
						      fu_progress_get_child(progress),
						      error)) {
		g_prefix_error(error, "failed to send ESM payload: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* send App code */
	fw_app = fu_bytes_new_offset(fw,
				     SPI_APP_PAYLOAD_START,
				     fu_kinetic_dp_secure_firmware_get_arm_app_code_size(firmware),
				     error);
	if (fw_app == NULL)
		return FALSE;
	if (!fu_kinetic_dp_secure_device_send_payload(self,
						      fw_app,
						      10000,
						      200,
						      fu_progress_get_child(progress),
						      error)) {
		g_prefix_error(error, "failed to send App FW payload: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* send App initialized data */
	fw_app_init =
	    fu_bytes_new_offset(fw,
				fu_kinetic_dp_secure_firmware_get_esm_xip_enabled(firmware)
				    ? SPI_APP_EXTEND_INIT_DATA_START
				    : SPI_APP_NORMAL_INIT_DATA_START,
				fu_kinetic_dp_secure_firmware_get_app_init_data_size(firmware),
				error);
	if (fw_app_init == NULL)
		return FALSE;
	if (!fu_kinetic_dp_secure_device_send_payload(self,
						      fw_app_init,
						      10000,
						      200,
						      fu_progress_get_child(progress),
						      error)) {
		g_prefix_error(error, "failed to send App init data: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* send Application Identifier */
	fw_app_data = fu_bytes_new_offset(fw,
					  SPI_APP_ID_DATA_START,
					  FU_STRUCT_KINETIC_DP_JAGUAR_FOOTER_SIZE,
					  error);
	if (fw_app_data == NULL)
		return FALSE;
	if (!fu_kinetic_dp_secure_device_send_payload(self,
						      fw_app_data,
						      10000,
						      200,
						      fu_progress_get_child(progress),
						      error)) {
		g_prefix_error(error, "failed to send App ID data: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_kinetic_dp_secure_device_install_fw_images_cb(FuDevice *device,
						 gpointer user_data,
						 GError **error)
{
	FuKineticDpSecureDevice *self = FU_KINETIC_DP_SECURE_DEVICE(device);
	guint8 status = 0;

	if (!fu_dpaux_device_read(FU_DPAUX_DEVICE(self),
				  DPCD_ADDR_CMD_STATUS_REG,
				  &status,
				  sizeof(status),
				  FU_KINETIC_DP_DEVICE_TIMEOUT,
				  error)) {
		g_prefix_error(error, "failed to read DPCD_MCA_CMD_REG: ");
		return FALSE;
	}

	/* confirmation bit is cleared */
	if ((status & DPCD_KT_CONFIRMATION_BIT) == 0) {
		if ((status & FU_KINETIC_DP_DPCD_CMD_INSTALL_IMAGES) > 0)
			return TRUE;
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "failed to install images");
		return FALSE;
	}

	/* failed */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_DATA,
		    "waiting for status, got %s",
		    fu_kinetic_dp_dpcd_to_string(status));
	return FALSE;
}

static gboolean
fu_kinetic_dp_secure_device_install_fw_images(FuKineticDpSecureDevice *self, GError **error)
{
	guint8 cmd_id = FU_KINETIC_DP_DPCD_CMD_INSTALL_IMAGES;

	if (!fu_kinetic_dp_secure_device_write_kt_prop_cmd(self, cmd_id, error)) {
		g_prefix_error(error, "failed to send DPCD command: ");
		return FALSE;
	}

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_kinetic_dp_secure_device_install_fw_images_cb,
				  1500,
				  INSTALL_IMAGE_POLL_INTERVAL_MS,
				  NULL,
				  error)) {
		g_prefix_error(error, "timeout waiting for install command to be processed ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_kinetic_dp_secure_device_get_flash_bank_idx(FuKineticDpSecureDevice *self, GError **error)
{
	guint8 status;
	guint8 buf[DPCD_SIZE_IEEE_OUI] = {0};
	guint8 res = FU_KINETIC_DP_BANK_NONE;

	if (!fu_kinetic_dp_device_dpcd_read_oui(FU_KINETIC_DP_DEVICE(self),
						buf,
						sizeof(buf),
						error))
		return FALSE;
	if (!fu_kinetic_dp_secure_device_write_mca_oui(self, error))
		return FALSE;
	if (!fu_kinetic_dp_secure_device_send_kt_prop_cmd(
		self,
		FU_KINETIC_DP_DPCD_CMD_GET_ACTIVE_FLASH_BANK,
		100,
		20,
		&status,
		error))
		return FALSE;
	if (!fu_kinetic_dp_secure_device_read_param_reg(self, &res, error))
		return FALSE;
	if (!fu_kinetic_dp_secure_device_clear_kt_prop_cmd(self, error))
		return FALSE;

	/* restore previous source OUI */
	if (!fu_kinetic_dp_device_dpcd_write_oui(FU_KINETIC_DP_DEVICE(self), buf, error))
		return FALSE;

	g_debug("secure aux got active flash bank 0x%x (0=BankA, 1=BankB, 2=TotalBanks)", res);
	self->flash_bank = (FuKineticDpBank)res;
	if (self->flash_bank == FU_KINETIC_DP_BANK_NONE) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "bank not NONE");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gchar *
fu_kinetic_dp_secure_device_convert_version(FuDevice *device, guint64 version_raw)
{
	guint8 buf[3] = {0};
	fu_memwrite_uint24(buf, version_raw, G_LITTLE_ENDIAN);
	return g_strdup_printf("%1d.%03d.%02d", buf[2], buf[1], buf[0]);
}

static gboolean
fu_kinetic_dp_secure_device_setup(FuDevice *device, GError **error)
{
	FuKineticDpSecureDevice *self = FU_KINETIC_DP_SECURE_DEVICE(device);

	/* FuKineticDpDevice->setup */
	if (!FU_DEVICE_CLASS(fu_kinetic_dp_secure_device_parent_class)->setup(device, error))
		return FALSE;

	/* get flash info */
	if (fu_kinetic_dp_device_get_fw_state(FU_KINETIC_DP_DEVICE(device)) ==
	    FU_KINETIC_DP_FW_STATE_APP) {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_DUAL_IMAGE);
		if (!fu_kinetic_dp_secure_device_get_flash_bank_idx(self, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_kinetic_dp_secure_device_prepare(FuDevice *device,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuKineticDpSecureDevice *self = FU_KINETIC_DP_SECURE_DEVICE(device);
	return fu_kinetic_dp_secure_device_write_mca_oui(self, error);
}

static gboolean
fu_kinetic_dp_secure_device_cleanup(FuDevice *device,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuKineticDpSecureDevice *self = FU_KINETIC_DP_SECURE_DEVICE(device);

	/* wait for flash clear to settle */
	fu_device_sleep(FU_DEVICE(self), 2000);

	/* send reset command */
	return fu_kinetic_dp_secure_device_write_kt_prop_cmd(self,
							     FU_KINETIC_DP_DPCD_CMD_RESET_SYSTEM,
							     error);
}

static gboolean
fu_kinetic_dp_secure_device_write_firmware(FuDevice *device,
					   FuFirmware *firmware,
					   FuProgress *progress,
					   FwupdInstallFlags flags,
					   GError **error)
{
	FuKineticDpSecureDevice *self = FU_KINETIC_DP_SECURE_DEVICE(device);
	FuKineticDpSecureFirmware *dp_firmware = FU_KINETIC_DP_SECURE_FIRMWARE(firmware);
	g_autoptr(GBytes) app_fw_blob = NULL;
	g_autoptr(GBytes) isp_drv_blob = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 3, "isp");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 97, "app");

	/* get image of ISP driver */
	isp_drv_blob =
	    fu_firmware_get_image_by_idx_bytes(firmware, FU_KINETIC_DP_FIRMWARE_IDX_ISP_DRV, error);
	if (isp_drv_blob == NULL)
		return FALSE;

	/* send ISP driver and execute it */
	if (g_bytes_get_size(isp_drv_blob) > 0) {
		if (!fu_kinetic_dp_secure_device_send_isp_drv(self,
							      isp_drv_blob,
							      fu_progress_get_child(progress),
							      error))
			return FALSE;
	}
	fu_progress_step_done(progress);

	/* enable FW update mode */
	if (!fu_kinetic_dp_secure_device_enable_fw_update_mode(dp_firmware, self, error))
		return FALSE;

	/* get image of App FW */
	app_fw_blob =
	    fu_firmware_get_image_by_idx_bytes(firmware, FU_KINETIC_DP_FIRMWARE_IDX_APP_FW, error);
	if (app_fw_blob == NULL)
		return FALSE;
	if (!fu_kinetic_dp_secure_device_send_app_fw(self,
						     dp_firmware,
						     app_fw_blob,
						     fu_progress_get_child(progress),
						     error))
		return FALSE;
	fu_progress_step_done(progress);

	/* install FW images */
	return fu_kinetic_dp_secure_device_install_fw_images(self, error);
}

static void
fu_kinetic_dp_secure_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_kinetic_dp_secure_device_init(FuKineticDpSecureDevice *self)
{
	self->read_flash_prog_time = 10;
	self->isp_secure_auth_mode = TRUE;
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_KINETIC_DP_SECURE_FIRMWARE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_retry_add_recovery(FU_DEVICE(self), FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, NULL);
}

static void
fu_kinetic_dp_secure_device_class_init(FuKineticDpSecureDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_kinetic_dp_secure_device_to_string;
	device_class->prepare = fu_kinetic_dp_secure_device_prepare;
	device_class->cleanup = fu_kinetic_dp_secure_device_cleanup;
	device_class->setup = fu_kinetic_dp_secure_device_setup;
	device_class->write_firmware = fu_kinetic_dp_secure_device_write_firmware;
	device_class->set_progress = fu_kinetic_dp_secure_device_set_progress;
	device_class->convert_version = fu_kinetic_dp_secure_device_convert_version;
}
