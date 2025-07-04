/*
 * Copyright 2022 Hai Su <hsu@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-kinetic-dp-puma-device.h"
#include "fu-kinetic-dp-puma-firmware.h"

/* Kinetic proprietary DPCD fields for Puma in both application and ISP driver */
#define PUMA_DPCD_SINK_MODE_REG	 0x0050D
#define PUMA_DPCD_CMD_STATUS_REG 0x0050E

#define PUMA_DPCD_DATA_ADDR	0x80000ul
#define PUMA_DPCD_DATA_SIZE	0x8000ul /* 0x80000ul ~ 0x87FFF, 32 KB*/
#define PUMA_DPCD_DATA_ADDR_END (PUMA_DPCD_DATA_ADDR + PUMA_DPCD_DATA_SIZE - 1)

#define PUMA_CHUNK_PROCESS_MAX_WAIT		    10000 /* max wait time in ms to process 32KB chunk */
#define FU_KINETIC_DP_PUMA_REQUEST_FLASH_ERASE_TIME 2  /* typical erase time, ms */
#define POLL_INTERVAL_MS			    20 /* check the status of installing FW images */

struct _FuKineticDpPumaDevice {
	FuKineticDpDevice parent_instance;
	guint16 read_flash_prog_time;
	guint16 flash_id;
	guint16 flash_size;
};

G_DEFINE_TYPE(FuKineticDpPumaDevice, fu_kinetic_dp_puma_device, FU_TYPE_KINETIC_DP_DEVICE)

static void
fu_kinetic_dp_puma_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuKineticDpPumaDevice *self = FU_KINETIC_DP_PUMA_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "ReadFlashProgTime", self->read_flash_prog_time);
	fwupd_codec_string_append_hex(str, idt, "FlashId", self->flash_id);
	fwupd_codec_string_append_hex(str, idt, "FlashSize", self->flash_size);
}

static gboolean
fu_kinetic_dp_puma_device_wait_dpcd_cmd_status_cb(FuDevice *device,
						  gpointer user_data,
						  GError **error)
{
	FuKineticDpPumaDevice *self = FU_KINETIC_DP_PUMA_DEVICE(device);
	guint8 status = 0;
	guint8 status_want = GPOINTER_TO_UINT(user_data);

	if (!fu_dpaux_device_read(FU_DPAUX_DEVICE(self),
				  PUMA_DPCD_CMD_STATUS_REG,
				  &status,
				  sizeof(status),
				  FU_KINETIC_DP_DEVICE_TIMEOUT,
				  error)) {
		g_prefix_error(error, "failed to read PUMA_DPCD_CMD_STATUS_REG for status: ");
		return FALSE;
	}
	if (status != status_want) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "flash mode was %s, wanted %s",
			    fu_kinetic_dp_puma_mode_to_string(status),
			    fu_kinetic_dp_puma_mode_to_string(status_want));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_kinetic_dp_puma_device_wait_dpcd_sink_mode_cb(FuDevice *device,
						 gpointer user_data,
						 GError **error)
{
	FuKineticDpPumaDevice *self = FU_KINETIC_DP_PUMA_DEVICE(device);
	guint8 status = 0;
	guint8 status_want = GPOINTER_TO_UINT(user_data);

	if (!fu_dpaux_device_read(FU_DPAUX_DEVICE(self),
				  PUMA_DPCD_SINK_MODE_REG,
				  &status,
				  sizeof(status),
				  FU_KINETIC_DP_DEVICE_TIMEOUT,
				  error)) {
		g_prefix_error(error, "failed to read PUMA_DPCD_SINK_MODE_REG for status: ");
		return FALSE;
	}
	if (status != status_want) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "flash mode was %s, wanted %s",
			    fu_kinetic_dp_puma_mode_to_string(status),
			    fu_kinetic_dp_puma_mode_to_string(status_want));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_kinetic_dp_puma_device_enter_code_loading_mode(FuKineticDpPumaDevice *self, GError **error)
{
	guint8 cmd = FU_KINETIC_DP_PUMA_REQUEST_CODE_LOAD_REQUEST;

	/* send cmd */
	if (!fu_dpaux_device_write(FU_DPAUX_DEVICE(self),
				   PUMA_DPCD_SINK_MODE_REG,
				   &cmd,
				   sizeof(cmd),
				   FU_KINETIC_DP_DEVICE_TIMEOUT,
				   error)) {
		g_prefix_error(error,
			       "failed to write PUMA_DPCD_SINK_MODE_REG with "
			       "CODE_LOAD_REQUEST: ");
		return FALSE;
	}

	/* wait for the command to be processed */
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_kinetic_dp_puma_device_wait_dpcd_sink_mode_cb,
				  5,
				  POLL_INTERVAL_MS,
				  GUINT_TO_POINTER(FU_KINETIC_DP_PUMA_REQUEST_CODE_LOAD_READY),
				  error)) {
		g_prefix_error(error, "timeout waiting for REQUEST_FW_UPDATE_READY: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_kinetic_dp_puma_device_send_chunk(FuKineticDpPumaDevice *self,
				     FuIOChannel *io_channel,
				     GBytes *fw,
				     GError **error)
{
	g_autoptr(FuChunkArray) chunks =
	    fu_chunk_array_new_from_bytes(fw, FU_CHUNK_ADDR_OFFSET_NONE, FU_CHUNK_PAGESZ_NONE, 16);
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_dpaux_device_write(FU_DPAUX_DEVICE(self),
					   PUMA_DPCD_DATA_ADDR + fu_chunk_get_address(chk),
					   fu_chunk_get_data(chk),
					   fu_chunk_get_data_sz(chk),
					   FU_KINETIC_DP_DEVICE_TIMEOUT,
					   error)) {
			g_prefix_error(error, "failed at 0x%x: ", (guint)fu_chunk_get_address(chk));
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_kinetic_dp_puma_device_send_payload(FuKineticDpPumaDevice *self,
				       FuIOChannel *io_channel,
				       GBytes *fw,
				       FuProgress *progress,
				       guint32 wait_time_ms,
				       gboolean ignore_error,
				       GError **error)
{
	g_autoptr(FuChunkArray) chunks = fu_chunk_array_new_from_bytes(fw,
								       FU_CHUNK_ADDR_OFFSET_NONE,
								       FU_CHUNK_PAGESZ_NONE,
								       PUMA_DPCD_DATA_SIZE);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GBytes) chk_blob = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		/* send a maximum 32KB chunk of payload to AUX window */
		chk_blob = fu_chunk_get_bytes(chk);
		if (!fu_kinetic_dp_puma_device_send_chunk(self, io_channel, chk_blob, error)) {
			g_prefix_error(error,
				       "failed to AUX write at 0x%x: ",
				       (guint)fu_chunk_get_address(chk));
			return FALSE;
		}

		/* check if data chunk received */
		if (!fu_device_retry_full(FU_DEVICE(self),
					  fu_kinetic_dp_puma_device_wait_dpcd_cmd_status_cb,
					  wait_time_ms / POLL_INTERVAL_MS,
					  POLL_INTERVAL_MS,
					  GUINT_TO_POINTER(FU_KINETIC_DP_PUMA_MODE_CHUNK_PROCESSED),
					  error)) {
			g_prefix_error(error, "timeout waiting for MODE_CHUNK_PROCESSED: ");
			return FALSE;
		}
		fu_progress_step_done(progress);
	}
	return TRUE;
}

static gboolean
fu_kinetic_dp_puma_device_wait_drv_ready(FuKineticDpPumaDevice *self,
					 FuIOChannel *io_channel,
					 GError **error)
{
	guint8 flashinfo[FU_STRUCT_KINETIC_DP_FLASH_INFO_SIZE] = {0};
	g_autoptr(GByteArray) st = NULL;

	self->flash_id = 0;
	self->flash_size = 0;
	self->read_flash_prog_time = 10;
	g_debug("wait for isp driver ready...");

	/* wait for the command to be processed */
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_kinetic_dp_puma_device_wait_dpcd_sink_mode_cb,
				  20,
				  POLL_INTERVAL_MS,
				  GUINT_TO_POINTER(FU_KINETIC_DP_PUMA_REQUEST_CODE_BOOTUP_DONE),
				  error)) {
		g_prefix_error(error, "timeout waiting for REQUEST_FW_UPDATE_READY: ");
		return FALSE;
	}
	if (!fu_dpaux_device_read(FU_DPAUX_DEVICE(self),
				  PUMA_DPCD_DATA_ADDR,
				  flashinfo,
				  sizeof(flashinfo),
				  FU_KINETIC_DP_DEVICE_TIMEOUT,
				  error)) {
		g_prefix_error(error, "failed to read Flash Info from Isp Driver: ");
		return FALSE;
	}
	st = fu_struct_kinetic_dp_flash_info_parse(flashinfo, sizeof(flashinfo), 0x0, error);
	self->flash_id = fu_struct_kinetic_dp_flash_info_get_id(st);
	self->flash_size = fu_struct_kinetic_dp_flash_info_get_size(st);
	self->read_flash_prog_time = fu_struct_kinetic_dp_flash_info_get_erase_time(st);
	if (self->read_flash_prog_time == 0)
		self->read_flash_prog_time = FU_KINETIC_DP_PUMA_REQUEST_FLASH_ERASE_TIME;
	return TRUE;
}

static gboolean
fu_kinetic_dp_puma_device_send_isp_drv(FuKineticDpPumaDevice *self,
				       GBytes *fw,
				       FuProgress *progress,
				       GError **error)
{
	FuIOChannel *io_channel = fu_udev_device_get_io_channel(FU_UDEV_DEVICE(self));
	if (!fu_kinetic_dp_puma_device_enter_code_loading_mode(self, error)) {
		g_prefix_error(error, "enter code loading mode failed: ");
		return FALSE;
	}
	fu_kinetic_dp_puma_device_send_payload(self,
					       io_channel,
					       fw,
					       progress,
					       PUMA_CHUNK_PROCESS_MAX_WAIT,
					       TRUE,
					       error);
	if (!fu_kinetic_dp_puma_device_wait_drv_ready(self, io_channel, error)) {
		g_prefix_error(error, "wait for ISP driver ready failed: ");
		return FALSE;
	}
	if (self->flash_size >= 0x400)
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
	return TRUE;
}

static gboolean
fu_kinetic_dp_puma_device_enable_fw_update_mode(FuKineticDpPumaDevice *self,
						FuKineticDpPumaFirmware *firmware,
						GError **error)
{
	guint8 cmd;

	/* send cmd */
	cmd = FU_KINETIC_DP_PUMA_REQUEST_FW_UPDATE_REQUEST;
	if (!fu_dpaux_device_write(FU_DPAUX_DEVICE(self),
				   PUMA_DPCD_SINK_MODE_REG,
				   &cmd,
				   sizeof(cmd),
				   FU_KINETIC_DP_DEVICE_TIMEOUT,
				   error)) {
		g_prefix_error(error,
			       "failed to write PUMA_DPCD_SINK_MODE_REG with FW_UPDATE_REQUEST: ");
		return FALSE;
	}
	if (fu_kinetic_dp_device_get_fw_state(FU_KINETIC_DP_DEVICE(self)) ==
	    FU_KINETIC_DP_FW_STATE_APP) {
		guint8 flashinfo[FU_STRUCT_KINETIC_DP_FLASH_INFO_SIZE] = {0};
		g_autoptr(GByteArray) st = NULL;

		/* Puma takes about 18ms (Winbond EF13) to get ISP driver ready for flash info */
		fu_device_sleep(FU_DEVICE(self), 18);
		if (!fu_device_retry_full(
			FU_DEVICE(self),
			fu_kinetic_dp_puma_device_wait_dpcd_cmd_status_cb,
			150,
			POLL_INTERVAL_MS,
			GUINT_TO_POINTER(FU_KINETIC_DP_PUMA_MODE_FLASH_INFO_READY),
			error)) {
			g_prefix_error(error, "timeout waiting for MODE_FLASH_INFO_READY: ");
			return FALSE;
		}

		/* get flash info */
		if (!fu_dpaux_device_read(FU_DPAUX_DEVICE(self),
					  PUMA_DPCD_DATA_ADDR,
					  flashinfo,
					  sizeof(flashinfo),
					  FU_KINETIC_DP_DEVICE_TIMEOUT,
					  error)) {
			g_prefix_error(error, "failed to read Flash Info: ");
			return FALSE;
		}
		st =
		    fu_struct_kinetic_dp_flash_info_parse(flashinfo, sizeof(flashinfo), 0x0, error);
		self->flash_id = fu_struct_kinetic_dp_flash_info_get_id(st);
		self->flash_size = fu_struct_kinetic_dp_flash_info_get_size(st);
		self->read_flash_prog_time = fu_struct_kinetic_dp_flash_info_get_erase_time(st);

		/* save flash info need to do memcopy copy here */
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
		if (self->flash_size >= 0x400)
			fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	}

	/* checking for flash erase done */
	g_debug("waiting for flash erasing...");
	if (self->read_flash_prog_time)
		fu_device_sleep(FU_DEVICE(self), self->read_flash_prog_time);
	else
		fu_device_sleep(FU_DEVICE(self), FU_KINETIC_DP_PUMA_REQUEST_FLASH_ERASE_TIME);
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_kinetic_dp_puma_device_wait_dpcd_sink_mode_cb,
				  150,
				  POLL_INTERVAL_MS,
				  GUINT_TO_POINTER(FU_KINETIC_DP_PUMA_REQUEST_FW_UPDATE_READY),
				  error)) {
		g_prefix_error(error, "timeout waiting for REQUEST_FW_UPDATE_READY: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_kinetic_dp_puma_device_setup(FuDevice *device, GError **error)
{
	FuKineticDpPumaDevice *self = FU_KINETIC_DP_PUMA_DEVICE(device);
	guint8 dpcd_buf[3] = {0};
	g_autofree gchar *version = NULL;

	/* FuKineticDpDevice->setup */
	if (!FU_DEVICE_CLASS(fu_kinetic_dp_puma_device_parent_class)->setup(device, error))
		return FALSE;

	/* read major and minor version */
	if (!fu_dpaux_device_read(FU_DPAUX_DEVICE(self),
				  DPCD_ADDR_BRANCH_FW_MAJ_REV,
				  dpcd_buf,
				  2,
				  FU_KINETIC_DP_DEVICE_TIMEOUT,
				  error))
		return FALSE;

	/* read sub */
	if (!fu_dpaux_device_read(FU_DPAUX_DEVICE(self),
				  DPCD_ADDR_BRANCH_FW_SUB,
				  dpcd_buf + 2,
				  1,
				  FU_KINETIC_DP_DEVICE_TIMEOUT,
				  error))
		return FALSE;
	version = g_strdup_printf("%1d.%03d.%02d", dpcd_buf[0], dpcd_buf[1], dpcd_buf[2]);
	fu_device_set_version(device, version);

	/* success */
	return TRUE;
}

static gboolean
fu_kinetic_dp_puma_device_prepare(FuDevice *device,
				  FuProgress *progress,
				  FwupdInstallFlags flags,
				  GError **error)
{
	guint8 mca_oui[DPCD_SIZE_IEEE_OUI] = {MCA_OUI_BYTE_0, MCA_OUI_BYTE_1, MCA_OUI_BYTE_2};
	return fu_kinetic_dp_device_dpcd_write_oui(FU_KINETIC_DP_DEVICE(device), mca_oui, error);
}

static gboolean
fu_kinetic_dp_puma_device_cleanup(FuDevice *device,
				  FuProgress *progress,
				  FwupdInstallFlags flags,
				  GError **error)
{
	FuKineticDpPumaDevice *self = FU_KINETIC_DP_PUMA_DEVICE(device);
	guint8 cmd = FU_KINETIC_DP_PUMA_REQUEST_CHIP_RESET_REQUEST;

	fu_device_sleep(FU_DEVICE(self), 3000);
	if (!fu_dpaux_device_write(FU_DPAUX_DEVICE(self),
				   PUMA_DPCD_SINK_MODE_REG,
				   &cmd,
				   sizeof(cmd),
				   FU_KINETIC_DP_DEVICE_TIMEOUT,
				   error)) {
		g_prefix_error(error,
			       "failed to write PUMA_DPCD_SINK_MODE_REG with CHIP_RESET_REQUEST: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_kinetic_dp_puma_device_write_firmware(FuDevice *device,
					 FuFirmware *firmware,
					 FuProgress *progress,
					 FwupdInstallFlags flags,
					 GError **error)
{
	FuKineticDpPumaDevice *self = FU_KINETIC_DP_PUMA_DEVICE(device);
	FuKineticDpPumaFirmware *dp_firmware = FU_KINETIC_DP_PUMA_FIRMWARE(firmware);
	FuIOChannel *io_channel = fu_udev_device_get_io_channel(FU_UDEV_DEVICE(self));
	g_autoptr(GBytes) app_fw_blob = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 90, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, NULL);

	/* only load driver if in IROM mode */
	if (fu_kinetic_dp_device_get_fw_state(FU_KINETIC_DP_DEVICE(device)) !=
	    FU_KINETIC_DP_FW_STATE_APP) {
		g_autoptr(GBytes) isp_drv_blob = NULL;

		/* get image of ISP driver */
		isp_drv_blob =
		    fu_firmware_get_image_by_idx_bytes(firmware,
						       FU_KINETIC_DP_FIRMWARE_IDX_ISP_DRV,
						       error);
		if (isp_drv_blob == NULL)
			return FALSE;
		if (g_bytes_get_size(isp_drv_blob) > 0) {
			g_debug("loading isp driver because in IROM mode");
			if (!fu_kinetic_dp_puma_device_send_isp_drv(self,
								    isp_drv_blob,
								    fu_progress_get_child(progress),
								    error))
				return FALSE;
		}
	}
	fu_progress_step_done(progress);

	/* enable FW update mode */
	if (!fu_kinetic_dp_puma_device_enable_fw_update_mode(self, dp_firmware, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* send App FW image */
	app_fw_blob =
	    fu_firmware_get_image_by_idx_bytes(firmware, FU_KINETIC_DP_FIRMWARE_IDX_APP_FW, error);
	if (app_fw_blob == NULL)
		return FALSE;
	if (!fu_kinetic_dp_puma_device_send_payload(self,
						    io_channel,
						    app_fw_blob,
						    fu_progress_get_child(progress),
						    PUMA_CHUNK_PROCESS_MAX_WAIT,
						    FALSE,
						    error)) {
		g_prefix_error(error, "sending App Firmware payload failed: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* validate firmware image in Puma */
	fu_device_sleep(FU_DEVICE(self), 100);
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_kinetic_dp_puma_device_wait_dpcd_sink_mode_cb,
				  100,
				  POLL_INTERVAL_MS,
				  GUINT_TO_POINTER(FU_KINETIC_DP_PUMA_REQUEST_FW_UPDATE_DONE),
				  error)) {
		g_prefix_error(error, "validating App Firmware failed: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static void
fu_kinetic_dp_puma_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_kinetic_dp_puma_device_init(FuKineticDpPumaDevice *self)
{
	self->read_flash_prog_time = 10;
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_KINETIC_DP_PUMA_FIRMWARE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
}

static void
fu_kinetic_dp_puma_device_class_init(FuKineticDpPumaDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_kinetic_dp_puma_device_to_string;
	device_class->setup = fu_kinetic_dp_puma_device_setup;
	device_class->prepare = fu_kinetic_dp_puma_device_prepare;
	device_class->cleanup = fu_kinetic_dp_puma_device_cleanup;
	device_class->write_firmware = fu_kinetic_dp_puma_device_write_firmware;
	device_class->set_progress = fu_kinetic_dp_puma_device_set_progress;
}
