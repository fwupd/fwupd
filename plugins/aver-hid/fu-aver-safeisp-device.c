/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-aver-hid-firmware.h"
#include "fu-aver-hid-struct.h"
#include "fu-aver-safeisp-device.h"

struct _FuAverSafeispDevice {
	FuHidDevice parent_instance;
};

G_DEFINE_TYPE(FuAverSafeispDevice, fu_aver_safeisp_device, FU_TYPE_HID_DEVICE)

#define FU_AVER_SAFEISP_DEVICE_TIMEOUT	     100000 /* ms */
#define FU_AVER_SAFEISP_DEVICE_POLL_INTERVAL 5000   /* ms */

typedef enum { ISP_CX3, ISP_M12 } FuAverSafeIspPartition;

static gboolean
fu_aver_safeisp_device_transfer(FuAverSafeispDevice *self,
				GByteArray *req,
				GByteArray *res,
				GError **error)
{
	if (req != NULL) {
		if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
					      req->data[0],
					      req->data,
					      req->len,
					      FU_AVER_SAFEISP_DEVICE_TIMEOUT,
					      FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER,
					      error)) {
			g_prefix_error(error, "failed to send packet: ");
			return FALSE;
		}
	}
	if (res != NULL) {
		if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
					      res->data[0],
					      res->data,
					      res->len,
					      FU_AVER_SAFEISP_DEVICE_TIMEOUT,
					      FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER,
					      error)) {
			g_prefix_error(error, "failed to receive packet: ");
			return FALSE;
		}
		g_debug("custom-isp-cmd: %s [0x%x]",
			fu_aver_safeisp_custom_cmd_to_string(
			    fu_struct_aver_safeisp_res_get_custom_cmd(res)),
			fu_struct_aver_safeisp_res_get_custom_cmd(res));
	}
	return TRUE;
}

static gboolean
fu_aver_safeisp_device_ensure_version(FuAverSafeispDevice *self, GError **error)
{
	g_autofree gchar *ver = NULL;
	g_autoptr(GByteArray) req = fu_struct_aver_safeisp_req_new();
	g_autoptr(GByteArray) res = fu_struct_aver_safeisp_res_new();
	fu_struct_aver_safeisp_req_set_custom_cmd(req, FU_AVER_SAFEISP_CUSTOM_CMD_GET_VERSION);
	if (!fu_aver_safeisp_device_transfer(self, req, res, error))
		return FALSE;
	if (!fu_struct_aver_safeisp_res_device_version_validate(res->data, res->len, 0x0, error))
		return FALSE;
	ver =
	    fu_strsafe((const gchar *)fu_struct_aver_safeisp_res_device_version_get_ver(res, NULL),
		       FU_STRUCT_AVER_SAFEISP_RES_DEVICE_VERSION_SIZE_VER);
	fu_device_set_version(FU_DEVICE(self), ver);
	return TRUE;
}

static gboolean
fu_aver_safeisp_device_setup(FuDevice *device, GError **error)
{
	FuAverSafeispDevice *self = FU_AVER_SAFEISP_DEVICE(device);

	/* HidDevice->setup */
	if (!FU_DEVICE_CLASS(fu_aver_safeisp_device_parent_class)->setup(device, error))
		return FALSE;
	/* get the version from the hardware while open */
	if (!fu_aver_safeisp_device_ensure_version(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_aver_safeisp_device_support(FuAverSafeispDevice *self, GError **error)
{
	g_autoptr(GByteArray) req = fu_struct_aver_safeisp_req_new();
	g_autoptr(GByteArray) res = fu_struct_aver_safeisp_res_new();

	fu_struct_aver_safeisp_req_set_custom_cmd(req, FU_AVER_SAFEISP_CUSTOM_CMD_SUPPORT);
	if (!fu_aver_safeisp_device_transfer(self, req, res, error))
		return FALSE;
	if (!fu_struct_aver_safeisp_res_validate(res->data, res->len, 0x0, error))
		return FALSE;
	if (fu_struct_aver_safeisp_res_get_custom_cmd(res) != FU_AVER_SAFEISP_ACK_STATUS_SUPPORT)
		return FALSE;
	return TRUE;
}

static gboolean
fu_aver_safeisp_device_upload_prepare(FuAverSafeispDevice *self,
				      FuAverSafeIspPartition partition,
				      gsize size,
				      GError **error)
{
	g_autoptr(GByteArray) req = fu_struct_aver_safeisp_req_new();
	g_autoptr(GByteArray) res = fu_struct_aver_safeisp_res_new();

	fu_struct_aver_safeisp_req_set_custom_cmd(req, FU_AVER_SAFEISP_CUSTOM_CMD_UPLOAD_PREPARE);
	fu_struct_aver_safeisp_req_set_custom_parm0(req, partition);
	fu_struct_aver_safeisp_req_set_custom_parm1(req, size);
	if (!fu_aver_safeisp_device_transfer(self, req, res, error))
		return FALSE;
	if (!fu_struct_aver_safeisp_res_validate(res->data, res->len, 0x0, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_aver_safeisp_device_erase_flash(FuAverSafeispDevice *self,
				   gsize param0,
				   gsize param1,
				   GError **error)
{
	g_autoptr(GByteArray) req = fu_struct_aver_safeisp_req_new();
	g_autoptr(GByteArray) res = fu_struct_aver_safeisp_res_new();

	fu_struct_aver_safeisp_req_set_custom_cmd(req, FU_AVER_SAFEISP_CUSTOM_CMD_ERASE_TEMP);
	fu_struct_aver_safeisp_req_set_custom_parm0(req, param0);
	fu_struct_aver_safeisp_req_set_custom_parm1(req, param1);
	if (!fu_aver_safeisp_device_transfer(self, req, res, error))
		return FALSE;
	if (!fu_struct_aver_safeisp_res_validate(res->data, res->len, 0x0, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_aver_safeisp_device_upload(FuAverSafeispDevice *self,
			      FuChunkArray *chunks,
			      FuProgress *progress,
			      FuAverSafeIspPartition partition,
			      GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GByteArray) req = fu_struct_aver_safeisp_req_new();
		g_autoptr(GByteArray) res = fu_struct_aver_safeisp_res_new();

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		/* copy in payload */
		if (partition == ISP_CX3) {
			fu_struct_aver_safeisp_req_set_custom_cmd(
			    req,
			    FU_AVER_SAFEISP_CUSTOM_CMD_UPLOAD_TO_CX3);
		} else if (partition == ISP_M12) {
			fu_struct_aver_safeisp_req_set_custom_cmd(
			    req,
			    FU_AVER_SAFEISP_CUSTOM_CMD_UPLOAD_TO_M12MO);
		} else {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid argument %u",
				    partition);
			return FALSE;
		}

		fu_struct_aver_safeisp_req_set_custom_parm0(req, fu_chunk_get_address(chk));
		fu_struct_aver_safeisp_req_set_custom_parm1(req, fu_chunk_get_data_sz(chk));

		if (!fu_memcpy_safe(req->data,
				    req->len,
				    FU_STRUCT_AVER_SAFEISP_REQ_OFFSET_DATA, /* dst */
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
			fu_struct_aver_safeisp_req_set_custom_parm1(req, fu_chunk_get_data_sz(chk));
		}
		if (!fu_aver_safeisp_device_transfer(self, req, res, error))
			return FALSE;
		if (!fu_struct_aver_safeisp_res_validate(res->data, res->len, 0x0, error))
			return FALSE;

		/* update progress */
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_aver_safeisp_device_upload_checksum(FuAverSafeispDevice *self,
				       gsize param0,
				       gsize param1,
				       GError **error)
{
	g_autoptr(GByteArray) req = fu_struct_aver_safeisp_req_new();
	g_autoptr(GByteArray) res = fu_struct_aver_safeisp_res_new();

	fu_struct_aver_safeisp_req_set_custom_cmd(
	    req,
	    FU_AVER_SAFEISP_CUSTOM_CMD_UPLOAD_COMPARE_CHECKSUM);
	fu_struct_aver_safeisp_req_set_custom_parm0(req, param0);
	fu_struct_aver_safeisp_req_set_custom_parm1(req, param1);
	if (!fu_aver_safeisp_device_transfer(self, req, res, error))
		return FALSE;
	if (!fu_struct_aver_safeisp_res_validate(res->data, res->len, 0x0, error))
		return FALSE;
	if (fu_struct_aver_safeisp_req_get_custom_cmd(res) != FU_AVER_SAFEISP_ACK_STATUS_SUCCESS)
		return FALSE;
	return TRUE;
}

static gboolean
fu_aver_safeisp_device_update(FuAverSafeispDevice *self, gsize param0, gsize param1, GError **error)
{
	g_autoptr(GByteArray) req = fu_struct_aver_safeisp_req_new();

	fu_struct_aver_safeisp_req_set_custom_cmd(req, FU_AVER_SAFEISP_CUSTOM_CMD_UPDATE_START);
	fu_struct_aver_safeisp_req_set_custom_parm0(req, param0);
	fu_struct_aver_safeisp_req_set_custom_parm1(req, param1);
	if (!fu_aver_safeisp_device_transfer(self, req, NULL, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_aver_safeisp_device_write_firmware(FuDevice *device,
				      FuFirmware *firmware,
				      FuProgress *progress,
				      FwupdInstallFlags flags,
				      GError **error)
{
	FuAverSafeispDevice *self = FU_AVER_SAFEISP_DEVICE(device);
	gsize cx3_fw_size;
	gsize m12_fw_size;
	const guint8 *cx3_fw_buf;
	const guint8 *m12_fw_buf;
	guint32 cx3_checksum = 0;
	guint32 m12_checksum = 0;
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GBytes) cx3_fw = NULL;
	g_autoptr(GBytes) m12_fw = NULL;
	g_autoptr(GInputStream) stream = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 5, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 58, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 34, NULL);

	/* get default image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;

	/* decompress */
	archive = fu_archive_new_stream(stream, FU_ARCHIVE_FLAG_NONE, error);
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
	if (cx3_fw_size > 256 * 1024) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "cx3 file size is invalid: 0x%x",
			    (guint)cx3_fw_size);

		return FALSE;
	}
	/* calculate CX3 firmware checksum */
	cx3_checksum = fu_sum32(cx3_fw_buf, cx3_fw_size);

	/* M12 fw file size should be less than 3MB */
	m12_fw_buf = g_bytes_get_data(m12_fw, &m12_fw_size);
	if (m12_fw_size > 3 * 1024 * 1024) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "m12 file size is invalid: 0x%x",
			    (guint)m12_fw_size);
		return FALSE;
	}
	/* calculate M12 firmware checksum */
	m12_checksum = fu_sum32(m12_fw_buf, m12_fw_size);

	/* check if the device supports safeisp */
	if (!fu_aver_safeisp_device_support(self, error))
		return FALSE;

	/* CX3 safeisp prepare */
	if (!fu_aver_safeisp_device_upload_prepare(self, ISP_CX3, cx3_fw_size, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* CX3 safeisp erase flash */
	if (!fu_aver_safeisp_device_erase_flash(self, ISP_CX3, 0x0, error))
		return FALSE;

	/* CX3 safeisp firmware upload */
	chunks = fu_chunk_array_new_from_bytes(cx3_fw,
					       FU_CHUNK_ADDR_OFFSET_NONE,
					       FU_CHUNK_PAGESZ_NONE,
					       512);
	if (!fu_aver_safeisp_device_upload(self,
					   chunks,
					   fu_progress_get_child(progress),
					   ISP_CX3,
					   error))
		return FALSE;
	fu_progress_step_done(progress);

	/* CX3 safeisp checksum */
	if (!fu_aver_safeisp_device_upload_checksum(self, ISP_CX3, cx3_checksum, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* M12 safeisp prepare */
	if (!fu_aver_safeisp_device_upload_prepare(self, ISP_M12, m12_fw_size, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* M12 safeisp erase flash */
	if (!fu_aver_safeisp_device_erase_flash(self, ISP_M12, 0x0, error))
		return FALSE;

	/* M12 safeisp firmware upload */
	chunks = fu_chunk_array_new_from_bytes(m12_fw,
					       FU_CHUNK_ADDR_OFFSET_NONE,
					       FU_CHUNK_PAGESZ_NONE,
					       512);
	if (!fu_aver_safeisp_device_upload(self,
					   chunks,
					   fu_progress_get_child(progress),
					   ISP_M12,
					   error))
		return FALSE;
	fu_progress_step_done(progress);

	/* M12 safeisp checksum */
	if (!fu_aver_safeisp_device_upload_checksum(self, ISP_M12, m12_checksum, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* update device */
	if (!fu_aver_safeisp_device_update(self, ((1 << ISP_CX3) | (1 << ISP_M12)), 0x0, error))
		return FALSE;

	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	/* success! */
	return TRUE;
}

static void
fu_aver_safeisp_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 68, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 31, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "reload");
}

static void
fu_aver_safeisp_device_init(FuAverSafeispDevice *self)
{
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_add_protocol(FU_DEVICE(self), "com.aver.safeisp");
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_AVER_HID_FIRMWARE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_AUTO_PAUSE_POLLING);
	fu_device_set_remove_delay(FU_DEVICE(self), 150000);
	fu_hid_device_add_flag(FU_HID_DEVICE(self), FU_HID_DEVICE_FLAG_RETRY_FAILURE);
	fu_hid_device_add_flag(FU_HID_DEVICE(self), FU_HID_DEVICE_FLAG_AUTODETECT_EPS);
}

static void
fu_aver_safeisp_device_class_init(FuAverSafeispDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_aver_safeisp_device_setup;
	device_class->write_firmware = fu_aver_safeisp_device_write_firmware;
	device_class->set_progress = fu_aver_safeisp_device_set_progress;
}
