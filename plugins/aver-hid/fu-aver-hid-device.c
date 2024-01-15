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

#define FU_AVER_HID_DEVICE_TIMEOUT		    200	   /* ms */
#define FU_AVER_HID_DEVICE_SAFEISP_RES_TIMEOUT	    100000 /* ms */
#define FU_AVER_HID_DEVICE_GET_STATUS_POLL_INTERVAL 1000   /* ms */
#define FU_AVER_HID_DEVICE_POLL_INTERVAL	    5000   /* ms */
#define FU_AVER_HID_DEVICE_ISP_RETRY_COUNT	    300
#define FU_AVER_HID_DEVICE_ISP_UNTAR_WAIT_COUNT	    600

#define FU_AVER_HID_FLAG_DUAL_ISP (1 << 0)
#define FU_AVER_HID_FLAG_SAFE_ISP (1 << 1)

enum { ISP_CX3, ISP_M12 };

static gboolean
fu_aver_hid_device_transfer(FuAverHidDevice *self, GByteArray *req, GByteArray *res, GError **error)
{
	if (req != NULL) {
		if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
					      req->data[0],
					      req->data,
					      req->len,
					      FU_AVER_HID_DEVICE_TIMEOUT,
					      FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER,
					      error)) {
			g_prefix_error(error, "failed to send packet: ");
			return FALSE;
		}
	}
	if (res != NULL) {
		if (fu_device_has_private_flag(FU_DEVICE(self), FU_AVER_HID_FLAG_SAFE_ISP)) {
			if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
						      res->data[0],
						      res->data,
						      res->len,
						      FU_AVER_HID_DEVICE_SAFEISP_RES_TIMEOUT,
						      FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER,
						      error)) {
				g_prefix_error(error, "failed to receive packet: ");
				return FALSE;
			}
		} else {
			if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
						      res->data[0],
						      res->data,
						      res->len,
						      FU_AVER_HID_DEVICE_TIMEOUT,
						      FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER,
						      error)) {
				g_prefix_error(error, "failed to receive packet: ");
				return FALSE;
			}
		}
		g_debug("custom-isp-cmd: %s [0x%x]",
			fu_aver_hid_custom_isp_cmd_to_string(
			    fu_struct_aver_hid_res_isp_get_custom_isp_cmd(res)),
			fu_struct_aver_hid_res_isp_get_custom_isp_cmd(res));
	}
	return TRUE;
}

static gboolean
fu_aver_hid_device_poll(FuDevice *device, GError **error)
{
	FuAverHidDevice *self = FU_AVER_HID_DEVICE(device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;

	if (fu_device_has_private_flag(FU_DEVICE(self), FU_AVER_HID_FLAG_SAFE_ISP)) {
		g_autoptr(GByteArray) req = fu_struct_aver_hid_req_safeisp_new();
		g_autoptr(GByteArray) res = fu_struct_aver_hid_res_safeisp_new();
		fu_struct_aver_hid_req_safeisp_set_custom_cmd(
		    req,
		    FU_AVER_HID_CUSTOM_SAFEISP_CMD_SAFEISP_GET_VERSION);
		if (!fu_aver_hid_device_transfer(self, req, res, error))
			return FALSE;
	} else {
		g_autoptr(GByteArray) req = fu_struct_aver_hid_req_isp_new();
		g_autoptr(GByteArray) res = fu_struct_aver_hid_res_isp_status_new();

		fu_struct_aver_hid_req_isp_set_custom_isp_cmd(req,
							      FU_AVER_HID_CUSTOM_ISP_CMD_STATUS);
		if (!fu_aver_hid_device_transfer(self, req, res, error))
			return FALSE;
		if (!fu_struct_aver_hid_res_isp_status_validate(res->data, res->len, 0x0, error))
			return FALSE;
		if (fu_struct_aver_hid_res_isp_status_get_status(res) == FU_AVER_HID_STATUS_BUSY) {
			fu_device_add_problem(device, FWUPD_DEVICE_PROBLEM_IN_USE);
		} else {
			fu_device_remove_problem(device, FWUPD_DEVICE_PROBLEM_IN_USE);
		}
	}

	return TRUE;
}

static gboolean
fu_aver_hid_device_ensure_version(FuAverHidDevice *self, GError **error)
{
	g_autofree gchar *ver = NULL;
	if (fu_device_has_private_flag(FU_DEVICE(self), FU_AVER_HID_FLAG_SAFE_ISP)) {
		g_autoptr(GByteArray) req = fu_struct_aver_hid_req_safeisp_new();
		g_autoptr(GByteArray) res = fu_struct_aver_hid_res_safeisp_new();
		fu_struct_aver_hid_req_safeisp_set_custom_cmd(
		    req,
		    FU_AVER_HID_CUSTOM_SAFEISP_CMD_SAFEISP_GET_VERSION);
		if (!fu_aver_hid_device_transfer(self, req, res, error))
			return FALSE;
		if (!fu_struct_aver_hid_res_safeisp_device_version_validate(res->data,
									    res->len,
									    0x0,
									    error))
			return FALSE;
		ver = fu_strsafe(
		    (const gchar *)fu_struct_aver_hid_res_safeisp_device_version_get_ver(res, NULL),
		    FU_STRUCT_AVER_HID_RES_SAFEISP_DEVICE_VERSION_SIZE_VER);
	} else {
		g_autoptr(GByteArray) req = fu_struct_aver_hid_req_device_version_new();
		g_autoptr(GByteArray) res = fu_struct_aver_hid_res_device_version_new();
		g_autoptr(GError) error_local = NULL;

		if (!fu_aver_hid_device_transfer(self, req, res, &error_local)) {
			if (g_error_matches(error_local,
					    G_USB_DEVICE_ERROR,
					    G_USB_DEVICE_ERROR_TIMED_OUT)) {
				g_debug("ignoring %s", error_local->message);
				fu_device_set_version(FU_DEVICE(self), "0.0.0000.00");
				return TRUE;
			}
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
		if (!fu_struct_aver_hid_res_device_version_validate(res->data,
								    res->len,
								    0x0,
								    error))
			return FALSE;
		ver = fu_strsafe(
		    (const gchar *)fu_struct_aver_hid_res_device_version_get_ver(res, NULL),
		    FU_STRUCT_AVER_HID_RES_DEVICE_VERSION_SIZE_VER);
	}
	fu_device_set_version(FU_DEVICE(self), ver);
	return TRUE;
}

static gboolean
fu_aver_hid_device_setup(FuDevice *device, GError **error)
{
	FuAverHidDevice *self = FU_AVER_HID_DEVICE(device);

	/* HidDevice->setup */
	if (!FU_DEVICE_CLASS(fu_aver_hid_device_parent_class)->setup(device, error))
		return FALSE;

	/* using isp status requests as polling device requests */
	if (!fu_aver_hid_device_poll(device, error))
		return FALSE;

	/* get the version from the hardware while open */
	if (!fu_aver_hid_device_ensure_version(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static FuFirmware *
fu_aver_hid_device_prepare_firmware(FuDevice *device,
				    GInputStream *stream,
				    FwupdInstallFlags flags,
				    GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_aver_hid_firmware_new();
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;
	return g_steal_pointer(&firmware);
}

static gboolean
fu_aver_hid_device_isp_file_dnload(FuAverHidDevice *self,
				   FuChunkArray *chunks,
				   FuProgress *progress,
				   GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GByteArray) req = fu_struct_aver_hid_req_isp_file_dnload_new();
		g_autoptr(GByteArray) res = fu_struct_aver_hid_res_isp_status_new();

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		/* copy in payload */
		if (fu_device_has_private_flag(FU_DEVICE(self), FU_AVER_HID_FLAG_DUAL_ISP)) {
			fu_struct_aver_hid_req_isp_file_dnload_set_custom_isp_cmd(
			    req,
			    FU_AVER_HID_CUSTOM_ISP_CMD_ALL_FILE_DNLOAD);
		} else {
			fu_struct_aver_hid_req_isp_file_dnload_set_custom_isp_cmd(
			    req,
			    FU_AVER_HID_CUSTOM_ISP_CMD_FILE_DNLOAD);
		}
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
		if ((i == (fu_chunk_array_length(chunks) - 1)) &&
		    (fu_chunk_get_data_sz(chk) < FU_STRUCT_AVER_HID_REQ_ISP_FILE_DNLOAD_SIZE_DATA))
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
				  gsize sz,
				  const gchar *name,
				  GError **error)
{
	g_autoptr(GByteArray) req = fu_struct_aver_hid_req_isp_file_start_new();
	g_autoptr(GByteArray) res = fu_struct_aver_hid_res_isp_status_new();

	if (fu_device_has_private_flag(FU_DEVICE(self), FU_AVER_HID_FLAG_DUAL_ISP)) {
		fu_struct_aver_hid_req_isp_file_start_set_custom_isp_cmd(
		    req,
		    FU_AVER_HID_CUSTOM_ISP_CMD_ALL_FILE_START);
	} else {
		fu_struct_aver_hid_req_isp_file_start_set_custom_isp_cmd(
		    req,
		    FU_AVER_HID_CUSTOM_ISP_CMD_FILE_START);
	}
	if (!fu_struct_aver_hid_req_isp_file_start_set_file_name(req, name, error))
		return FALSE;
	fu_struct_aver_hid_req_isp_file_start_set_file_size(req, sz);
	if (!fu_aver_hid_device_transfer(self, req, res, error))
		return FALSE;
	if (!fu_struct_aver_hid_res_isp_status_validate(res->data, res->len, 0x0, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_aver_hid_device_isp_file_end(FuAverHidDevice *self, gsize sz, const gchar *name, GError **error)
{
	g_autoptr(GByteArray) req = fu_struct_aver_hid_req_isp_file_end_new();
	g_autoptr(GByteArray) res = fu_struct_aver_hid_res_isp_status_new();

	if (fu_device_has_private_flag(FU_DEVICE(self), FU_AVER_HID_FLAG_DUAL_ISP)) {
		fu_struct_aver_hid_req_isp_file_end_set_custom_isp_cmd(
		    req,
		    FU_AVER_HID_CUSTOM_ISP_CMD_ALL_FILE_END);
	} else {
		fu_struct_aver_hid_req_isp_file_end_set_custom_isp_cmd(
		    req,
		    FU_AVER_HID_CUSTOM_ISP_CMD_FILE_END);
	}
	if (!fu_struct_aver_hid_req_isp_file_end_set_file_name(req, name, error))
		return FALSE;
	fu_struct_aver_hid_req_isp_file_end_set_end_flag(req, 1);
	fu_struct_aver_hid_req_isp_file_end_set_file_size(req, sz);
	if (!fu_aver_hid_device_transfer(self, req, res, error))
		return FALSE;
	if (!fu_struct_aver_hid_res_isp_status_validate(res->data, res->len, 0x0, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_aver_hid_device_wait_for_untar_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuAverHidDevice *self = FU_AVER_HID_DEVICE(device);
	g_autoptr(GByteArray) req = fu_struct_aver_hid_req_isp_file_end_new();
	g_autoptr(GByteArray) res = fu_struct_aver_hid_res_isp_status_new();

	fu_struct_aver_hid_req_isp_set_custom_isp_cmd(req, FU_AVER_HID_CUSTOM_ISP_CMD_STATUS);
	if (!fu_aver_hid_device_transfer(self, req, res, error))
		return FALSE;

	g_info("isp status: %s",
	       fu_aver_hid_status_to_string(fu_struct_aver_hid_res_isp_status_get_status(res)));
	if (fu_struct_aver_hid_res_isp_status_get_status(res) != FU_AVER_HID_STATUS_WAITUSR) {
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
fu_aver_hid_device_isp_start(FuAverHidDevice *self, GError **error)
{
	g_autoptr(GByteArray) req = fu_struct_aver_hid_req_isp_new();
	g_autoptr(GByteArray) res = fu_struct_aver_hid_res_isp_status_new();

	if (fu_device_has_private_flag(FU_DEVICE(self), FU_AVER_HID_FLAG_DUAL_ISP)) {
		fu_struct_aver_hid_req_isp_start_set_custom_isp_cmd(
		    req,
		    FU_AVER_HID_CUSTOM_ISP_CMD_ALL_START);
	} else {
		fu_struct_aver_hid_req_isp_start_set_custom_isp_cmd(
		    req,
		    FU_AVER_HID_CUSTOM_ISP_CMD_START);
	}
	if (!fu_aver_hid_device_transfer(self, req, res, error))
		return FALSE;
	if (!fu_struct_aver_hid_res_isp_status_validate(res->data, res->len, 0x0, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_aver_hid_device_isp_reboot(FuAverHidDevice *self, GError **error)
{
	g_autoptr(GByteArray) req = fu_struct_aver_hid_req_isp_new();
	fu_struct_aver_hid_req_isp_set_custom_isp_cmd(req, FU_AVER_HID_CUSTOM_ISP_CMD_ISP_REBOOT);
	return fu_aver_hid_device_transfer(self, req, NULL, error);
}

static gboolean
fu_aver_hid_device_wait_for_reboot_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuAverHidDevice *self = FU_AVER_HID_DEVICE(device);
	FuProgress *progress = FU_PROGRESS(user_data);
	g_autoptr(GByteArray) req = fu_struct_aver_hid_req_isp_new();
	g_autoptr(GByteArray) res = fu_struct_aver_hid_res_isp_status_new();

	fu_struct_aver_hid_req_isp_set_custom_isp_cmd(req, FU_AVER_HID_CUSTOM_ISP_CMD_STATUS);
	if (!fu_aver_hid_device_transfer(self, req, res, error))
		return FALSE;
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
fu_aver_hid_device_safeisp_support(FuAverHidDevice *self, GError **error)
{
	g_autoptr(GByteArray) req = fu_struct_aver_hid_req_safeisp_new();
	g_autoptr(GByteArray) res = fu_struct_aver_hid_res_safeisp_new();

	fu_struct_aver_hid_req_safeisp_set_custom_cmd(
	    req,
	    FU_AVER_HID_CUSTOM_SAFEISP_CMD_SAFEISP_SUPPORT);
	fu_struct_aver_hid_req_safeisp_set_custom_parm0(req, 0x00);
	fu_struct_aver_hid_req_safeisp_set_custom_parm1(req, 0x00);
	if (!fu_aver_hid_device_transfer(self, req, res, error))
		return FALSE;
	if (!fu_struct_aver_hid_res_safeisp_validate(res->data, res->len, 0x0, error))
		return FALSE;
	if (fu_struct_aver_hid_res_safeisp_get_custom_cmd(res) !=
	    FU_AVER_HID_SAFEISP_ACK_STATUS_SAFEISP_SUPPORT)
		return FALSE;
	return TRUE;
}

static gboolean
fu_aver_hid_device_safeisp_upload_prepare(FuAverHidDevice *self,
					  gsize param0,
					  gsize param1,
					  GError **error)
{
	g_autoptr(GByteArray) req = fu_struct_aver_hid_req_safeisp_new();
	g_autoptr(GByteArray) res = fu_struct_aver_hid_res_safeisp_new();

	fu_struct_aver_hid_req_safeisp_set_custom_cmd(
	    req,
	    FU_AVER_HID_CUSTOM_SAFEISP_CMD_SAFEISP_UPLOAD_PREPARE);
	fu_struct_aver_hid_req_safeisp_set_custom_parm0(req, param0);
	fu_struct_aver_hid_req_safeisp_set_custom_parm1(req, param1);
	if (!fu_aver_hid_device_transfer(self, req, res, error))
		return FALSE;
	if (!fu_struct_aver_hid_res_safeisp_validate(res->data, res->len, 0x0, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_aver_hid_device_safeisp_erase_flash(FuAverHidDevice *self,
				       gsize param0,
				       gsize param1,
				       GError **error)
{
	g_autoptr(GByteArray) req = fu_struct_aver_hid_req_safeisp_new();
	g_autoptr(GByteArray) res = fu_struct_aver_hid_res_safeisp_new();

	fu_struct_aver_hid_req_safeisp_set_custom_cmd(
	    req,
	    FU_AVER_HID_CUSTOM_SAFEISP_CMD_SAFEISP_ERASE_TEMP);
	fu_struct_aver_hid_req_safeisp_set_custom_parm0(req, param0);
	fu_struct_aver_hid_req_safeisp_set_custom_parm1(req, param1);
	if (!fu_aver_hid_device_transfer(self, req, res, error))
		return FALSE;
	if (!fu_struct_aver_hid_res_safeisp_validate(res->data, res->len, 0x0, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_aver_hid_device_safeisp_upload(FuAverHidDevice *self,
				  FuChunkArray *chunks,
				  FuProgress *progress,
				  guint ISP_SOC,
				  GError **error)
{
	gsize addr = 0;
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GByteArray) req = fu_struct_aver_hid_req_safeisp_new();
		g_autoptr(GByteArray) res = fu_struct_aver_hid_res_safeisp_new();

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		/* copy in payload */
		if (ISP_SOC == ISP_CX3)
			fu_struct_aver_hid_req_safeisp_set_custom_cmd(
			    req,
			    FU_AVER_HID_CUSTOM_SAFEISP_CMD_SAFEISP_UPLOAD_TO_CX3);
		else if (ISP_SOC == ISP_M12)
			fu_struct_aver_hid_req_safeisp_set_custom_cmd(
			    req,
			    FU_AVER_HID_CUSTOM_SAFEISP_CMD_SAFEISP_UPLOAD_TO_M12MO);
		else
			return FALSE;

		fu_struct_aver_hid_req_safeisp_set_custom_parm0(req, addr);
		fu_struct_aver_hid_req_safeisp_set_custom_parm1(req, 512);

		if (!fu_memcpy_safe(req->data,
				    req->len,
				    FU_STRUCT_AVER_HID_REQ_SAFEISP_OFFSET_DATA, /* dst */
				    fu_chunk_get_data(chk),
				    fu_chunk_get_data_sz(chk),
				    0x0, /* src */
				    fu_chunk_get_data_sz(chk),
				    error))
			return FALSE;

		/* resize the last packet */
		if ((i == (fu_chunk_array_length(chunks) - 1)) &&
		    (fu_chunk_get_data_sz(chk) < 512)) {
			fu_byte_array_set_size(req, 12 + fu_chunk_get_data_sz(chk), 0x0);
			fu_struct_aver_hid_req_safeisp_set_custom_parm1(req,
									fu_chunk_get_data_sz(chk));
		}
		if (!fu_aver_hid_device_transfer(self, req, res, error))
			return FALSE;
		if (!fu_struct_aver_hid_res_safeisp_validate(res->data, res->len, 0x0, error))
			return FALSE;

		addr += 512;

		/* update progress */
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_aver_hid_device_safeisp_upload_checksum(FuAverHidDevice *self,
					   gsize param0,
					   gsize param1,
					   GError **error)
{
	g_autoptr(GByteArray) req = fu_struct_aver_hid_req_safeisp_new();
	g_autoptr(GByteArray) res = fu_struct_aver_hid_res_safeisp_new();

	fu_struct_aver_hid_req_safeisp_set_custom_cmd(
	    req,
	    FU_AVER_HID_CUSTOM_SAFEISP_CMD_SAFEISP_UPLOAD_COMPARE_CHECKSUM);
	fu_struct_aver_hid_req_safeisp_set_custom_parm0(req, param0);
	fu_struct_aver_hid_req_safeisp_set_custom_parm1(req, param1);
	if (!fu_aver_hid_device_transfer(self, req, res, error))
		return FALSE;
	if (!fu_struct_aver_hid_res_safeisp_validate(res->data, res->len, 0x0, error))
		return FALSE;
	if (fu_struct_aver_hid_req_safeisp_get_custom_cmd(res) !=
	    FU_AVER_HID_SAFEISP_ACK_STATUS_SUCCESS)
		return FALSE;
	return TRUE;
}

static gboolean
fu_aver_hid_device_safeisp_update(FuAverHidDevice *self, gsize param0, gsize param1, GError **error)
{
	g_autoptr(GByteArray) req = fu_struct_aver_hid_req_safeisp_new();

	fu_struct_aver_hid_req_safeisp_set_custom_cmd(
	    req,
	    FU_AVER_HID_CUSTOM_SAFEISP_CMD_SAFEISP_UPDATE_START);
	fu_struct_aver_hid_req_safeisp_set_custom_parm0(req, param0);
	fu_struct_aver_hid_req_safeisp_set_custom_parm1(req, param1);
	if (!fu_aver_hid_device_transfer(self, req, NULL, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_aver_hid_device_write_firmware_for_safeisp(FuDevice *device,
					      FuFirmware *firmware,
					      FuProgress *progress,
					      FwupdInstallFlags flags,
					      GError **error)
{
	FuAverHidDevice *self = FU_AVER_HID_DEVICE(device);
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GBytes) cx3_fw = NULL;
	g_autoptr(GBytes) m12_fw = NULL;
	gsize cx3_fw_size;
	gsize m12_fw_size;
	const guint8 *cx3_fw_buf;
	const guint8 *m12_fw_buf;
	guint32 cx3_checksum = 0;
	guint32 m12_checksum = 0;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 5, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 58, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 34, NULL);

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* decompress */
	archive = fu_archive_new(fw, FU_ARCHIVE_FLAG_NONE, error);
	if (archive == NULL)
		return FALSE;
	cx3_fw = fu_archive_lookup_by_fn(archive, "update/cx3uvc.img", error);
	if (cx3_fw == NULL)
		return FALSE;
	m12_fw = fu_archive_lookup_by_fn(archive, "update/RS_M12MO.bin", error);
	if (m12_fw == NULL)
		return FALSE;

	/* CX3 fw file size should be less than 256KB */
	cx3_fw_buf = g_bytes_get_data(cx3_fw, &cx3_fw_size);
	if (cx3_fw_size > 256 * 1024)
		return FALSE;
	/* calculate CX3 firmware checksum */
	for (guint i = 0; i < cx3_fw_size; i++)
		cx3_checksum += cx3_fw_buf[i];
	/* M12 fw file size should be less than 3MB */
	m12_fw_buf = g_bytes_get_data(m12_fw, &m12_fw_size);
	if (m12_fw_size > 3 * 1024 * 1024)
		return FALSE;
	/* calculate M12 firmware checksum */
	for (guint i = 0; i < m12_fw_size; i++)
		m12_checksum += m12_fw_buf[i];

	/* Check if the device supports safeisp */
	if (!fu_aver_hid_device_safeisp_support(self, error))
		return FALSE;

	/* CX3 safeisp prepare */
	if (!fu_aver_hid_device_safeisp_upload_prepare(self, ISP_CX3, cx3_fw_size, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* CX3 safeisp erase flash */
	if (!fu_aver_hid_device_safeisp_erase_flash(self, ISP_CX3, 0x0, error))
		return FALSE;

	/* CX3 safeisp firmware upload */
	chunks = fu_chunk_array_new_from_bytes(cx3_fw, 0x00, 512);
	if (!fu_aver_hid_device_safeisp_upload(self,
					       chunks,
					       fu_progress_get_child(progress),
					       ISP_CX3,
					       error))
		return FALSE;
	fu_progress_step_done(progress);

	/* CX3 safeisp checksum */
	if (!fu_aver_hid_device_safeisp_upload_checksum(self, ISP_CX3, cx3_checksum, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* M12 safeisp prepare */
	if (!fu_aver_hid_device_safeisp_upload_prepare(self, ISP_M12, m12_fw_size, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* M12 safeisp erase flash */
	if (!fu_aver_hid_device_safeisp_erase_flash(self, ISP_M12, 0x0, error))
		return FALSE;

	/* M12 safeisp firmware upload */
	chunks = fu_chunk_array_new_from_bytes(m12_fw, 0x00, 512);
	if (!fu_aver_hid_device_safeisp_upload(self,
					       chunks,
					       fu_progress_get_child(progress),
					       ISP_M12,
					       error))
		return FALSE;
	fu_progress_step_done(progress);

	/* M12 safeisp checksum */
	if (!fu_aver_hid_device_safeisp_upload_checksum(self, ISP_M12, m12_checksum, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* update device */
	if (!fu_aver_hid_device_safeisp_update(self, ((1 << ISP_CX3) | (1 << ISP_M12)), 0x0, error))
		return FALSE;

	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	/* success! */
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
	const gchar *aver_fw_name = NULL;
	gsize fw_size;
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(GBytes) aver_fw = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* AVer CAM340plus uses a different upgrade process */
	if (fu_device_has_private_flag(FU_DEVICE(self), FU_AVER_HID_FLAG_SAFE_ISP)) {
		return fu_aver_hid_device_write_firmware_for_safeisp(device,
								     firmware,
								     progress,
								     flags,
								     error);
	}

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 80, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 15, NULL);

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* decompress */
	archive = fu_archive_new(fw, FU_ARCHIVE_FLAG_NONE, error);
	if (archive == NULL)
		return FALSE;
	aver_fw_name = fu_firmware_get_filename(firmware);
	aver_fw = fu_archive_lookup_by_fn(archive, aver_fw_name, error);
	if (aver_fw == NULL)
		return FALSE;
	fw_size = g_bytes_get_size(aver_fw);

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
	if (!fu_aver_hid_device_isp_file_start(self, fw_size, aver_fw_name, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* ISP_FILE_DNLOAD */
	chunks = fu_chunk_array_new_from_bytes(aver_fw,
					       0x00,
					       FU_STRUCT_AVER_HID_REQ_ISP_FILE_DNLOAD_SIZE_DATA);

	if (!fu_aver_hid_device_isp_file_dnload(self,
						chunks,
						fu_progress_get_child(progress),
						error))
		return FALSE;
	fu_progress_step_done(progress);

	/* ISP_FILE_END */
	if (!fu_aver_hid_device_isp_file_end(self, fw_size, aver_fw_name, error))
		return FALSE;

	/* poll for the file untar progress */
	if (!fu_device_retry_full(device,
				  fu_aver_hid_device_wait_for_untar_cb,
				  FU_AVER_HID_DEVICE_ISP_UNTAR_WAIT_COUNT,
				  FU_AVER_HID_DEVICE_GET_STATUS_POLL_INTERVAL,
				  fu_progress_get_child(progress),
				  error))
		return FALSE;
	fu_progress_step_done(progress);

	/* ISP_START */
	if (!fu_aver_hid_device_isp_start(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* poll for the actual write progress */
	if (!fu_device_retry_full(device,
				  fu_aver_hid_device_wait_for_reboot_cb,
				  FU_AVER_HID_DEVICE_ISP_RETRY_COUNT,
				  FU_AVER_HID_DEVICE_GET_STATUS_POLL_INTERVAL,
				  fu_progress_get_child(progress),
				  error))
		return FALSE;

	fu_progress_step_done(progress);

	/* send ISP_REBOOT, no response expected */
	if (!fu_aver_hid_device_isp_reboot(self, error))
		return FALSE;
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	/* success! */
	return TRUE;
}

static void
fu_aver_hid_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 74, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 25, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "reload");
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
	fu_hid_device_add_flag(FU_HID_DEVICE(self), FU_HID_DEVICE_FLAG_AUTODETECT_EPS);
	fu_device_register_private_flag(FU_DEVICE(self), FU_AVER_HID_FLAG_DUAL_ISP, "dual-isp");
	fu_device_register_private_flag(FU_DEVICE(self), FU_AVER_HID_FLAG_SAFE_ISP, "safe-isp");
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
