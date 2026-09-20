/*
 * Copyright 2026 Realtek Corporation
 * Copyright 2026 Justin Li <justin_li@realsil.com.cn>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-realtek-scaler-common.h"
#include "fu-realtek-scaler-device.h"
#include "fu-realtek-scaler-struct.h"

struct _FuRealtekScalerDevice {
	FuDrmDevice parent_instance;
};

G_DEFINE_TYPE(FuRealtekScalerDevice, fu_realtek_scaler_device, FU_TYPE_DRM_DEVICE)

/* write [sub_addr][buf...] to the 8-bit target address on the DDC (DP AUX) i2c proxy; the
 * address is set for every transfer as the proxy is shared and the scaler uses two of them */
static gboolean
fu_realtek_scaler_device_write(FuRealtekScalerDevice *self,
			       guint8 target8,
			       guint8 sub_addr,
			       const guint8 *buf,
			       gsize bufsz,
			       GError **error)
{
	FuI2cDevice *proxy;
	guint8 frame[REALTEK_SCALER_BLOCK_MAXSZ + 1] = {sub_addr};

	if (bufsz == 0 || bufsz > REALTEK_SCALER_BLOCK_MAXSZ) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "i2c write length out of range");
		return FALSE;
	}
	proxy = FU_I2C_DEVICE(fu_device_get_proxy(FU_DEVICE(self), error));
	if (proxy == NULL)
		return FALSE;
	if (!fu_memcpy_safe(frame, sizeof(frame), 0x1, buf, bufsz, 0x0, bufsz, error))
		return FALSE;
	if (!fu_i2c_device_set_address(proxy, target8 >> 1, FALSE, error))
		return FALSE;
	fu_dump_raw(G_LOG_DOMAIN, "i2c-write", frame, bufsz + 1);
	return fu_i2c_device_write(proxy, frame, bufsz + 1, error);
}

/* write [sub_addr] and then read @bufsz bytes back, as two separate transfers */
static gboolean
fu_realtek_scaler_device_read(FuRealtekScalerDevice *self,
			      guint8 target8,
			      guint8 sub_addr,
			      guint8 *buf,
			      gsize bufsz,
			      GError **error)
{
	FuI2cDevice *proxy;

	proxy = FU_I2C_DEVICE(fu_device_get_proxy(FU_DEVICE(self), error));
	if (proxy == NULL)
		return FALSE;
	if (!fu_i2c_device_set_address(proxy, target8 >> 1, FALSE, error))
		return FALSE;
	if (!fu_i2c_device_write(proxy, &sub_addr, sizeof(sub_addr), error))
		return FALSE;
	if (!fu_i2c_device_read(proxy, buf, bufsz, error))
		return FALSE;
	fu_dump_raw(G_LOG_DOMAIN, "i2c-read", buf, bufsz);
	return TRUE;
}

/* Send a DDCCI-style command to the DDCCI target (0x6E). The DDCCI frame is
 * [target][sub][len|0x80][cmd...][xor-checksum over target..cmd]; the sub
 * address is the first byte on the wire and only [len|0x80][cmd][xor] follows it. */
static gboolean
fu_realtek_scaler_device_ddcci_write(FuRealtekScalerDevice *self,
				     guint8 sub_addr,
				     const guint8 *cmds,
				     gsize cmdsz,
				     GError **error)
{
	guint8 buf[REALTEK_SCALER_BLOCK_MAXSZ] = {0};
	gsize bufsz = cmdsz + 2; /* [len|0x80] + cmds + [xor] */
	guint8 checksum = 0x00;

	if (bufsz > REALTEK_SCALER_BLOCK_MAXSZ) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "ddcci command too long");
		return FALSE;
	}
	buf[0] = cmdsz | FU_REALTEK_SCALER_DDCCI_FLAG_LEN_MSB;
	if (!fu_memcpy_safe(buf + 1, sizeof(buf) - 1, 0x0, cmds, cmdsz, 0x0, cmdsz, error))
		return FALSE;
	/* checksum over target ^ sub ^ [len|0x80] ^ cmds */
	checksum = FU_REALTEK_SCALER_TARGET_ADDR_DDCCI ^ sub_addr;
	for (gsize i = 0; i < cmdsz + 1; i++)
		checksum ^= buf[i];
	buf[cmdsz + 1] = checksum;

	return fu_realtek_scaler_device_write(self,
					      FU_REALTEK_SCALER_TARGET_ADDR_DDCCI,
					      sub_addr,
					      buf,
					      bufsz,
					      error);
}

/* poll the foreground status register until it leaves the busy state, then
 * require an idle-success terminal status -- an idle-fail means the scaler
 * rejected the command and must not be mistaken for success.
 * This is a plain loop rather than fu_device_retry(): a busy status is the
 * normal case and would otherwise log thousands of "failed on try" lines for
 * every update. The status register lives on the ISP target */
static gboolean
fu_realtek_scaler_device_poll_status(FuRealtekScalerDevice *self, GError **error)
{
	guint8 value = FU_REALTEK_SCALER_STATUS_BUSY;

	for (guint i = 0; i < REALTEK_SCALER_STATUS_POLL_RETRY_COUNT; i++) {
		if (!fu_realtek_scaler_device_read(self,
						   FU_REALTEK_SCALER_TARGET_ADDR_FG,
						   FU_REALTEK_SCALER_FG_SUB_ADDR_STATUS,
						   &value,
						   sizeof(value),
						   error))
			return FALSE;
		if (value != FU_REALTEK_SCALER_STATUS_BUSY)
			break;
		fu_device_sleep(FU_DEVICE(self), REALTEK_SCALER_RETRY_DELAY_MS);
	}
	if (value == FU_REALTEK_SCALER_STATUS_BUSY) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_TIMED_OUT,
				    "timed out waiting for the device to leave the busy state");
		return FALSE;
	}
	g_debug("poll_status: read status=0x%02x (%s)",
		value,
		fu_realtek_scaler_status_to_string(value));
	if (value != FU_REALTEK_SCALER_STATUS_IDLE_SUCCESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "device reported status 0x%02x [%s]",
			    value,
			    fu_realtek_scaler_status_to_string(value));
		return FALSE;
	}
	return TRUE;
}

/* enter foreground fw-update mode: send the DDCCI change-mode command on the
 * DDCCI target (0x6E), then read the foreground status register on the FG target
 * (0x3A) once.  Entering the mode is judged solely from this status read, and
 * the outer change_mode() retry re-sends the whole sequence on failure */
static gboolean
fu_realtek_scaler_device_fg_change_mode_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuRealtekScalerDevice *self = FU_REALTEK_SCALER_DEVICE(device);
	guint8 status = 0;
	guint8 buf[3] = {FU_REALTEK_SCALER_DDCCI_OPCODE_VENDOR,
			 FU_REALTEK_SCALER_FG_SUB_ADDR_STATUS,
			 FU_REALTEK_SCALER_FG_SUB_OPCODE_FOREGROUND};

	g_debug("change_mode: entering foreground fw-update mode");
	(void)fu_realtek_scaler_device_ddcci_write(self,
						   FU_REALTEK_SCALER_DDCCI_SUB_ADDR_CMD,
						   buf,
						   sizeof(buf),
						   NULL);

	/* let the scaler settle before reading the FG status register */
	fu_device_sleep(device, REALTEK_SCALER_RECEIVE_CMD_DELAY_BASE_MS);

	/* read the status once; the status lives on the FG target (0x3A) */
	if (!fu_realtek_scaler_device_read(self,
					   FU_REALTEK_SCALER_TARGET_ADDR_FG,
					   FU_REALTEK_SCALER_FG_SUB_ADDR_STATUS,
					   &status,
					   sizeof(status),
					   error)) {
		g_prefix_error_literal(error, "failed to enter foreground mode: ");
		return FALSE;
	}
	g_debug("change_mode: FG mode status=0x%02x (%s)",
		status,
		fu_realtek_scaler_status_to_string(status));
	if (status != FU_REALTEK_SCALER_STATUS_IDLE_SUCCESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "enter foreground mode status was 0x%02x [%s]",
			    status,
			    fu_realtek_scaler_status_to_string(status));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_realtek_scaler_device_fg_change_mode(FuRealtekScalerDevice *self, GError **error)
{
	return fu_device_retry(FU_DEVICE(self),
			       fu_realtek_scaler_device_fg_change_mode_cb,
			       REALTEK_SCALER_FG_MODE_ENTER_RETRY_COUNT,
			       NULL, /* user_data */
			       error);
}

/* leave foreground fw-update mode without resetting, so the panel runs
 * normally again after the version read */
static gboolean
fu_realtek_scaler_device_fg_exit_mode(FuRealtekScalerDevice *self, GError **error)
{
	guint8 cmd = FU_REALTEK_SCALER_FG_OPCODE_FW_UPDATE_EXIT;

	if (!fu_realtek_scaler_device_write(self,
					    FU_REALTEK_SCALER_TARGET_ADDR_FG,
					    FU_REALTEK_SCALER_FG_SUB_ADDR_CMD,
					    &cmd,
					    sizeof(cmd),
					    error)) {
		g_prefix_error_literal(error, "failed to leave foreground mode: ");
		return FALSE;
	}
	g_debug("exit_mode: sent 0x08 to FG target, leaving foreground mode");
	fu_device_sleep(FU_DEVICE(self), REALTEK_SCALER_RECEIVE_CMD_DELAY_BASE_MS);
	return TRUE;
}

/* read the running firmware version while in foreground mode (GET_FW_INFO
 * 0x04, returns 7 bytes at sub-address 0x00) */
static gboolean
fu_realtek_scaler_device_fg_get_version(FuRealtekScalerDevice *self, GError **error)
{
	guint8 req = FU_REALTEK_SCALER_FG_OPCODE_GET_FW_INFO;
	guint8 buf[FU_STRUCT_REALTEK_SCALER_FW_INFO_SIZE] = {0};
	g_autofree gchar *version = NULL;
	g_autoptr(FuStructRealtekScalerFwInfo) st = NULL;

	g_debug("get_version: requesting FW info 0x04 on FG target");
	if (!fu_realtek_scaler_device_write(self,
					    FU_REALTEK_SCALER_TARGET_ADDR_FG,
					    FU_REALTEK_SCALER_FG_SUB_ADDR_CMD,
					    &req,
					    sizeof(req),
					    error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), REALTEK_SCALER_GET_VERSION_WAIT_MS);
	if (!fu_realtek_scaler_device_read(self,
					   FU_REALTEK_SCALER_TARGET_ADDR_FG,
					   0x00,
					   buf,
					   sizeof(buf),
					   error))
		return FALSE;
	st = fu_struct_realtek_scaler_fw_info_parse(buf, sizeof(buf), 0x0, error);
	if (st == NULL)
		return FALSE;
	version = g_strdup_printf("%u.%u",
				  fu_struct_realtek_scaler_fw_info_get_major(st),
				  fu_struct_realtek_scaler_fw_info_get_minor(st));
	fu_device_set_version(FU_DEVICE(self), version);
	return TRUE;
}

/* the version can only be read in foreground mode: enter FG mode, read the
 * version, then leave FG mode again so the panel runs normally */
static gboolean
fu_realtek_scaler_device_fg_read_version(FuRealtekScalerDevice *self, GError **error)
{
	if (!fu_realtek_scaler_device_fg_change_mode(self, error)) {
		g_prefix_error_literal(error, "cannot enter foreground mode: ");
		return FALSE;
	}
	if (!fu_realtek_scaler_device_fg_get_version(self, error)) {
		g_autoptr(GError) error_exit = NULL;
		/* do not leave the panel stuck in foreground mode on failure */
		if (!fu_realtek_scaler_device_fg_exit_mode(self, &error_exit))
			g_warning("failed to exit foreground mode: %s", error_exit->message);
		g_prefix_error_literal(error, "cannot read version: ");
		return FALSE;
	}
	if (!fu_realtek_scaler_device_fg_exit_mode(self, error)) {
		g_prefix_error_literal(error, "cannot exit foreground mode: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_realtek_scaler_device_setup(FuDevice *device, GError **error)
{
	FuRealtekScalerDevice *self = FU_REALTEK_SCALER_DEVICE(device);

	/* FuDrmDevice->setup */
	if (!FU_DEVICE_CLASS(fu_realtek_scaler_device_parent_class)->setup(device, error))
		return FALSE;
	return fu_realtek_scaler_device_fg_read_version(self, error);
}

/* the DRM connector does not necessarily re-enumerate after the scaler resets,
 * so read the version again to show the new firmware after an update */
static gboolean
fu_realtek_scaler_device_reload(FuDevice *device, GError **error)
{
	FuRealtekScalerDevice *self = FU_REALTEK_SCALER_DEVICE(device);
	return fu_realtek_scaler_device_fg_read_version(self, error);
}

static gboolean
fu_realtek_scaler_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuRealtekScalerDevice *self = FU_REALTEK_SCALER_DEVICE(device);

	/* enter foreground ISP mode for the update */
	return fu_realtek_scaler_device_fg_change_mode(self, error);
}

static gboolean
fu_realtek_scaler_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuRealtekScalerDevice *self = FU_REALTEK_SCALER_DEVICE(device);
	guint8 cmd = FU_REALTEK_SCALER_FG_OPCODE_FW_UPDATE_RESET;

	/* exit ISP mode and reset into the newly written firmware (the
	 * firmware commit was already verified by ->write_firmware()) */
	if (!fu_realtek_scaler_device_write(self,
					    FU_REALTEK_SCALER_TARGET_ADDR_FG,
					    FU_REALTEK_SCALER_FG_SUB_ADDR_CMD,
					    &cmd,
					    sizeof(cmd),
					    error)) {
		g_prefix_error_literal(error, "failed to exit foreground mode: ");
		return FALSE;
	}

	/* device needs time to restart with the new firmware */
	fu_device_sleep_full(device, REALTEK_SCALER_ATTACH_RESET_WAIT_MS, progress);
	return TRUE;
}

static gboolean
fu_realtek_scaler_device_write_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	FuRealtekScalerDevice *self = FU_REALTEK_SCALER_DEVICE(device);
	guint8 buf[REALTEK_SCALER_BLOCK_MAXSZ] = {0x00};
	guint8 project_addr[FU_STRUCT_REALTEK_SCALER_PROJECT_ID_ADDR_SIZE] = {0};
	guint32 project_id_addr = 0;
	guint8 project_id_count = 0;
	guint8 req = 0;
	gsize blocksz = REALTEK_SCALER_BLOCK_MAXSZ;
	g_autoptr(FuInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(FuStructRealtekScalerProjectIdAddr) st_project = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "setup");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "start");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 99, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "finish");

	/* simple image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;
	fu_progress_step_done(progress);

	/* enable ISP high priority */
	buf[0] = FU_REALTEK_SCALER_FG_OPCODE_ENTER_HIGH_PRI;
	buf[1] = 0x01;
	if (!fu_realtek_scaler_device_write(self,
					    FU_REALTEK_SCALER_TARGET_ADDR_FG,
					    FU_REALTEK_SCALER_FG_SUB_ADDR_CMD,
					    buf,
					    2,
					    error))
		return FALSE;
	fu_device_sleep(device, REALTEK_SCALER_RECEIVE_CMD_DELAY_BASE_MS);
	if (!fu_realtek_scaler_device_poll_status(self, error)) {
		g_prefix_error_literal(error, "failed to enter ISP high priority mode: ");
		return FALSE;
	}

	/* get project ID address */
	req = FU_REALTEK_SCALER_FG_OPCODE_GET_PROJECT_ID_ADDR;
	if (!fu_realtek_scaler_device_write(self,
					    FU_REALTEK_SCALER_TARGET_ADDR_FG,
					    FU_REALTEK_SCALER_FG_SUB_ADDR_CMD,
					    &req,
					    sizeof(req),
					    error))
		return FALSE;
	fu_device_sleep(device, REALTEK_SCALER_CHECK_PROJECT_ID_WAIT_MS);
	if (!fu_realtek_scaler_device_read(self,
					   FU_REALTEK_SCALER_TARGET_ADDR_FG,
					   FU_REALTEK_SCALER_FG_SUB_ADDR_STATUS,
					   project_addr,
					   sizeof(project_addr),
					   error))
		return FALSE;
	st_project = fu_struct_realtek_scaler_project_id_addr_parse(project_addr,
								    sizeof(project_addr),
								    0x0,
								    error);
	if (st_project == NULL)
		return FALSE;
	if (fu_struct_realtek_scaler_project_id_addr_get_status(st_project) !=
	    FU_REALTEK_SCALER_STATUS_IDLE_SUCCESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to read project ID address, status 0x%02x",
			    fu_struct_realtek_scaler_project_id_addr_get_status(st_project));
		return FALSE;
	}
	project_id_addr = fu_struct_realtek_scaler_project_id_addr_get_addr(st_project);
	project_id_count = fu_struct_realtek_scaler_project_id_addr_get_count(st_project);

	/* the project ID follows the opcode in the VERIFY command, so it has to fit the block */
	if (project_id_count == 0 || project_id_count > sizeof(buf) - 1) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid project ID length %u, expected 1..%u",
			    project_id_count,
			    (guint)sizeof(buf) - 1);
		return FALSE;
	}

	/* verify project ID (read from the firmware image at the reported offset) */
	buf[0] = FU_REALTEK_SCALER_FG_OPCODE_VERIFY_PROJECT_ID;
	if (!fu_input_stream_read_safe(stream,
				       buf,
				       sizeof(buf),
				       0x1,		/* dst */
				       project_id_addr, /* src */
				       project_id_count,
				       error)) {
		g_prefix_error(error,
			       "failed to read project ID from image @0x%04x: ",
			       project_id_addr);
		return FALSE;
	}
	if (!fu_realtek_scaler_device_write(self,
					    FU_REALTEK_SCALER_TARGET_ADDR_FG,
					    FU_REALTEK_SCALER_FG_SUB_ADDR_CMD,
					    buf,
					    project_id_count + 1,
					    error))
		return FALSE;
	fu_device_sleep(device, REALTEK_SCALER_RECEIVE_CMD_DELAY_BASE_MS);
	if (!fu_realtek_scaler_device_poll_status(self, error)) {
		g_prefix_error_literal(error, "failed to verify project ID: ");
		return FALSE;
	}

	/* start command with the packet length */
	buf[0] = FU_REALTEK_SCALER_FG_OPCODE_START;
	fu_memwrite_uint16(buf + 1, (guint16)blocksz, G_BIG_ENDIAN);
	if (!fu_realtek_scaler_device_write(self,
					    FU_REALTEK_SCALER_TARGET_ADDR_FG,
					    FU_REALTEK_SCALER_FG_SUB_ADDR_CMD,
					    buf,
					    3,
					    error))
		return FALSE;
	fu_device_sleep(device, REALTEK_SCALER_RECEIVE_CMD_DELAY_BASE_MS);
	fu_progress_step_done(progress);

	/* write data in blocks */
	chunks = fu_chunk_array_new_from_stream(stream,
						FU_CHUNK_ADDR_OFFSET_NONE,
						FU_CHUNK_PAGESZ_NONE,
						blocksz,
						error);
	if (chunks == NULL)
		return FALSE;
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		/* poll-then-write handshake for each data block: wait until the
		 * device is ready for the next packet, then send it */
		if (!fu_realtek_scaler_device_poll_status(self, error)) {
			g_prefix_error(error,
				       "failed to write @0x%04x: ",
				       (guint)fu_chunk_get_address(chk));
			return FALSE;
		}
		if (!fu_realtek_scaler_device_write(self,
						    FU_REALTEK_SCALER_TARGET_ADDR_FG,
						    FU_REALTEK_SCALER_FG_SUB_ADDR_DATA,
						    fu_chunk_get_data(chk),
						    fu_chunk_get_data_sz(chk),
						    error)) {
			g_prefix_error(error,
				       "failed to write @0x%04x: ",
				       (guint)fu_chunk_get_address(chk));
			return FALSE;
		}
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						(gsize)i + 1,
						(gsize)fu_chunk_array_length(chunks));
	}
	fu_progress_step_done(progress);

	/* finish: wait for the last data block to complete */
	if (!fu_realtek_scaler_device_poll_status(self, error)) {
		g_prefix_error_literal(error, "write did not complete: ");
		return FALSE;
	}
	buf[0] = FU_REALTEK_SCALER_FG_OPCODE_FINISH;
	if (!fu_realtek_scaler_device_write(self,
					    FU_REALTEK_SCALER_TARGET_ADDR_FG,
					    FU_REALTEK_SCALER_FG_SUB_ADDR_CMD,
					    buf,
					    sizeof(guint8),
					    error)) {
		g_prefix_error_literal(error, "failed to send finish command: ");
		return FALSE;
	}
	fu_device_sleep(device, REALTEK_SCALER_RECEIVE_CMD_DELAY_BASE_MS);
	/* wait for the finish command to complete; this is the decisive check
	 * that the firmware was committed successfully.  The last byte of the
	 * image is a CRC which the scaler verifies itself once the write has
	 * finished, so no further image validation is done on the host. */
	if (!fu_realtek_scaler_device_poll_status(self, error)) {
		g_prefix_error_literal(error, "firmware commit failed: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success -- the device is reset into the new firmware by ->attach() */
	return TRUE;
}

static void
fu_realtek_scaler_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 87, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 13, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_realtek_scaler_device_init(FuRealtekScalerDevice *self)
{
	/* the DDC (DP AUX) i2c proxy is created by the udev backend; the quirk GType switch
	 * drops it unless the proxy type is declared here */
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FOR_OPEN);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REFCOUNTED_PROXY);
	fu_device_set_proxy_gtype(FU_DEVICE(self), FU_TYPE_I2C_DEVICE);
	/* 5ms between foreground-mode enter retries */
	fu_device_retry_set_delay(FU_DEVICE(self), REALTEK_SCALER_RETRY_DELAY_MS);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
	fu_device_add_protocol(FU_DEVICE(self), "com.realtek.scaler");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_EMULATION_TAG);
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_VIDEO_DISPLAY);
	fu_device_set_vendor(FU_DEVICE(self), "Realtek");
	/* linux_display also enumerates this DRM connector, with the same device ID */
	fu_device_set_priority(FU_DEVICE(self), 1);
}

static void
fu_realtek_scaler_device_class_init(FuRealtekScalerDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_realtek_scaler_device_setup;
	device_class->reload = fu_realtek_scaler_device_reload;
	device_class->detach = fu_realtek_scaler_device_detach;
	device_class->attach = fu_realtek_scaler_device_attach;
	device_class->write_firmware = fu_realtek_scaler_device_write_firmware;
	device_class->set_progress = fu_realtek_scaler_device_set_progress;
}
