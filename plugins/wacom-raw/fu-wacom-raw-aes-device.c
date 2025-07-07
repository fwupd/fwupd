/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-wacom-raw-aes-device.h"
#include "fu-wacom-raw-common.h"

struct _FuWacomRawAesDevice {
	FuWacomRawDevice parent_instance;
};

G_DEFINE_TYPE(FuWacomRawAesDevice, fu_wacom_raw_aes_device, FU_TYPE_WACOM_RAW_DEVICE)

static gboolean
fu_wacom_raw_aes_device_add_recovery_hwid(FuWacomRawAesDevice *self, GError **error)
{
	guint16 pid;
	g_autoptr(FuStructWacomRawRequest) st_req = fu_struct_wacom_raw_request_new();
	g_autoptr(FuStructWacomRawBlVerifyResponse) st_rsp = NULL;

	fu_struct_wacom_raw_request_set_report_id(st_req, FU_WACOM_RAW_BL_REPORT_ID_SET);
	fu_struct_wacom_raw_request_set_cmd(st_req, FU_WACOM_RAW_BL_CMD_VERIFY_FLASH);
	fu_struct_wacom_raw_request_set_echo(st_req, 0x01);
	fu_struct_wacom_raw_request_set_addr(st_req, FU_WACOM_RAW_BL_START_ADDR);
	fu_struct_wacom_raw_request_set_size8(st_req, FU_WACOM_RAW_BL_BYTES_CHECK / 8);
	if (!fu_wacom_raw_device_set_feature(FU_WACOM_RAW_DEVICE(self),
					     st_req->data,
					     st_req->len,
					     error)) {
		g_prefix_error(error, "failed to send: ");
		return FALSE;
	}
	if (!fu_wacom_raw_device_get_feature(FU_WACOM_RAW_DEVICE(self),
					     st_req->data,
					     st_req->len,
					     error)) {
		g_prefix_error(error, "failed to receive: ");
		return FALSE;
	}
	st_rsp =
	    fu_struct_wacom_raw_bl_verify_response_parse(st_req->data, st_req->len, 0x0, error);
	if (st_rsp == NULL)
		return FALSE;
	if (fu_struct_wacom_raw_bl_verify_response_get_size8(st_rsp) !=
	    fu_struct_wacom_raw_request_get_size8(st_req)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "firmware does not support this feature");
		return FALSE;
	}
	pid = fu_struct_wacom_raw_bl_verify_response_get_pid(st_rsp);
	if (pid == 0xFFFF || pid == 0x0000) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "invalid recovery product ID %04x",
			    pid);
		return FALSE;
	}

	/* add recovery IDs */
	fu_device_add_instance_u16(FU_DEVICE(self), "VEN", 0x2D1F);
	fu_device_add_instance_u16(FU_DEVICE(self), "DEV", pid);
	if (!fu_device_build_instance_id(FU_DEVICE(self), error, "HIDRAW", "VID", "PID", NULL))
		return FALSE;
	fu_device_add_instance_u16(FU_DEVICE(self), "VEN", 0x056A);
	return fu_device_build_instance_id(FU_DEVICE(self), error, "HIDRAW", "VID", "PID", NULL);
}

static gboolean
fu_wacom_raw_aes_device_query_operation_mode(FuWacomRawAesDevice *self,
					     FuWacomRawOperationMode *mode,
					     GError **error)
{
	g_autoptr(GByteArray) st_req = fu_struct_wacom_raw_fw_query_mode_request_new();
	g_autoptr(GByteArray) st_rsp = NULL;

	if (!fu_wacom_raw_device_get_feature(FU_WACOM_RAW_DEVICE(self),
					     st_req->data,
					     st_req->len,
					     error))
		return FALSE;
	st_rsp =
	    fu_struct_wacom_raw_fw_query_mode_response_parse(st_req->data, st_req->len, 0x0, error);
	if (st_rsp == NULL)
		return FALSE;
	if (mode != NULL)
		*mode = fu_struct_wacom_raw_fw_query_mode_response_get_mode(st_rsp);
	return TRUE;
}

static gboolean
fu_wacom_raw_aes_device_setup(FuDevice *device, GError **error)
{
	FuWacomRawAesDevice *self = FU_WACOM_RAW_AES_DEVICE(device);
	FuWacomRawOperationMode mode = 0;
	g_autoptr(GError) error_local = NULL;

	/* find out if in bootloader mode already */
	if (!fu_wacom_raw_aes_device_query_operation_mode(self, &mode, error))
		return FALSE;

	if (mode == FU_WACOM_RAW_OPERATION_MODE_BOOTLOADER) {
		fu_device_set_version(device, "0.0");
		/* get the recovery PID if supported */
		if (!fu_wacom_raw_aes_device_add_recovery_hwid(self, &error_local))
			g_debug("failed to get HwID: %s", error_local->message);
	} else if (mode == FU_WACOM_RAW_OPERATION_MODE_RUNTIME) {
		g_autofree gchar *version = NULL;
		g_autoptr(FuStructWacomRawFwStatusRequest) st_req =
		    fu_struct_wacom_raw_fw_status_request_new();
		g_autoptr(FuStructWacomRawFwStatusResponse) st_rsp = NULL;

		/* get firmware version */
		if (!fu_wacom_raw_device_get_feature(FU_WACOM_RAW_DEVICE(self),
						     st_req->data,
						     st_req->len,
						     error))
			return FALSE;
		st_rsp = fu_struct_wacom_raw_fw_status_response_parse(st_req->data,
								      st_req->len,
								      0x0,
								      error);
		if (st_rsp == NULL)
			return FALSE;
		version = g_strdup_printf(
		    "%04x.%02x",
		    fu_struct_wacom_raw_fw_status_response_get_version_major(st_rsp),
		    fu_struct_wacom_raw_fw_status_response_get_version_minor(st_rsp));
		fu_device_set_version(device, version);
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to query operation mode, got 0x%x",
			    mode);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_wacom_raw_aes_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuWacomRawDevice *self = FU_WACOM_RAW_DEVICE(device);
	g_autoptr(FuStructWacomRawRequest) st_req = fu_struct_wacom_raw_request_new();

	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in runtime mode, skipping");
		return TRUE;
	}

	fu_struct_wacom_raw_request_set_report_id(st_req, FU_WACOM_RAW_BL_REPORT_ID_TYPE);
	fu_struct_wacom_raw_request_set_cmd(st_req, FU_WACOM_RAW_BL_TYPE_FINALIZER);
	if (!fu_wacom_raw_device_set_feature(self, st_req->data, st_req->len, error)) {
		g_prefix_error(error, "failed to finalize the device: ");
		return FALSE;
	}

	/* does the device have to replug to bootloader mode */
	if (fu_device_has_private_flag(device, FU_WACOM_RAW_DEVICE_FLAG_REQUIRES_WAIT_FOR_REPLUG)) {
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	} else {
		/* wait for device back to runtime mode */
		fu_device_sleep(device, 500); /* ms */
		fu_device_remove_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_wacom_raw_aes_device_erase_all(FuWacomRawAesDevice *self, FuProgress *progress, GError **error)
{
	g_autoptr(FuStructWacomRawRequest) st_req = fu_struct_wacom_raw_request_new();

	fu_struct_wacom_raw_request_set_report_id(st_req, FU_WACOM_RAW_BL_REPORT_ID_SET);
	fu_struct_wacom_raw_request_set_cmd(st_req, FU_WACOM_RAW_BL_CMD_ALL_ERASE);
	fu_struct_wacom_raw_request_set_echo(
	    st_req,
	    fu_wacom_raw_device_get_echo_next(FU_WACOM_RAW_DEVICE(self)));
	if (!fu_wacom_raw_device_cmd(FU_WACOM_RAW_DEVICE(self),
				     st_req,
				     NULL,
				     2000, /* this takes a long time */
				     FU_WACOM_RAW_DEVICE_CMD_FLAG_POLL_ON_WAITING,
				     error)) {
		g_prefix_error(error, "failed to send eraseall command: ");
		return FALSE;
	}
	fu_device_sleep_full(FU_DEVICE(self), 2000, progress);
	return TRUE;
}

static gboolean
fu_wacom_raw_aes_device_write_block(FuWacomRawAesDevice *self,
				    guint32 idx,
				    guint32 address,
				    const guint8 *data,
				    gsize datasz,
				    GError **error)
{
	gsize blocksz = fu_wacom_raw_device_get_block_sz(FU_WACOM_RAW_DEVICE(self));
	g_autoptr(FuStructWacomRawRequest) st_req = fu_struct_wacom_raw_request_new();

	/* check size */
	if (datasz != blocksz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "block size 0x%x != 0x%x untested",
			    (guint)datasz,
			    (guint)blocksz);
		return FALSE;
	}

	/* write */
	fu_struct_wacom_raw_request_set_report_id(st_req, FU_WACOM_RAW_BL_REPORT_ID_SET);
	fu_struct_wacom_raw_request_set_cmd(st_req, FU_WACOM_RAW_BL_CMD_WRITE_FLASH);
	fu_struct_wacom_raw_request_set_echo(st_req, (guint8)idx + 1);
	fu_struct_wacom_raw_request_set_addr(st_req, address);
	fu_struct_wacom_raw_request_set_size8(st_req, datasz / 8);
	if (!fu_struct_wacom_raw_request_set_data(st_req, data, datasz, error))
		return FALSE;
	if (!fu_wacom_raw_device_cmd(FU_WACOM_RAW_DEVICE(self),
				     st_req,
				     NULL,
				     1, /* ms */
				     FU_WACOM_RAW_DEVICE_CMD_FLAG_POLL_ON_WAITING,
				     error)) {
		g_prefix_error(error, "failed to write block %u: ", idx);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_wacom_raw_aes_device_write_firmware(FuDevice *device,
				       FuChunkArray *chunks,
				       FuProgress *progress,
				       GError **error)
{
	FuWacomRawAesDevice *self = FU_WACOM_RAW_AES_DEVICE(device);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 28, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 72, NULL);

	/* erase */
	if (!fu_wacom_raw_aes_device_erase_all(self, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write */
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_wacom_raw_aes_device_write_block(self,
							 fu_chunk_get_idx(chk),
							 fu_chunk_get_address(chk),
							 fu_chunk_get_data(chk),
							 fu_chunk_get_data_sz(chk),
							 error))
			return FALSE;
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						(gsize)i,
						(gsize)fu_chunk_array_length(chunks));
	}
	fu_progress_step_done(progress);
	return TRUE;
}

static void
fu_wacom_raw_aes_device_init(FuWacomRawAesDevice *self)
{
	fu_device_set_name(FU_DEVICE(self), "Wacom AES Device");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
}

static void
fu_wacom_raw_aes_device_class_init(FuWacomRawAesDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	FuWacomRawDeviceClass *wac_device_class = FU_WACOM_RAW_DEVICE_CLASS(klass);
	device_class->setup = fu_wacom_raw_aes_device_setup;
	device_class->attach = fu_wacom_raw_aes_device_attach;
	wac_device_class->write_firmware = fu_wacom_raw_aes_device_write_firmware;
}
