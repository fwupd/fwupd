/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-wacom-common.h"
#include "fu-wacom-emr-device.h"

struct _FuWacomEmrDevice {
	FuWacomDevice parent_instance;
};

G_DEFINE_TYPE(FuWacomEmrDevice, fu_wacom_emr_device, FU_TYPE_WACOM_DEVICE)

static gboolean
fu_wacom_emr_device_setup(FuDevice *device, GError **error)
{
	FuWacomEmrDevice *self = FU_WACOM_EMR_DEVICE(device);

	/* check MPU type */
	if (!fu_wacom_device_check_mpu(FU_WACOM_DEVICE(self), error))
		return FALSE;

	/* get firmware version */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		fu_device_set_version_raw(device, 0);
	} else {
		guint16 fw_ver;
		guint8 data[19] = {0x03, 0x0}; /* 0x03 is an unknown ReportID */
		if (!fu_wacom_device_get_feature(FU_WACOM_DEVICE(self), data, sizeof(data), error))
			return FALSE;
		if (!fu_memread_uint16_safe(data,
					    sizeof(data),
					    11,
					    &fw_ver,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
		fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		fu_device_set_version_raw(device, fw_ver);
	}

	/* success */
	return TRUE;
}

static guint8
fu_wacom_emr_device_calc_checksum(guint8 init1, const guint8 *buf, gsize bufsz)
{
	return init1 + ~(fu_sum8(buf, bufsz)) + 1;
}

static gboolean
fu_wacom_emr_device_w9013_erase_data(FuWacomEmrDevice *self, GError **error)
{
	g_autoptr(FuStructWacomRawRequest) st_req = fu_struct_wacom_raw_request_new();
	guint8 *buf = st_req->data + FU_STRUCT_WACOM_RAW_REQUEST_OFFSET_ADDR;

	fu_struct_wacom_raw_request_set_report_id(st_req, FU_WACOM_RAW_BL_REPORT_ID_SET);
	fu_struct_wacom_raw_request_set_cmd(st_req, FU_WACOM_RAW_BL_CMD_ERASE_DATAMEM);
	fu_struct_wacom_raw_request_set_echo(st_req,
					     fu_wacom_device_get_echo_next(FU_WACOM_DEVICE(self)));
	buf[0] = 0x00; /* erased block */
	buf[1] = fu_wacom_emr_device_calc_checksum(0x05 + 0x00 + 0x07 + 0x00,
						   (const guint8 *)&st_req,
						   4);
	if (!fu_wacom_device_cmd(FU_WACOM_DEVICE(self),
				 st_req,
				 NULL,
				 1, /* ms */
				 FU_WACOM_DEVICE_CMD_FLAG_POLL_ON_WAITING,
				 error)) {
		g_prefix_error(error, "failed to erase datamem: ");
		return FALSE;
	}
	g_usleep(50);
	return TRUE;
}

static gboolean
fu_wacom_emr_device_w9013_erase_code(FuWacomEmrDevice *self,
				     guint8 idx,
				     guint8 block_nr,
				     GError **error)
{
	g_autoptr(FuStructWacomRawRequest) st_req = fu_struct_wacom_raw_request_new();
	guint8 *buf = st_req->data + FU_STRUCT_WACOM_RAW_REQUEST_OFFSET_ADDR;

	fu_struct_wacom_raw_request_set_report_id(st_req, FU_WACOM_RAW_BL_REPORT_ID_SET);
	fu_struct_wacom_raw_request_set_cmd(st_req, FU_WACOM_RAW_BL_CMD_ERASE_FLASH);
	fu_struct_wacom_raw_request_set_echo(st_req, idx);
	buf[0] = block_nr;
	buf[1] = fu_wacom_emr_device_calc_checksum(0x05 + 0x00 + 0x07 + 0x00,
						   (const guint8 *)&st_req,
						   4);
	if (!fu_wacom_device_cmd(FU_WACOM_DEVICE(self),
				 st_req,
				 NULL,
				 1, /* ms */
				 FU_WACOM_DEVICE_CMD_FLAG_POLL_ON_WAITING,
				 error)) {
		g_prefix_error(error, "failed to erase codemem: ");
		return FALSE;
	}
	g_usleep(50);
	return TRUE;
}

static gboolean
fu_wacom_emr_device_w9021_erase_all(FuWacomEmrDevice *self, GError **error)
{
	g_autoptr(FuStructWacomRawRequest) st_req = fu_struct_wacom_raw_request_new();

	fu_struct_wacom_raw_request_set_report_id(st_req, FU_WACOM_RAW_BL_REPORT_ID_SET);
	fu_struct_wacom_raw_request_set_cmd(st_req, FU_WACOM_RAW_BL_CMD_ALL_ERASE);
	fu_struct_wacom_raw_request_set_echo(st_req, 0x01);
	if (!fu_wacom_device_cmd(FU_WACOM_DEVICE(self),
				 st_req,
				 NULL,
				 2000, /* this takes a long time */
				 FU_WACOM_DEVICE_CMD_FLAG_POLL_ON_WAITING,
				 error)) {
		g_prefix_error(error, "failed to send eraseall command: ");
		return FALSE;
	}
	g_usleep(50);
	return TRUE;
}

static gboolean
fu_wacom_emr_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuWacomDevice *self = FU_WACOM_DEVICE(device);
	g_autoptr(FuStructWacomRawRequest) st_req = fu_struct_wacom_raw_request_new();

	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in runtime mode, skipping");
		return TRUE;
	}

	fu_struct_wacom_raw_request_set_report_id(st_req, FU_WACOM_RAW_BL_REPORT_ID_SET);
	fu_struct_wacom_raw_request_set_cmd(st_req, FU_WACOM_RAW_BL_CMD_ATTACH);
	fu_struct_wacom_raw_request_set_echo(st_req,
					     fu_wacom_device_get_echo_next(FU_WACOM_DEVICE(self)));
	if (!fu_wacom_device_set_feature(self, st_req->data, st_req->len, error)) {
		g_prefix_error(error, "failed to switch to runtime mode: ");
		return FALSE;
	}

	/* does the device have to replug to bootloader mode */
	if (fu_device_has_private_flag(device, FU_WACOM_RAW_DEVICE_FLAG_REQUIRES_WAIT_FOR_REPLUG)) {
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	} else {
		fu_device_remove_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_wacom_emr_device_write_block(FuWacomEmrDevice *self,
				guint32 idx,
				guint32 address,
				const guint8 *data,
				gsize datasz,
				GError **error)
{
	gsize blocksz = fu_wacom_device_get_block_sz(FU_WACOM_DEVICE(self));
	g_autoptr(FuStructWacomRawRequest) st_req = fu_struct_wacom_raw_request_new();
	guint8 *data_unused = st_req->data + FU_STRUCT_WACOM_RAW_REQUEST_OFFSET_DATA_UNUSED;

	/* check size */
	if (datasz > FU_STRUCT_WACOM_RAW_REQUEST_SIZE_DATA) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "data size 0x%x too large for packet",
			    (guint)datasz);
		return FALSE;
	}
	if (datasz != blocksz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "block size 0x%x != 0x%x untested",
			    (guint)datasz,
			    (guint)blocksz);
		return FALSE;
	}

	/* data */
	fu_struct_wacom_raw_request_set_report_id(st_req, FU_WACOM_RAW_BL_REPORT_ID_SET);
	fu_struct_wacom_raw_request_set_cmd(st_req, FU_WACOM_RAW_BL_CMD_WRITE_FLASH);
	fu_struct_wacom_raw_request_set_echo(st_req, (guint8)idx + 1);
	fu_struct_wacom_raw_request_set_addr(st_req, address);
	fu_struct_wacom_raw_request_set_size8(st_req, datasz / 8);
	if (!fu_struct_wacom_raw_request_set_data(st_req, data, datasz, error))
		return FALSE;

	/* cmd and data checksums */
	data_unused[0] =
	    fu_wacom_emr_device_calc_checksum(0x05 + 0x00 + 0x4c + 0x00, st_req->data, 8);
	data_unused[1] = fu_wacom_emr_device_calc_checksum(0x00, data, datasz);
	if (!fu_wacom_device_cmd(FU_WACOM_DEVICE(self),
				 st_req,
				 NULL,
				 1,
				 FU_WACOM_DEVICE_CMD_FLAG_NONE,
				 error)) {
		g_prefix_error(error, "failed to write at 0x%x: ", address);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_wacom_emr_device_write_firmware(FuDevice *device,
				   FuChunkArray *chunks,
				   FuProgress *progress,
				   GError **error)
{
	FuWacomEmrDevice *self = FU_WACOM_EMR_DEVICE(device);
	guint8 idx = 0;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 10, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 90, NULL);

	/* erase W9013 */
	if (fu_device_has_instance_id(device, "WacomEMR_W9013", FU_DEVICE_INSTANCE_FLAG_QUIRKS)) {
		if (!fu_wacom_emr_device_w9013_erase_data(self, error))
			return FALSE;
		for (guint i = 127; i >= 8; i--) {
			if (!fu_wacom_emr_device_w9013_erase_code(self, idx++, i, error))
				return FALSE;
		}
	}

	/* erase W9021 */
	if (fu_device_has_instance_id(device, "WacomEMR_W9021", FU_DEVICE_INSTANCE_FLAG_QUIRKS)) {
		if (!fu_wacom_emr_device_w9021_erase_all(self, error))
			return FALSE;
	}
	fu_progress_step_done(progress);

	/* write */
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (fu_wacom_common_block_is_empty(fu_chunk_get_data(chk),
						   fu_chunk_get_data_sz(chk)))
			continue;
		if (!fu_wacom_emr_device_write_block(self,
						     fu_chunk_get_idx(chk),
						     fu_chunk_get_address(chk),
						     fu_chunk_get_data(chk),
						     fu_chunk_get_data_sz(chk),
						     error))
			return FALSE;
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						(gsize)i + 1,
						(gsize)fu_chunk_array_length(chunks));
	}
	fu_progress_step_done(progress);

	return TRUE;
}

static gchar *
fu_wacom_emr_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint32(version_raw, fu_device_get_version_format(device));
}

static void
fu_wacom_emr_device_init(FuWacomEmrDevice *self)
{
	fu_device_set_name(FU_DEVICE(self), "Wacom EMR Device");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
}

static void
fu_wacom_emr_device_class_init(FuWacomEmrDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	FuWacomDeviceClass *wac_device_class = FU_WACOM_DEVICE_CLASS(klass);
	device_class->setup = fu_wacom_emr_device_setup;
	device_class->attach = fu_wacom_emr_device_attach;
	device_class->convert_version = fu_wacom_emr_device_convert_version;
	wac_device_class->write_firmware = fu_wacom_emr_device_write_firmware;
}
