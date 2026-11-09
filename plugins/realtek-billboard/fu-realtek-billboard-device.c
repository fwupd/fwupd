/*
 * Copyright 2026 Realtek Corporation
 * Copyright 2026 Shadow Zhang <shadow_zhang@realsil.com.cn>
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "config.h"

#include "fu-realtek-billboard-device.h"
#include "fu-realtek-billboard-struct.h"

/* control transfer timeout */
#define FU_REALTEK_BILLBOARD_TRANSACTION_TIMEOUT 5000 /* ms */

struct _FuRealtekBillboardDevice {
	FuUsbDevice parent_instance;
};

G_DEFINE_TYPE(FuRealtekBillboardDevice, fu_realtek_billboard_device, FU_TYPE_USB_DEVICE)

/* sends data to the device using a vendor control transfer */
static gboolean
fu_realtek_billboard_device_send(FuRealtekBillboardDevice *self,
				 guint8 request,
				 guint16 value,
				 guint16 index,
				 const guint8 *buf,
				 gsize bufsz,
				 GError **error)
{
	gsize actual_len = 0;
	g_autofree guint8 *buf_tmp = NULL;

	if (bufsz > 0) {
		buf_tmp = fu_memdup_safe(buf, bufsz, error);
		if (buf_tmp == NULL)
			return FALSE;
	}
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    request,
					    value,
					    index,
					    buf_tmp,
					    bufsz,
					    &actual_len,
					    FU_REALTEK_BILLBOARD_TRANSACTION_TIMEOUT,
					    error)) {
		g_prefix_error_literal(error, "send error: ");
		return FALSE;
	}
	if (actual_len != bufsz) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "send length mismatch");
		return FALSE;
	}

	/* success */
	return TRUE;
}

/* receives data from the device using a vendor control transfer */
static gboolean
fu_realtek_billboard_device_recv(FuRealtekBillboardDevice *self,
				 guint8 request,
				 guint16 value,
				 guint16 index,
				 guint8 *buf,
				 gsize bufsz,
				 GError **error)
{
	gsize actual_len = 0;

	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_DEVICE_TO_HOST,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    request,
					    value,
					    index,
					    buf,
					    bufsz,
					    &actual_len,
					    FU_REALTEK_BILLBOARD_TRANSACTION_TIMEOUT,
					    error)) {
		g_prefix_error_literal(error, "recv error: ");
		return FALSE;
	}
	if (actual_len != bufsz) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "recv length mismatch");
		return FALSE;
	}

	/* success */
	return TRUE;
}

/* checks one of the echo bytes at the start of a reply */
static gboolean
fu_realtek_billboard_device_check_echo(guint expected,
				       guint actual,
				       const gchar *what,
				       GError **error)
{
	if (actual != expected) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "unexpected %s echo 0x%02x, expected 0x%02x",
			    what,
			    actual,
			    expected);
		return FALSE;
	}

	/* success */
	return TRUE;
}

/* gets the firmware version using the GetFWVersion command */
static gboolean
fu_realtek_billboard_device_ensure_version(FuRealtekBillboardDevice *self, GError **error)
{
	guint8 buf[FU_STRUCT_REALTEK_BILLBOARD_FW_VERSION_RSP_SIZE] = {0x0};
	g_autofree gchar *version = NULL;
	g_autoptr(FuStructRealtekBillboardFwVersionRsp) st = NULL;

	if (!fu_realtek_billboard_device_recv(self,
					      FU_REALTEK_BILLBOARD_CMD_GET_FW_VERSION,
					      0x0,
					      0x0,
					      buf,
					      sizeof(buf),
					      error)) {
		g_prefix_error_literal(error, "failed to get FW version: ");
		return FALSE;
	}
	st = fu_struct_realtek_billboard_fw_version_rsp_parse(buf, sizeof(buf), 0x0, error);
	if (st == NULL)
		return FALSE;
	if (!fu_realtek_billboard_device_check_echo(
		FU_REALTEK_BILLBOARD_CMD_GET_FW_VERSION,
		fu_struct_realtek_billboard_fw_version_rsp_get_cmd(st),
		"request",
		error))
		return FALSE;

	/* success */
	version = g_strdup_printf("%u.%u.0.0",
				  fu_struct_realtek_billboard_fw_version_rsp_get_major(st),
				  fu_struct_realtek_billboard_fw_version_rsp_get_minor(st));
	fu_device_set_version(FU_DEVICE(self), version);
	return TRUE;
}

/* gets the offset and length of the Project ID within the firmware; offset is relative to
 * the start of the image, not an absolute flash address */
static gboolean
fu_realtek_billboard_device_get_project_id_address_info(FuRealtekBillboardDevice *self,
							guint32 *address,
							guint8 *length,
							GError **error)
{
	guint8 buf[FU_STRUCT_REALTEK_BILLBOARD_PROJECT_ID_ADDRESS_INFO_RSP_SIZE] = {0x0};
	g_autoptr(FuStructRealtekBillboardProjectIdAddressInfoRsp) st = NULL;

	if (!fu_realtek_billboard_device_recv(self,
					      FU_REALTEK_BILLBOARD_CMD_PROJECT_ID,
					      FU_REALTEK_BILLBOARD_PROJECT_ID_OP_GET_ADDRESS_INFO,
					      0x0,
					      buf,
					      sizeof(buf),
					      error)) {
		g_prefix_error_literal(error, "failed to get project ID address info: ");
		return FALSE;
	}
	st = fu_struct_realtek_billboard_project_id_address_info_rsp_parse(buf,
									   sizeof(buf),
									   0x0,
									   error);
	if (st == NULL)
		return FALSE;
	if (!fu_realtek_billboard_device_check_echo(
		FU_REALTEK_BILLBOARD_CMD_PROJECT_ID,
		fu_struct_realtek_billboard_project_id_address_info_rsp_get_cmd(st),
		"request",
		error))
		return FALSE;
	if (!fu_realtek_billboard_device_check_echo(
		FU_REALTEK_BILLBOARD_PROJECT_ID_OP_GET_ADDRESS_INFO,
		fu_struct_realtek_billboard_project_id_address_info_rsp_get_opcode(st),
		"opcode",
		error))
		return FALSE;
	*address = fu_struct_realtek_billboard_project_id_address_info_rsp_get_address(st);
	*length = fu_struct_realtek_billboard_project_id_address_info_rsp_get_length(st);
	if (*length == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "device returned a zero project ID length");
		return FALSE;
	}

	/* success */
	return TRUE;
}

/* gets the result of the Project ID verification; failing if the device rejected it */
static gboolean
fu_realtek_billboard_device_get_verify_project_id_result(FuRealtekBillboardDevice *self,
							 GError **error)
{
	guint8 buf[FU_STRUCT_REALTEK_BILLBOARD_RESULT_RSP_SIZE] = {0x0};
	g_autoptr(FuStructRealtekBillboardResultRsp) st = NULL;

	if (!fu_realtek_billboard_device_recv(self,
					      FU_REALTEK_BILLBOARD_CMD_PROJECT_ID,
					      FU_REALTEK_BILLBOARD_PROJECT_ID_OP_GET_VERIFY_RESULT,
					      0x0,
					      buf,
					      sizeof(buf),
					      error)) {
		g_prefix_error_literal(error, "failed to get verify project ID result: ");
		return FALSE;
	}
	st = fu_struct_realtek_billboard_result_rsp_parse(buf, sizeof(buf), 0x0, error);
	if (st == NULL) {
		g_prefix_error_literal(error,
				       "failed to parse results for project ID verification: ");
		return FALSE;
	}
	if (!fu_realtek_billboard_device_check_echo(
		FU_REALTEK_BILLBOARD_CMD_PROJECT_ID,
		fu_struct_realtek_billboard_result_rsp_get_cmd(st),
		"request",
		error))
		return FALSE;
	if (!fu_realtek_billboard_device_check_echo(
		FU_REALTEK_BILLBOARD_PROJECT_ID_OP_GET_VERIFY_RESULT,
		fu_struct_realtek_billboard_result_rsp_get_opcode(st),
		"opcode",
		error))
		return FALSE;

	/* success */
	return TRUE;
}

/* starts a firmware update, failing if the device rejected the start */
static gboolean
fu_realtek_billboard_device_fw_update_start(FuRealtekBillboardDevice *self, GError **error)
{
	guint8 buf[FU_STRUCT_REALTEK_BILLBOARD_RESULT_RSP_SIZE] = {0x0};
	g_autoptr(FuStructRealtekBillboardResultRsp) st = NULL;

	if (!fu_realtek_billboard_device_recv(self,
					      FU_REALTEK_BILLBOARD_CMD_FW_UPDATE,
					      FU_REALTEK_BILLBOARD_FW_UPDATE_OP_START,
					      0x0,
					      buf,
					      sizeof(buf),
					      error)) {
		g_prefix_error_literal(error, "failed to start FW update: ");
		return FALSE;
	}
	st = fu_struct_realtek_billboard_result_rsp_parse(buf, sizeof(buf), 0x0, error);
	if (st == NULL) {
		g_prefix_error_literal(error, "failed to parse results for FW update start: ");
		return FALSE;
	}
	if (!fu_realtek_billboard_device_check_echo(
		FU_REALTEK_BILLBOARD_CMD_FW_UPDATE,
		fu_struct_realtek_billboard_result_rsp_get_cmd(st),
		"request",
		error))
		return FALSE;
	if (!fu_realtek_billboard_device_check_echo(
		FU_REALTEK_BILLBOARD_FW_UPDATE_OP_START,
		fu_struct_realtek_billboard_result_rsp_get_opcode(st),
		"opcode",
		error))
		return FALSE;

	/* success */
	return TRUE;
}

/* writes firmware data using the FW Update Write data command */
static gboolean
fu_realtek_billboard_device_fw_update_write_data(FuRealtekBillboardDevice *self,
						 const guint8 *buf,
						 gsize bufsz,
						 GError **error)
{
	if (!fu_realtek_billboard_device_send(self,
					      FU_REALTEK_BILLBOARD_CMD_FW_UPDATE,
					      FU_REALTEK_BILLBOARD_FW_UPDATE_OP_WRITE_DATA,
					      0x0,
					      buf,
					      bufsz,
					      error)) {
		g_prefix_error_literal(error, "failed to write FW update data: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

/* finishes a firmware update, failing if the device rejected the update */
static gboolean
fu_realtek_billboard_device_fw_update_finish(FuRealtekBillboardDevice *self, GError **error)
{
	guint8 buf[FU_STRUCT_REALTEK_BILLBOARD_RESULT_RSP_SIZE] = {0x0};
	g_autoptr(FuStructRealtekBillboardResultRsp) st = NULL;

	if (!fu_realtek_billboard_device_recv(self,
					      FU_REALTEK_BILLBOARD_CMD_FW_UPDATE,
					      FU_REALTEK_BILLBOARD_FW_UPDATE_OP_FINISH,
					      0x0,
					      buf,
					      sizeof(buf),
					      error)) {
		g_prefix_error_literal(error, "failed to finish FW update: ");
		return FALSE;
	}
	st = fu_struct_realtek_billboard_result_rsp_parse(buf, sizeof(buf), 0x0, error);
	if (st == NULL) {
		g_prefix_error_literal(error, "failed to parse results for FW update finish: ");
		return FALSE;
	}
	if (!fu_realtek_billboard_device_check_echo(
		FU_REALTEK_BILLBOARD_CMD_FW_UPDATE,
		fu_struct_realtek_billboard_result_rsp_get_cmd(st),
		"request",
		error))
		return FALSE;
	if (!fu_realtek_billboard_device_check_echo(
		FU_REALTEK_BILLBOARD_FW_UPDATE_OP_FINISH,
		fu_struct_realtek_billboard_result_rsp_get_opcode(st),
		"opcode",
		error))
		return FALSE;

	/* success */
	return TRUE;
}

/* resets the device -- a STALL is the only way the device reports a failure */
static gboolean
fu_realtek_billboard_device_reset(FuRealtekBillboardDevice *self, GError **error)
{
	/* no data stage, so the control transfer only has a status stage */
	if (!fu_realtek_billboard_device_send(self,
					      FU_REALTEK_BILLBOARD_CMD_RESET,
					      0x0,
					      0x0,
					      NULL,
					      0x0,
					      error)) {
		g_prefix_error_literal(error, "failed to reset: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_realtek_billboard_device_check_firmware(FuDevice *device,
					   FuFirmware *firmware,
					   FuFirmwareParseFlags flags,
					   GError **error)
{
	FuRealtekBillboardDevice *self = FU_REALTEK_BILLBOARD_DEVICE(device);
	guint32 address = 0x0;
	guint8 length = 0x0;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuInputStream) stream = NULL;

	/* the address and length differ for each project, so ask the device */
	if (!fu_realtek_billboard_device_get_project_id_address_info(self,
								     &address,
								     &length,
								     error))
		return FALSE;

	/* the Project ID is embedded in the firmware image at the reported address,
	 * which the device reports as an offset into the image rather than as a
	 * flash address, so it can be used to seek directly */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;
	buf = fu_input_stream_read_byte_array(stream, address, length, NULL, error);
	if (buf == NULL) {
		g_prefix_error(error, "failed to read project ID from 0x%x: ", address);
		return FALSE;
	}
	if (!fu_realtek_billboard_device_send(self,
					      FU_REALTEK_BILLBOARD_CMD_PROJECT_ID,
					      FU_REALTEK_BILLBOARD_PROJECT_ID_OP_SET_VERIFY,
					      0x0,
					      buf->data,
					      buf->len,
					      error)) {
		g_prefix_error_literal(error, "failed to set verify project ID: ");
		return FALSE;
	}
	if (!fu_realtek_billboard_device_get_verify_project_id_result(self, error)) {
		g_prefix_error_literal(error, "project ID verification rejected: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

/* streams the firmware image to the device in chunks */
static gboolean
fu_realtek_billboard_device_write_firmware_data(FuRealtekBillboardDevice *self,
						FuInputStream *stream,
						FuProgress *progress,
						GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;

	/* the Start command has already been sent, so all the data is a single write */
	chunks = fu_chunk_array_new_from_stream(stream,
						FU_CHUNK_ADDR_OFFSET_NONE,
						FU_CHUNK_PAGESZ_NONE,
						256,
						error);
	if (chunks == NULL)
		return FALSE;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_realtek_billboard_device_fw_update_write_data(self,
								      fu_chunk_get_data(chk),
								      fu_chunk_get_data_sz(chk),
								      error)) {
			g_prefix_error(error,
				       "failed to write 0x%x bytes at 0x%x: ",
				       (guint)fu_chunk_get_data_sz(chk),
				       (guint)fu_chunk_get_address(chk));
			return FALSE;
		}
		/* the device has a single packet buffer that is only released once
		 * its main loop has copied the data out, so a packet sent straight
		 * after the previous one is rejected with a STALL */
		fu_device_sleep(FU_DEVICE(self), 10);
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_realtek_billboard_device_write_firmware(FuDevice *device,
					   FuFirmware *firmware,
					   FuProgress *progress,
					   FwupdInstallFlags flags,
					   GError **error)
{
	FuRealtekBillboardDevice *self = FU_REALTEK_BILLBOARD_DEVICE(device);
	g_autoptr(FuInputStream) stream = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, "erase");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 90, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, "finish");

	/* erase the flash and get ready for the data */
	if (!fu_realtek_billboard_device_fw_update_start(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* the device only answers the Start command from the USB interrupt, the
	 * erase and the address setup happen later in the main loop, so give it
	 * time to finish before the first packet arrives */
	fu_device_sleep(FU_DEVICE(self), 500); /* ms */

	/* stream the image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;
	if (!fu_realtek_billboard_device_write_firmware_data(self,
							     stream,
							     fu_progress_get_child(progress),
							     error)) {
		g_prefix_error_literal(error, "failed to write FW update data: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* commit the image and let the device check it */
	if (!fu_realtek_billboard_device_fw_update_finish(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_realtek_billboard_device_reload_version_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuRealtekBillboardDevice *self = FU_REALTEK_BILLBOARD_DEVICE(device);
	return fu_realtek_billboard_device_ensure_version(self, error);
}

static gboolean
fu_realtek_billboard_device_reload(FuDevice *device, GError **error)
{
	/* the device may still be starting up when the replug has been seen */
	return fu_device_retry_full(device,
				    fu_realtek_billboard_device_reload_version_cb,
				    5,	  /* count */
				    2000, /* ms */
				    NULL,
				    error);
}

static gboolean
fu_realtek_billboard_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuRealtekBillboardDevice *self = FU_REALTEK_BILLBOARD_DEVICE(device);
	g_autoptr(GError) error_local = NULL;

	/* run the image that was just committed, the device ignores a failed
	 * reset as it may already have reset itself when the update finished */
	if (!fu_realtek_billboard_device_reset(self, &error_local))
		g_debug("ignoring reset failure: %s", error_local->message);

	/* success */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static void
fu_realtek_billboard_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static gboolean
fu_realtek_billboard_device_setup(FuDevice *device, GError **error)
{
	FuRealtekBillboardDevice *self = FU_REALTEK_BILLBOARD_DEVICE(device);

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_realtek_billboard_device_parent_class)->setup(device, error))
		return FALSE;

	/* set firmware version */
	if (!fu_realtek_billboard_device_ensure_version(self, error)) {
		g_prefix_error_literal(error, "failed to read FW version: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_realtek_billboard_device_init(FuRealtekBillboardDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.realtek.billboard");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_set_summary(FU_DEVICE(self), "USB Billboard controller");
	fu_device_set_install_duration(FU_DEVICE(self), 10);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_DETACH_PREPARE_FIRMWARE);
	/* the USB Billboard interface is always #0 on this device */
	fu_usb_device_add_interface(FU_USB_DEVICE(self), 0x0);
}

static void
fu_realtek_billboard_device_class_init(FuRealtekBillboardDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_realtek_billboard_device_setup;
	device_class->check_firmware = fu_realtek_billboard_device_check_firmware;
	device_class->write_firmware = fu_realtek_billboard_device_write_firmware;
	device_class->attach = fu_realtek_billboard_device_attach;
	device_class->reload = fu_realtek_billboard_device_reload;
	device_class->set_progress = fu_realtek_billboard_device_set_progress;
}
