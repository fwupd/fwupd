/*
 * Copyright (C) 2022 Hai Su <hsu@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-kinetic-dp-connection.h"
#include "fu-kinetic-dp-puma-aux-isp.h"

/* certificate + ESM + Signature + hash + certificate + Puma App + Signature + hash */
#define PUMA_FW_HEADER_OBJECT_MAX 8
#define PUMA_FW_HEADER_LENGTH_MAX                                                                  \
	2 + PUMA_FW_HEADER_OBJECT_MAX * sizeof(HeaderInfoFormat) /*50 bytes*/

#define PUMA_FW_HEADER_SIZE	 50
#define PUMA_FW_HASH_SIZE	 32
#define PUMA_STS_FW_PAYLOAD_SIZE (SIZE_512KB + PUMA_FW_HEADER_SIZE + (PUMA_FW_HASH_SIZE * 2))

/* Puma STD F/W SPI mapping */
#define PUMA_FW_STD_VER_START_ADDR (PUMA_STS_FW_PAYLOAD_SIZE - 52) /*0x8003E*/

/* Puma STD F/W CMDB */
#define PUMA_CMDB_SIZE		  128
#define PUMA_FW_CMDB_SIG_SIZE	  4
#define PUMA_FW_CMDB_START_ADDR	  0x7FE52
#define PUMA_FW_CMDB_STD_VER_ADDR 0x7FE56
#define PUMA_FW_CMDB_REV_ADDR	  0x7FE58
#define PUMA_FW_CMDB_REV_SIZE	  3

/* Kinetic proprietary DPCD fields for Puma in both application and ISP driver */
#define PUMA_DPCD_SINK_MODE_REG	 0x0050D
#define PUMA_DPCD_CMD_STATUS_REG 0x0050E

#define PUMA_DPCD_DATA_ADDR	0x80000ul
#define PUMA_DPCD_DATA_SIZE	0x8000ul /* 0x80000ul ~ 0x87FFF, 32 KB*/
#define PUMA_DPCD_DATA_ADDR_END (PUMA_DPCD_DATA_ADDR + PUMA_DPCD_DATA_SIZE - 1)
/* max wait time in ms to enter code load mode */
#define PUMA_CODE_LOAD_READY_MAX_WAIT 100
/* max wait time in ms to process 32KB chunk */
#define PUMA_CHUNK_PROCESS_MAX_WAIT 10000
/* driver takes about 120ms to come up. max wait is 250ms */
#define PUMA_ISP_DRV_MAX_WAIT 250
/* max wait time for flash become ready */
#define PUMA_FLASH_READY_MAX_WAIT 3000
/* typical Puma flash erase time */
#define PUMA_FLASH_ERASE_TIME 2000
/* max wait time for flash erase done */
#define PUMA_FLASH_ERASE_MAX_WAIT 3000
/* max wait time for fw validation */
#define PUMA_FW_VALIDATE_MAX_WAIT 2000
/* polling interval to check the status of installing FW images */
#define POLL_INTERVAL_MS 20

#define BYTES_TO_GUINT32(value)                                                                    \
	(guint32)(value[3] | ((guint32)value[2] << 8) | ((guint32)value[1] << 16) |                \
		  ((guint32)value[0] << 24))

typedef enum {
	PUMA_CHIP_RESET_REQUEST = 0,
	PUMA_CODE_LOAD_REQUEST = 0x01,
	PUMA_CODE_LOAD_READY = 0x03,
	PUMA_CODE_BOOTUP_DONE = 0x07,
	PUMA_CMDB_GETINFO_REQ = 0xA0,
	PUMA_CMDB_GETINFO_READ = 0xA1,
	PUMA_CMDB_GETINFO_INVALID = 0xA2,
	PUMA_CMDB_GETINFO_DONE = 0xA3,
	PUMA_FLASH_ERASE_DONE = 0xE0,
	PUMA_FLASH_ERASE_FAIL = 0xE1,
	PUMA_FLASH_ERASE_REQUEST = 0xEE,
	PUMA_FW_UPDATE_DONE = 0xF8,
	PUMA_FW_UPDATE_READY = 0xFC,
	PUMA_FW_UPDATE_REQUEST = 0xFE,
} AuxWinModeRequestType;

typedef enum {
	PUMA_MODE_CHUNK_PROCESSED = 0x03,
	PUMA_MODE_CHUNK_RECEIVED = 0x07,
	PUMA_MODE_FLASH_INFO_READY = 0xA1,
	PUMA_MODE_UPDATE_ABORT = 0x55,
} AuxWinModeStatusType;

typedef struct {
	guint8 ObjectType;
	guint8 ObjectSubType;
	guint8 ObjectLength[sizeof(guint32)];
} HeaderInfoFormat;

struct _FuKineticDpPumaAuxIsp {
	FuKineticDpAuxIsp parent_instance;
};

typedef struct {
	guint32 isp_processed_size;
	guint32 isp_total_size;
	guint16 read_flash_prog_time;
	guint16 flash_id;
	guint16 flash_size;
} FuKineticDpPumaAuxIspPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuKineticDpPumaAuxIsp,
			   fu_kinetic_dp_puma_aux_isp,
			   FU_TYPE_KINETIC_DP_AUX_ISP)

#define GET_PRIVATE(o) (fu_kinetic_dp_puma_aux_isp_get_instance_private(o))

static gboolean
fu_kinetic_dp_puma_aux_isp_enter_code_loading_mode(FuKineticDpConnection *connection,
						   GError **error)
{
	guint8 cmd;
	guint8 status;
	/*this may need adjust or as input parameter instead*/
	guint32 max_time_ms = PUMA_CODE_LOAD_READY_MAX_WAIT;
	/*this may need adjust or as input parameter instead*/
	guint32 interval_ms = POLL_INTERVAL_MS;

	g_debug("Entering Puma isp driver code loading mode...");
	/* send PUMA_CODE_LOAD_REQUEST cmd */
	cmd = PUMA_CODE_LOAD_REQUEST;
	if (!fu_kinetic_dp_connection_write(connection,
					    PUMA_DPCD_SINK_MODE_REG,
					    &cmd,
					    sizeof(cmd),
					    error)) {
		g_prefix_error(
		    error,
		    "failed to write PUMA_DPCD_SINK_MODE_REG with PUMA_CODE_LOAD_REQUEST: ");
		return FALSE;
	}

	/* Wait for the command to be processed */
	while (max_time_ms != 0) {
		if (!fu_kinetic_dp_connection_read(connection,
						   PUMA_DPCD_SINK_MODE_REG,
						   &status,
						   1,
						   error)) {
			g_prefix_error(error,
				       "failed to read PUMA_DPCD_SINK_MODE_REG for status: ");
			return FALSE;
		}
		if (status == PUMA_CODE_LOAD_READY) {
			return TRUE;
		} else {
			/* wait interval before check the status again */
			g_usleep((gulong)(interval_ms * 1000));
			if (max_time_ms >= interval_ms)
				max_time_ms -= interval_ms;
			else
				max_time_ms = 0;
		}
	}
	/* time out */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "waiting for PUMA_CODE_LOAD_READY timed-out");
	return FALSE;
}

static gboolean
fu_kinetic_dp_puma_aux_isp_send_payload(FuKineticDpPumaAuxIspPrivate *priv,
					FuKineticDpConnection *connection,
<<<<<<< HEAD
					const guint8 *buf,
					const guint32 bufsz,
=======
					const guint8 *payload,
					const guint32 payload_size,
>>>>>>> kinetic-dp: Add a plugin to update Kinetic's DisplayPort converter
					FuProgress *progress,
					guint32 wait_time_ms,
					GError **error,
					gboolean ignore_error)
{
<<<<<<< HEAD
	guint8 *remain_payload = (guint8 *)buf;
	guint32 remain_payload_len = bufsz;
=======
	guint8 *remain_payload = (guint8 *)payload;
	guint32 remain_payload_len = payload_size;
>>>>>>> kinetic-dp: Add a plugin to update Kinetic's DisplayPort converter
	guint32 chunk_len;
	guint32 chunk_remain_len;
	guint32 chunk_offset;
	guint8 status;
	guint32 write_size;
<<<<<<< HEAD
	gboolean show_message = FALSE;
=======
	gboolean show_message;
>>>>>>> kinetic-dp: Add a plugin to update Kinetic's DisplayPort converter

	while (remain_payload_len > 0) {
		chunk_len = (remain_payload_len >= PUMA_DPCD_DATA_SIZE) ? PUMA_DPCD_DATA_SIZE
									: remain_payload_len;

		/* send a maximum 32KB chunk of payload to AUX window */
		chunk_remain_len = chunk_len;
		chunk_offset = 0;
		while (chunk_remain_len > 0) {
			/* maximum length of each AUX write transaction is 16 bytes */
			write_size = (chunk_remain_len >= 16) ? 16 : chunk_remain_len;
			if (!fu_kinetic_dp_connection_write(connection,
							    PUMA_DPCD_DATA_ADDR + chunk_offset,
							    remain_payload + chunk_offset,
							    write_size,
							    error)) {
				g_prefix_error(error,
					       "failed to AUX write at payload 0x%x: ",
<<<<<<< HEAD
					       (guint)((remain_payload + chunk_offset) - buf));
=======
					       (guint)((remain_payload + chunk_offset) - payload));
>>>>>>> kinetic-dp: Add a plugin to update Kinetic's DisplayPort converter
				return FALSE;
			}
			/* adjust and write the next 16 bytes */
			chunk_offset += write_size;
			chunk_remain_len -= write_size;
		}
		/* check if data chunk received */
		while (wait_time_ms > 0) {
			if (!fu_kinetic_dp_connection_read(connection,
							   PUMA_DPCD_CMD_STATUS_REG,
							   &status,
							   1,
							   error)) {
				g_prefix_error(error, "failed to AUX read CMD_STATUS_REG: ");
				return FALSE;
			}
			if (status == PUMA_MODE_CHUNK_RECEIVED) {
				/* chunk received and now wait for processing */
				if (show_message) {
					g_debug(
					    "Data Chunk received and now wait for processing...");
					show_message = FALSE;
				}
			} else if (status == PUMA_MODE_CHUNK_PROCESSED) {
				g_debug("Data Chunk processed");
				show_message = TRUE;
				break;
			}
			wait_time_ms -= 1;
		}
		/* check if timeout */
		if (status != PUMA_MODE_CHUNK_PROCESSED) {
			if (!ignore_error) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INTERNAL,
						    "waiting PUMA_MODE_CHUNK_PROCESSED timed-out");
				return FALSE;
			}
		}
		remain_payload += chunk_len;
		remain_payload_len -= chunk_len;
		g_debug("Remain payload size 0x%x(%u)", remain_payload_len, remain_payload_len);
		priv->isp_processed_size += chunk_len; /*set processed size for progress tracking */

		fu_progress_set_percentage_full(progress,
						priv->isp_processed_size,
						priv->isp_total_size);
	}
	return TRUE;
}

static gboolean
fu_kinetic_dp_puma_aux_isp_wait_drv_ready(FuKineticDpPumaAuxIspPrivate *priv,
					  FuKineticDpConnection *connection,
					  GError **error)
{
	guint8 status;
	/* It takes about 120ms for driver to come up */
	guint32 max_time_ms = PUMA_ISP_DRV_MAX_WAIT;
	/* time between status check */
	guint32 interval_ms = POLL_INTERVAL_MS;
	guint8 flashinfo[6] = {0};

	priv->flash_id = 0;
	priv->flash_size = 0;
	priv->read_flash_prog_time = 10;

	g_debug("Wait for isp driver ready...");
	while (max_time_ms > 0) {
		if (!fu_kinetic_dp_connection_read(connection,
						   PUMA_DPCD_SINK_MODE_REG,
						   &status,
						   1,
						   error)) {
			g_prefix_error(error,
				       "failed to read PUMA_DPCD_SINK_MODE_REG for status: ");
			return FALSE;
		}
		if (status == PUMA_CODE_BOOTUP_DONE) {
			/* get flash info FlashID(2 bytes)+FlashSize(2 bytes)+FlashEraseTime(2 *
			 * bytes) */
			if (!fu_kinetic_dp_connection_read(connection,
							   PUMA_DPCD_DATA_ADDR,
							   flashinfo,
							   sizeof(flashinfo),
							   error)) {
				g_prefix_error(error,
					       "failed to read Flash Info from Isp Driver: ");
				return FALSE;
			}
			/* save flash info need to do memcopy copy here */
			/* TO DO fu_memread_uint16 */
			memcpy(&(priv->flash_id), flashinfo, 2 * sizeof(guint8));
			memcpy(&(priv->flash_size), flashinfo + 2, 2 * sizeof(guint8));
			memcpy(&(priv->read_flash_prog_time), flashinfo + 4, 2 * sizeof(guint8));
			if (priv->read_flash_prog_time <= 0)
				priv->read_flash_prog_time = PUMA_FLASH_ERASE_TIME;
			/* TO DO to_string() */
			g_debug("Puma isp driver running...flashID 0x%x,"
				"flashSize 0x%x(%d),"
				"flashProgramTime 0x%x",
				priv->flash_id,
				priv->flash_size,
				priv->flash_size,
				priv->read_flash_prog_time);
			return TRUE;
		} else {
			/* wait interval before check the status again */
			g_usleep((gulong)(interval_ms * 1000));
			if (max_time_ms >= interval_ms)
				max_time_ms -= interval_ms;
			else
				max_time_ms = 0;
		}
	}
	/* time out */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "waiting for PUMA_CODE_BOOTUP_DONE (Isp Driver Ready) timed-out");
	return FALSE;
}

static gboolean
fu_kinetic_dp_puma_aux_isp_send_isp_drv(FuKineticDpPumaAuxIspPrivate *priv,
					FuKineticDpDevice *device,
					FuKineticDpConnection *connection,
					gboolean is_app_mode,
					const guint8 *isp_drv_data,
					guint32 isp_drv_len,
					FuProgress *progress,
					GError **error)
{
	if (!fu_kinetic_dp_puma_aux_isp_enter_code_loading_mode(connection, error)) {
		g_prefix_error(error, "enter code loading mode failed: ");
		return FALSE;
	}
	fu_kinetic_dp_puma_aux_isp_send_payload(priv,
						connection,
						isp_drv_data,
						isp_drv_len,
						progress,
						PUMA_CHUNK_PROCESS_MAX_WAIT,
						error,
						TRUE);
	if (!fu_kinetic_dp_puma_aux_isp_wait_drv_ready(priv, connection, error)) {
		g_prefix_error(error, "wait for ISP driver ready failed: ");
		return FALSE;
	}
	g_debug("flash ID: 0x%04X", priv->flash_id);
	if (priv->flash_size) {
		if (priv->flash_size < 0x400)
			g_debug("flash size: %d KB, Dual Bank is not supported!", priv->flash_size);
		else
			g_debug("flash size: 0x%04X", priv->flash_size);
	} else {
		if (priv->flash_id) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "SPI flash not supported");
		} else {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "SPI flash not connected");
		}
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_kinetic_dp_puma_aux_isp_enable_fw_update_mode(FuKineticDpPumaAuxIspPrivate *priv,
						 FuKineticDpFirmware *firmware,
						 FuKineticDpConnection *connection,
						 gboolean is_app_mode,
						 GError **error)
{
	guint8 cmd;
	guint8 status;
	guint32 max_time_ms = PUMA_FLASH_READY_MAX_WAIT;
	guint32 interval_ms = POLL_INTERVAL_MS;
	guint8 flashinfo[6] = {0};

	g_debug("Entering app firmware update mode...");

	/* send PUMA_FW_UPDATE_REQUEST cmd */
	cmd = PUMA_FW_UPDATE_REQUEST;
	if (!fu_kinetic_dp_connection_write(connection,
					    PUMA_DPCD_SINK_MODE_REG,
					    &cmd,
					    sizeof(cmd),
					    error)) {
		g_prefix_error(
		    error,
		    "failed to write PUMA_DPCD_SINK_MODE_REG with PUMA_FW_UPDATE_REQUEST: ");
		return FALSE;
	}
	if (is_app_mode) {
		/* Puma takes about 18ms (Winbond EF13) to get ISP driver ready for Flash info. */
		g_usleep((gulong)(18 * 1000));
		/* check for ISP driver ready */
		while (max_time_ms > 0) {
			if (!fu_kinetic_dp_connection_read(connection,
							   PUMA_DPCD_CMD_STATUS_REG,
							   &status,
							   1,
							   error)) {
				g_prefix_error(
				    error,
				    "failed to read PUMA_DPCD_SINK_MODE_REG for status: ");
				return FALSE;
			}
			if (status == PUMA_MODE_FLASH_INFO_READY) {
				/* get flash info FlashID(2 bytes)+FlashSize(2 *
				 * bytes)+FlashEraseTime(2 bytes) */
				if (!fu_kinetic_dp_connection_read(connection,
								   PUMA_DPCD_DATA_ADDR,
								   flashinfo,
								   sizeof(flashinfo),
								   error)) {
					g_prefix_error(error, "failed to read Flash Info: ");
					return FALSE;
				}
				/* save flash info need to do memcopy copy here */
				/* TO DO fu_memread_uint16 */
				memcpy(&(priv->flash_id), flashinfo, 2 * sizeof(guint8));
				memcpy(&(priv->flash_size), flashinfo + 2, 2 * sizeof(guint8));
				memcpy(&(priv->read_flash_prog_time),
				       flashinfo + 4,
				       2 * sizeof(guint8));
				g_debug("Flash ID: 0x%04X and Estimated Flash Erase Wait Time: %u",
					priv->flash_id,
					priv->read_flash_prog_time);
				if (priv->flash_size) {
					if (priv->flash_size < 0x400)
						g_debug("flash Size: %d KB, Dual Bank is not "
							"supported!",
							priv->flash_size);
					else
						g_debug(
						    "Flash size: 0x%04X, Dual Bank is supported!",
						    priv->flash_size);
					break;
				} else {
					if (priv->flash_id)
						g_set_error_literal(error,
								    FWUPD_ERROR,
								    FWUPD_ERROR_INTERNAL,
								    "SPI flash not supported");
					else
						g_set_error_literal(error,
								    FWUPD_ERROR,
								    FWUPD_ERROR_INTERNAL,
								    "SPI flash not connected");
					return FALSE;
				}
			} else {
				/* wait interval before check the status again */
				g_usleep((gulong)(interval_ms * 1000));
				if (max_time_ms >= interval_ms)
					max_time_ms -= interval_ms;
				else
					max_time_ms = 0;
			}
		}
		/* time out */
		if (max_time_ms == 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "timeout waiting for PUMA_MODE_FLASH_INFO_READY.");
			return FALSE;
		}
	}

	/* use flash erase time read or standard flash erase time to wait */
	g_debug("waiting for flash erasing...");
	if (priv->read_flash_prog_time)
		g_usleep((gulong)(priv->read_flash_prog_time * 1000));
	else
		g_usleep((gulong)PUMA_FLASH_ERASE_TIME);
	/* checking for flash erase done */
	max_time_ms = PUMA_FLASH_ERASE_MAX_WAIT;
	while (max_time_ms > 0) {
		if (!fu_kinetic_dp_connection_read(connection,
						   PUMA_DPCD_SINK_MODE_REG,
						   &status,
						   1,
						   error)) {
			g_prefix_error(error,
				       "failed to read PUMA_DPCD_SINK_MODE_REG for status: ");
			return FALSE;
		}
		if (status == PUMA_FW_UPDATE_READY) {
			g_debug("Flash erase done");
			return TRUE;
		} else {
			/* wait interval before check the status again */
			g_usleep(interval_ms * 1000);
			if (max_time_ms >= interval_ms)
				max_time_ms -= interval_ms;
			else
				max_time_ms = 0;
		}
	}
	/* time out */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "waiting for PUMA_FW_UPDATE_READY failed.");
	return FALSE;
}

static gboolean
fu_kinetic_dp_puma_aux_isp_wait_fw_validate(FuKineticDpConnection *connection, GError **error)
{
	guint8 status;
	guint32 max_time_ms = PUMA_FW_VALIDATE_MAX_WAIT;
	guint32 interval_ms = POLL_INTERVAL_MS;

	/* It takes about 90ms to validate firmware image in Puma */
	g_usleep(100 * 1000);
	g_debug("Validate app firmware...");
	while (max_time_ms > 0) {
		if (!fu_kinetic_dp_connection_read(connection,
						   PUMA_DPCD_SINK_MODE_REG,
						   &status,
						   1,
						   error)) {
			g_prefix_error(error,
				       "failed to read PUMA_DPCD_SINK_MODE_REG for status: ");
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "waiting for PUMA_FW_UPDATE_DONE failed.");
			return FALSE;
		}
		if (status == PUMA_FW_UPDATE_DONE) {
			g_debug("Firmware Update Done");
<<<<<<< HEAD
			return TRUE;
=======
			break;
>>>>>>> kinetic-dp: Add a plugin to update Kinetic's DisplayPort converter
		} else {
			/* wait interval before check the status again */
			g_usleep(interval_ms * 1000);
			if (max_time_ms >= interval_ms)
				max_time_ms -= interval_ms;
			else
				max_time_ms = 0;
		}
	}
<<<<<<< HEAD
	/* if get here mean it is time out */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "waiting for PUMA_FW_UPDATE_READY failed.");
	return FALSE;
=======
	/* time out */
	if (max_time_ms == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "waiting for PUMA_FW_UPDATE_READY failed.");
		return FALSE;
	} else {
		return TRUE;
	}
>>>>>>> kinetic-dp: Add a plugin to update Kinetic's DisplayPort converter
}

static gboolean
fu_kinetic_dp_puma_aux_isp_get_device_info(FuKineticDpAuxIsp *self,
					   FuKineticDpDevice *device,
					   KtDpDevInfo *dev_info,
					   GError **error)
{
	g_autoptr(FuKineticDpConnection) connection = NULL;
	guint8 dpcd_buf[16] = {0};

	connection = fu_kinetic_dp_connection_new(fu_udev_device_get_fd(FU_UDEV_DEVICE(device)));

	/* chip ID, FW work state, and branch ID string are known */
	if (!fu_kinetic_dp_connection_read(connection,
					   DPCD_ADDR_BRANCH_FW_SUB,
					   dpcd_buf,
					   sizeof(dpcd_buf),
					   error)) {
		g_prefix_error(error, "reading branch id failed: ");
		return FALSE;
	}
	dev_info->chip_rev = dpcd_buf[1]; /* DPCD 0x509 HW_Ver */
	/* DPCD 0x50A,0x50B,0x508 */
	dev_info->fw_info.std_fw_ver =
	    (guint32)dpcd_buf[2] << 16 | (guint32)dpcd_buf[3] << 8 | dpcd_buf[0];

	/* TODO: implement Gprobe over Aux to read active flash bank */

	dev_info->fw_info.boot_code_ver = 0;
	/* TODO: Add function to read CMDB information */
	dev_info->fw_info.std_cmdb_ver = 0;
	dev_info->fw_info.cmdb_rev = 0;

	return TRUE;
}

/* starting puma isp process */
static gboolean
fu_kinetic_dp_puma_aux_isp_start(FuKineticDpAuxIsp *self,
				 FuKineticDpDevice *device,
				 FuFirmware *firmware,
				 FuProgress *progress,
				 const KtDpDevInfo *dev_info,
				 GError **error)
{
	FuKineticDpPumaAuxIsp *puma_aux_isp = FU_KINETIC_DP_PUMA_AUX_ISP(self);
	FuKineticDpPumaAuxIspPrivate *priv = GET_PRIVATE(puma_aux_isp);
	FuKineticDpFirmware *firmware_self = FU_KINETIC_DP_FIRMWARE(firmware);
	gboolean is_app_mode = (KT_FW_STATE_RUN_APP == dev_info->fw_run_state) ? TRUE : FALSE;
	const guint8 *payload_data;
	gsize payload_len;
	gboolean retcode = FALSE;
	guint8 cmd;
	g_autoptr(FuKineticDpConnection) connection = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuFirmware) img = NULL;
	g_autoptr(GBytes) app = NULL;
	g_autoptr(GBytes) isp_drv = NULL;
	guint8 mca_oui[DPCD_SIZE_IEEE_OUI] = {MCA_OUI_BYTE_0, MCA_OUI_BYTE_1, MCA_OUI_BYTE_2};

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 5, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 5, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 90, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, NULL);

	priv->isp_processed_size = 0;
	priv->isp_total_size = fu_kinetic_dp_firmware_get_arm_app_code_size(firmware_self);
	g_debug("start Puma AUX-ISP [%s]...",
		fu_kinetic_dp_aux_isp_get_chip_id_str(dev_info->chip_id));
	connection = fu_kinetic_dp_connection_new(fu_udev_device_get_fd(FU_UDEV_DEVICE(device)));

	/* write MCA OUI */
	if (!fu_kinetic_dp_aux_dpcd_write_oui(connection, mca_oui, error))
		goto PUMA_AUX_ISP_END;

	fu_progress_step_done(progress);

	/* only load driver if in IROM mode */
	if (!is_app_mode) {
		g_debug("loading isp driver because it is in IROM mode");
		priv->isp_total_size += fu_kinetic_dp_firmware_get_isp_drv_size(firmware_self);
		/* get image of ISP driver */
		img = fu_firmware_get_image_by_idx(firmware, FU_KT_FW_IMG_IDX_ISP_DRV, error);
		if (NULL == img)
			return FALSE;
		isp_drv = fu_firmware_write(img, error);
		if (isp_drv == NULL)
			return FALSE;
		/* send ISP driver and execute it */
		payload_data = g_bytes_get_data(isp_drv, &payload_len);
		if (payload_len > 0)
			if (!fu_kinetic_dp_puma_aux_isp_send_isp_drv(
				priv,
				device,
				connection,
				is_app_mode,
				payload_data,
				payload_len,
				fu_progress_get_child(progress),
				error))
				goto PUMA_AUX_ISP_END;
	}
	fu_progress_step_done(progress);

	/* enable FW update mode */
	if (!fu_kinetic_dp_puma_aux_isp_enable_fw_update_mode(priv,
							      firmware_self,
							      connection,
							      is_app_mode,
							      error))
		goto PUMA_AUX_ISP_END;

	/* get image of App FW */
	img = fu_firmware_get_image_by_idx(firmware, FU_KT_FW_IMG_IDX_APP_FW, error);
	if (NULL == img)
		return FALSE;
	app = fu_firmware_write(img, error);
	if (app == NULL)
		return FALSE;
	fu_progress_step_done(progress);

	/* send App FW image */
	payload_data = g_bytes_get_data(app, &payload_len);
	if (!fu_kinetic_dp_puma_aux_isp_send_payload(priv,
						     connection,
						     payload_data,
						     (const guint32)payload_len,
						     fu_progress_get_child(progress),
						     (guint32)PUMA_CHUNK_PROCESS_MAX_WAIT,
						     error,
						     FALSE)) {
		g_prefix_error(error, "sending App Firmware payload failed: ");
		goto PUMA_AUX_ISP_END;
	}
	fu_progress_step_done(progress);

	/* validate FW images */
	if (!fu_kinetic_dp_puma_aux_isp_wait_fw_validate(connection, error)) {
		g_prefix_error(error, "validating App Firmware failed: ");
		goto PUMA_AUX_ISP_END;
	}

/* Note: not to reuse the GError below */
PUMA_AUX_ISP_END:
	fu_progress_sleep(progress, 3000);
	/* send PUMA_CHIP_RESET_REQUEST command */
	cmd = PUMA_CHIP_RESET_REQUEST;
	if (!fu_kinetic_dp_connection_write(connection,
					    PUMA_DPCD_SINK_MODE_REG,
					    &cmd,
					    sizeof(cmd),
					    error)) {
		g_prefix_error(
		    error,
		    "failed to write PUMA_DPCD_SINK_MODE_REG with PUMA_CHIP_RESET_REQUEST: ");
	} else {
		g_debug("Reset sent.");
		retcode = TRUE;
	}
	return retcode;
}

/* Puma firmware specific parsing */
gboolean
fu_kinetic_dp_puma_aux_isp_parse_app_fw(FuKineticDpFirmware *firmware,
					const guint8 *fw_bin_buf,
					const guint32 fw_bin_size,
					const guint16 fw_bin_flag,
					GError **error)
{
	guint32 code_size;
	guint8 *header;
	guint8 object_count = fw_bin_buf[1];
	guint32 std_fw_ver = 0;
	guint8 puma_std_cmdb_sig[PUMA_FW_CMDB_SIG_SIZE] = {'P', 'M', 'D', 'B'};
	guint8 cmdb_sig[PUMA_FW_CMDB_SIG_SIZE] = {0};
	guint8 i, crc = 0;
	const guint8 *cmdb_buf;
<<<<<<< HEAD
	guint32 checksum;
=======
	guint8 checksum;
>>>>>>> kinetic-dp: Add a plugin to update Kinetic's DisplayPort converter

	if (fw_bin_size < SIZE_512KB) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "F/W payload size (%u) is not valid",
			    fw_bin_size);
		return FALSE;
	}
	if (object_count != PUMA_FW_HEADER_OBJECT_MAX) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "F/W header object count (%u) is not %d",
			    object_count,
			    PUMA_FW_HEADER_OBJECT_MAX);
		return FALSE;
	}

	/* calculate code size */
	/* 2 bytes counts + number of headers * size of headers = 50 bytes */
	code_size = 2 + fw_bin_buf[1] * sizeof(HeaderInfoFormat);
	/* adding all the code segment length in the header*/
	header = (guint8 *)(fw_bin_buf + 2);
	while (object_count > 0) {
		code_size += BYTES_TO_GUINT32(((HeaderInfoFormat *)header)->ObjectLength);
		header += sizeof(HeaderInfoFormat);
		object_count--;
	}
	if (code_size < (SIZE_512KB + PUMA_FW_HEADER_LENGTH_MAX)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Puma F/W BIN file is not correct! File size (%u) not reasonable!",
			    code_size);
		return FALSE;
	}

	fu_kinetic_dp_firmware_set_arm_app_code_size(firmware, code_size);
	/* get STD F/W version */
	std_fw_ver = (guint32)(fw_bin_buf[PUMA_FW_STD_VER_START_ADDR] << 8);	   /*minor*/
	std_fw_ver += (guint32)(fw_bin_buf[PUMA_FW_STD_VER_START_ADDR + 1] << 16); /*major*/
	std_fw_ver += (guint32)(fw_bin_buf[PUMA_FW_STD_VER_START_ADDR + 2]);	   /*rev*/
	fu_kinetic_dp_firmware_set_std_fw_ver(firmware, std_fw_ver);
	/* get cmbd block info */
	memcpy(cmdb_sig, &fw_bin_buf[PUMA_FW_CMDB_START_ADDR], PUMA_FW_CMDB_SIG_SIZE);
	if (memcmp(cmdb_sig, puma_std_cmdb_sig, PUMA_FW_CMDB_SIG_SIZE) == 0) {
		g_debug("cmdb block found in Puma App firmware.");
		memcpy(&checksum, &fw_bin_buf[PUMA_FW_CMDB_REV_ADDR], PUMA_FW_CMDB_REV_SIZE);
		checksum = (checksum << 1);
		cmdb_buf = &fw_bin_buf[PUMA_FW_CMDB_START_ADDR];
		/* calculate crc for cmbd block */
		for (i = 0; i < PUMA_CMDB_SIZE; i++)
			crc += cmdb_buf[i];

		if (crc == checksum) {
			fu_kinetic_dp_firmware_set_cmdb_block_size(firmware, PUMA_CMDB_SIZE);
			fu_kinetic_dp_firmware_set_cmdb_ver(
			    firmware,
			    (fw_bin_buf[PUMA_FW_CMDB_STD_VER_ADDR] << 8 |
			     fw_bin_buf[PUMA_FW_CMDB_STD_VER_ADDR + 1]));
			fu_kinetic_dp_firmware_set_cmdb_rev(
			    firmware,
			    (fw_bin_buf[PUMA_FW_CMDB_REV_ADDR] << 16 |
			     fw_bin_buf[PUMA_FW_CMDB_REV_ADDR + 1] << 8 |
			     fw_bin_buf[PUMA_FW_CMDB_REV_ADDR + 2]));
		}
	} else {
		g_debug("cmdb block not found in Puma App firmware.");
	}
	return TRUE;
}

/**/
static void
fu_kinetic_dp_puma_aux_isp_init(FuKineticDpPumaAuxIsp *self)
{
	FuKineticDpPumaAuxIspPrivate *priv = GET_PRIVATE(self);

	priv->isp_processed_size = 0;
	priv->isp_total_size = 0;
	priv->read_flash_prog_time = 10;
	priv->flash_id = 0;
	priv->flash_size = 0;
}

/**/
static void
fu_kinetic_dp_puma_aux_isp_class_init(FuKineticDpPumaAuxIspClass *klass)
{
	FuKineticDpAuxIspClass *klass_aux_isp = FU_KINETIC_DP_AUX_ISP_CLASS(klass);

	klass_aux_isp->get_device_info = fu_kinetic_dp_puma_aux_isp_get_device_info;
	klass_aux_isp->start = fu_kinetic_dp_puma_aux_isp_start;
}

/**/
FuKineticDpPumaAuxIsp *
fu_kinetic_dp_puma_aux_isp_new(void)
{
	FuKineticDpPumaAuxIsp *self = g_object_new(FU_TYPE_KINETIC_DP_PUMA_AUX_ISP, NULL);
	return FU_KINETIC_DP_PUMA_AUX_ISP(self);
}
