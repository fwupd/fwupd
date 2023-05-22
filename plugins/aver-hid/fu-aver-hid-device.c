/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-aver-hid-device.h"
#include "fu-aver-hid-firmware.h"
#include "fu-aver-hid-struct.h"

struct _FuAverHidDevice {
	FuHidDevice parent_instance;
};

G_DEFINE_TYPE(FuAverHidDevice, fu_aver_hid_device, FU_TYPE_HID_DEVICE)

#define FU_AVER_HID_DEVICE_TIMEOUT 5000 /* ms */
#define FU_AVER_HID_DEVICE_GET_STATUS_POLL_INTERVAL 1000 /* ms */
#define FU_AVER_HID_DEVICE_POLL_INTERVAL 5000 /* ms */
#define FU_AVER_HID_DEVICE_ISP_TIMEOUT		    300	 /* 300 secs */
#define FU_AVER_HID_ISP_FILE_NAME_LENGTH	    15
#define FU_AVER_HID_ISP_AUDIO_FILE_NAME_LENGTH	    (FU_AVER_HID_ISP_FILE_NAME_LENGTH + 6)

static gboolean
fu_aver_hid_device_transfer(FuAverHidDevice *self, GByteArray *req, GByteArray *res, GError **error)
{
	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      req->data[0],
				      req->data,
				      req->len,
				      FU_AVER_HID_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		g_prefix_error(error, "failed to send packet: ");
		return FALSE;
	}
	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      res->data[0],
				      res->data,
				      res->len,
				      FU_AVER_HID_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		g_prefix_error(error, "failed to receive packet: ");
		return FALSE;
	}
	g_debug("custom-isp-cmd: %s [0x%x]",
		fu_aver_hid_custom_isp_cmd_to_string(
		    fu_struct_aver_hid_res_isp_get_custom_isp_cmd(res)),
		fu_struct_aver_hid_res_isp_get_custom_isp_cmd(res));
	return TRUE;
}

static gboolean
fu_aver_hid_device_poll(FuDevice *device, GError **error)
{
	FuAverHidDevice *self = FU_AVER_HID_DEVICE(device);
	g_autoptr(GByteArray) req = fu_struct_aver_hid_req_isp_new();
	g_autoptr(GByteArray) res = fu_struct_aver_hid_res_isp_status_new();
	g_autoptr(FuDeviceLocker) locker = NULL;

	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;
	fu_struct_aver_hid_req_isp_set_custom_isp_cmd(req, FU_AVER_HID_CUSTOM_ISP_CMD_STATUS);
	if (!fu_aver_hid_device_transfer(self, req, res, error))
		return FALSE;
	if (!fu_struct_aver_hid_res_isp_status_validate(res->data, res->len, 0x0, error))
		return FALSE;
	if (fu_struct_aver_hid_res_isp_status_get_status(res) == FU_AVER_HID_STATUS_BUSY) {
		fu_device_add_problem(device, FWUPD_DEVICE_PROBLEM_IN_USE);
	} else {
		fu_device_remove_problem(device, FWUPD_DEVICE_PROBLEM_IN_USE);
	}

	return TRUE;
}

static gboolean
fu_aver_hid_device_get_version(FuDevice *device, GError **error)
{
	FuAverHidDevice *self = FU_AVER_HID_DEVICE(device);
	g_autoptr(GByteArray) req = fu_struct_aver_hid_req_device_version_new();
	g_autoptr(GByteArray) res = fu_struct_aver_hid_res_device_version_new();
	g_autofree gchar *ver = NULL;

	if (!fu_aver_hid_device_transfer(self, req, res, error))
		return FALSE;
	if (!fu_struct_aver_hid_res_device_version_validate(res->data, res->len, 0x0, error))
		return FALSE;
	ver = fu_strsafe((const gchar *)fu_struct_aver_hid_res_device_version_get_ver(res, NULL),
			 FU_STRUCT_AVER_HID_RES_DEVICE_VERSION_SIZE_VER);
	fu_device_set_version(device, ver);
	return TRUE;
}

static gboolean
fu_aver_hid_device_setup(FuDevice *device, GError **error)
{
	FuAverHidDevice *self = FU_AVER_HID_DEVICE(device);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));

	/* HidDevice->setup */
	if (!FU_DEVICE_CLASS(fu_aver_hid_device_parent_class)->setup(device, error))
		return FALSE;

	/* get the version and other properties from the hardware while open */
	g_assert(self != NULL);
	g_assert(usb_device != NULL);
	if (!fu_aver_hid_device_get_version(device, error))
		return FALSE;

	return TRUE;
}

static FuFirmware *
fu_aver_hid_device_prepare_firmware(FuDevice *device,
				    GBytes *fw,
				    FwupdInstallFlags flags,
				    GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_aver_hid_firmware_new();
	if (!fu_firmware_parse(firmware, fw, flags, error))
		return NULL;
	return g_steal_pointer(&firmware);
}

static gboolean
fu_aver_hid_device_isp_file_dnload(FuAverHidDevice *self,
				   GPtrArray *chunks,
				   FuProgress *progress,
				   GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		g_autoptr(GByteArray) req = fu_struct_aver_hid_req_isp_file_dnload_new();
		g_autoptr(GByteArray) res = fu_struct_aver_hid_res_isp_status_new();

		/* copy in payload */
		fu_struct_aver_hid_req_isp_file_dnload_set_custom_isp_cmd(
		    req,
		    FU_AVER_HID_CUSTOM_ISP_CMD_FILE_DNLOAD);
		if (!fu_memcpy_safe(req->data,
				    req->len,
				    FU_STRUCT_AVER_HID_REQ_ISP_FILE_DNLOAD_OFFSET_DATA, /* dst */
				    fu_chunk_get_data(chk),
				    fu_chunk_get_data_sz(chk),
				    0x0, /* src */
				    fu_chunk_get_data_sz(chk),
				    error))
			return FALSE;

		/* resize the last packet */
		if ((i == (chunks->len - 1)) && (fu_chunk_get_data_sz(chk) < FU_STRUCT_AVER_HID_REQ_ISP_FILE_DNLOAD_SIZE_DATA))
			fu_byte_array_set_size(req, 3 + fu_chunk_get_data_sz(chk), 0x0);
		if (!fu_aver_hid_device_transfer(self, req, res, error))
			return FALSE;
		if (!fu_struct_aver_hid_res_isp_status_validate(res->data, res->len, 0x0, error))
			return FALSE;
		/* invalid chunk */
		if (fu_struct_aver_hid_res_isp_status_get_status(res) ==
		    FU_AVER_HID_STATUS_FILEERR) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_BUSY,
				    "device has status %s",
				    fu_aver_hid_status_to_string(
					fu_struct_aver_hid_res_isp_status_get_status(res)));
			return FALSE;
		}

		/* update progress */
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_aver_hid_device_wait_for_ready_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuAverHidDevice *self = FU_AVER_HID_DEVICE(device);
	g_autoptr(GByteArray) req = fu_struct_aver_hid_req_isp_new();
	g_autoptr(GByteArray) res = fu_struct_aver_hid_res_isp_status_new();
	fu_struct_aver_hid_req_isp_set_custom_isp_cmd(req, FU_AVER_HID_CUSTOM_ISP_CMD_STATUS);
	if (!fu_aver_hid_device_transfer(self, req, res, error))
		return FALSE;
	if (!fu_struct_aver_hid_res_isp_status_validate(res->data, res->len, 0x0, error))
		return FALSE;
	if (fu_struct_aver_hid_res_isp_status_get_status(res) != FU_AVER_HID_STATUS_READY) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_BUSY,
			    "device has status %s",
			    fu_aver_hid_status_to_string(
				fu_struct_aver_hid_res_isp_status_get_status(res)));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_aver_hid_device_isp_file_start(FuAverHidDevice *self,
				  guint32 *sz,
				  const gchar *name,
				  GError **error)
{
	g_autoptr(GByteArray) req = fu_struct_aver_hid_req_isp_file_start_new();
	g_autoptr(GByteArray) res = fu_struct_aver_hid_res_isp_status_new();
	fu_struct_aver_hid_req_isp_file_start_set_custom_isp_cmd(
	    req,
	    FU_AVER_HID_CUSTOM_ISP_CMD_FILE_START);
	if (!fu_struct_aver_hid_req_isp_file_start_set_file_name(req,
								 (guint8 *)name,
								 FU_AVER_HID_ISP_FILE_NAME_LENGTH,
								 error))
		return FALSE;
	if (!fu_struct_aver_hid_req_isp_file_start_set_file_size(
		req,
		(guint8 *)sz,
		FU_STRUCT_AVER_HID_REQ_ISP_FILE_START_SIZE_FILE_SIZE,
		error))
		return FALSE;
	if (!fu_aver_hid_device_transfer(self, req, res, error))
		return FALSE;
	if (!fu_struct_aver_hid_res_isp_status_validate(res->data, res->len, 0x0, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_aver_hid_device_isp_file_end(FuAverHidDevice *self,
				guint32 *sz,
				const gchar *name,
				GError **error)
{
	g_autoptr(GByteArray) req = fu_struct_aver_hid_req_isp_file_end_new();
	g_autoptr(GByteArray) res = fu_struct_aver_hid_res_isp_status_new();
	fu_struct_aver_hid_req_isp_file_end_set_custom_isp_cmd(req,
							       FU_AVER_HID_CUSTOM_ISP_CMD_FILE_END);
	if (!fu_struct_aver_hid_req_isp_file_end_set_file_name(req,
							       (guint8 *)name,
							       FU_AVER_HID_ISP_FILE_NAME_LENGTH,
							       error))
		return FALSE;
	fu_struct_aver_hid_req_isp_file_end_set_end_flag(req, 1);
	if (!fu_struct_aver_hid_req_isp_file_end_set_file_size(
		req,
		(guint8 *)sz,
		FU_STRUCT_AVER_HID_REQ_ISP_FILE_END_SIZE_FILE_SIZE,
		error))
		return FALSE;
	if (!fu_aver_hid_device_transfer(self, req, res, error))
		return FALSE;
	if (!fu_struct_aver_hid_res_isp_status_validate(res->data, res->len, 0x0, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_aver_hid_device_isp_start(FuAverHidDevice *self, GError **error)
{
	g_autoptr(GByteArray) req = fu_struct_aver_hid_req_isp_new();
	g_autoptr(GByteArray) res = fu_struct_aver_hid_res_isp_status_new();
	fu_struct_aver_hid_req_isp_start_set_custom_isp_cmd(req, FU_AVER_HID_CUSTOM_ISP_CMD_START);
	if (!fu_aver_hid_device_transfer(self, req, res, error))
		return FALSE;
	if (!fu_struct_aver_hid_res_isp_status_validate(res->data, res->len, 0x0, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_aver_hid_device_wait_for_reboot_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuAverHidDevice *self = FU_AVER_HID_DEVICE(device);
	FuProgress *progress = FU_PROGRESS(user_data);
	g_autoptr(GByteArray) req = fu_struct_aver_hid_req_isp_new();
	g_autoptr(GByteArray) res = fu_struct_aver_hid_res_isp_status_new();

	fu_struct_aver_hid_req_isp_set_custom_isp_cmd(req, FU_AVER_HID_CUSTOM_ISP_CMD_STATUS);
	if (!fu_aver_hid_device_transfer(self, req, res, error)) {
		return FALSE;
	}
	if (fu_struct_aver_hid_res_isp_status_get_status(res) == FU_AVER_HID_STATUS_ISPING) {
		guint8 percentage = fu_struct_aver_hid_res_isp_status_get_progress(res);
		if (percentage < 100)
			fu_progress_set_percentage(progress, percentage);
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_BUSY,
			    "device has status %s",
			    fu_aver_hid_status_to_string(
				fu_struct_aver_hid_res_isp_status_get_status(res)));
		return FALSE;
	}
	if (fu_struct_aver_hid_res_isp_status_get_status(res) != FU_AVER_HID_STATUS_REBOOT) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_BUSY,
			    "device has status %s",
			    fu_aver_hid_status_to_string(
				fu_struct_aver_hid_res_isp_status_get_status(res)));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_aver_hid_device_write_firmware(FuDevice *device,
				  FuFirmware *firmware,
				  FuProgress *progress,
				  FwupdInstallFlags flags,
				  GError **error)
{
	FuAverHidDevice *self = FU_AVER_HID_DEVICE(device);
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GBytes) aver_fw = NULL;
	g_autoptr(GBytes) audio_fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(FuArchive) archive2 = NULL;
	const gchar *aver_fw_name = NULL;
	g_autofree gchar *audio_fw_name = NULL;
	guint32 fw_size = 0;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 44, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 15, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 15, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 15, NULL);

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* decompress twice :
	 * Each SoC has a compressed file,
	 * and then all the compressed files will be combined into a single compressed fw file.
	 */
	archive = fu_archive_new(fw, FU_ARCHIVE_FLAG_NONE, error);
	if (archive == NULL)
		return FALSE;
	aver_fw_name = fu_firmware_get_filename(firmware);
	aver_fw = fu_archive_lookup_by_fn(archive, aver_fw_name, error);
	if (aver_fw == NULL)
		return FALSE;
	archive2 = fu_archive_new(aver_fw, FU_ARCHIVE_FLAG_NONE, error);
	if (archive2 == NULL)
		return FALSE;
	audio_fw_name = g_strdup_printf("%s.audio.dat", fu_firmware_get_version(firmware));
	audio_fw = fu_archive_lookup_by_fn(archive2, audio_fw_name, error);
	if (audio_fw == NULL)
		return FALSE;
	fw_size = g_bytes_get_size(audio_fw);

	/* wait for ST_READY */
	if (!fu_device_retry_full(device,
				  fu_aver_hid_device_wait_for_ready_cb,
				  5,
				  FU_AVER_HID_DEVICE_GET_STATUS_POLL_INTERVAL,
				  NULL,
				  error))
		return FALSE;
	fu_progress_step_done(progress);

	/* ISP_FILE_START */
	if (!fu_aver_hid_device_isp_file_start(self, &fw_size, aver_fw_name, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* ISP_FILE_DNLOAD */
	chunks = fu_chunk_array_new_from_bytes(audio_fw,
					       0x00,
					       0x00, /* page_sz */
					       FU_STRUCT_AVER_HID_REQ_ISP_FILE_DNLOAD_SIZE_DATA);

	if (!fu_aver_hid_device_isp_file_dnload(self,
						chunks,
						fu_progress_get_child(progress),
						error))
		return FALSE;
	fu_progress_step_done(progress);

	/* ISP_FILE_END */
	if (!fu_aver_hid_device_isp_file_end(self, &fw_size, aver_fw_name, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* ISP_START */
	if (!fu_aver_hid_device_isp_start(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* poll for the actual write progress */
	if (!fu_device_retry_full(device,
				  fu_aver_hid_device_wait_for_reboot_cb,
				  FU_AVER_HID_DEVICE_ISP_TIMEOUT,
				  FU_AVER_HID_DEVICE_GET_STATUS_POLL_INTERVAL,
				  fu_progress_get_child(progress),
				  error))
		return FALSE;

	fu_progress_step_done(progress);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	/* success! */
	return TRUE;
}

static void
fu_aver_hid_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 57, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 43, "reload");
}

static void
fu_aver_hid_device_init(FuAverHidDevice *self)
{
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_add_protocol(FU_DEVICE(self), "com.aver.hid");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_ONLY_WAIT_FOR_REPLUG);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_AUTO_PAUSE_POLLING);
	fu_device_set_poll_interval(FU_DEVICE(self), FU_AVER_HID_DEVICE_POLL_INTERVAL);
	fu_device_set_remove_delay(FU_DEVICE(self), 150000);
	fu_hid_device_add_flag(FU_HID_DEVICE(self), FU_HID_DEVICE_FLAG_RETRY_FAILURE);
	fu_hid_device_add_flag(FU_HID_DEVICE(self), FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER);
}

static void
fu_aver_hid_device_class_init(FuAverHidDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->poll = fu_aver_hid_device_poll;
	klass_device->setup = fu_aver_hid_device_setup;
	klass_device->prepare_firmware = fu_aver_hid_device_prepare_firmware;
	klass_device->write_firmware = fu_aver_hid_device_write_firmware;
	klass_device->set_progress = fu_aver_hid_device_set_progress;
}
