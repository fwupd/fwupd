/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
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
		fu_device_set_version(device, "0.0");
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
		fu_device_set_version_u32(device, fw_ver);
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
	FuWacomRawResponse rsp = {0x00};
	FuWacomRawRequest req = {.cmd = FU_WACOM_RAW_BL_CMD_ERASE_DATAMEM,
				 .echo = FU_WACOM_RAW_ECHO_DEFAULT,
				 0x00};
	guint8 *buf = (guint8 *)&req.addr;
	buf[0] = 0x00; /* erased block */
	buf[1] =
	    fu_wacom_emr_device_calc_checksum(0x05 + 0x00 + 0x07 + 0x00, (const guint8 *)&req, 4);
	if (!fu_wacom_device_cmd(FU_WACOM_DEVICE(self),
				 &req,
				 &rsp,
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
	FuWacomRawResponse rsp = {0x00};
	FuWacomRawRequest req = {.cmd = FU_WACOM_RAW_BL_CMD_ERASE_FLASH, .echo = idx, 0x00};
	guint8 *buf = (guint8 *)&req.addr;
	buf[0] = block_nr;
	buf[1] =
	    fu_wacom_emr_device_calc_checksum(0x05 + 0x00 + 0x07 + 0x00, (const guint8 *)&req, 4);
	if (!fu_wacom_device_cmd(FU_WACOM_DEVICE(self),
				 &req,
				 &rsp,
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
fu_wacom_device_w9021_erase_all(FuWacomEmrDevice *self, GError **error)
{
	FuWacomRawRequest req = {
	    .cmd = FU_WACOM_RAW_BL_CMD_ALL_ERASE,
	    .echo = 0x01,
	    .addr = 0x00,
	};
	FuWacomRawResponse rsp = {0x00};
	if (!fu_wacom_device_cmd(FU_WACOM_DEVICE(self),
				 &req,
				 &rsp,
				 2000, /* this takes a long time */
				 FU_WACOM_DEVICE_CMD_FLAG_POLL_ON_WAITING,
				 error)) {
		g_prefix_error(error, "failed to send eraseall command: ");
		return FALSE;
	}
	if (!fu_wacom_common_rc_set_error(&rsp, error)) {
		g_prefix_error(error, "failed to erase: ");
		return FALSE;
	}
	g_usleep(50);
	return TRUE;
}

static gboolean
fu_wacom_emr_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuWacomDevice *self = FU_WACOM_DEVICE(device);
	FuWacomRawRequest req = {.report_id = FU_WACOM_RAW_BL_REPORT_ID_SET,
				 .cmd = FU_WACOM_RAW_BL_CMD_ATTACH,
				 .echo = FU_WACOM_RAW_ECHO_DEFAULT,
				 0x00};

	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in runtime mode, skipping");
		return TRUE;
	}
	if (!fu_wacom_device_set_feature(self, (const guint8 *)&req, sizeof(req), error)) {
		g_prefix_error(error, "failed to switch to runtime mode: ");
		return FALSE;
	}

	fu_device_remove_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
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
	FuWacomRawRequest req = {
	    .cmd = FU_WACOM_RAW_BL_CMD_WRITE_FLASH,
	    .echo = (guint8)idx + 1,
	    .addr = GUINT32_TO_LE(address),
	    .size8 = datasz / 8,
	    .data = {0x00},
	};
	FuWacomRawResponse rsp = {0x00};

	/* check size */
	if (datasz > sizeof(req.data)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "data size 0x%x too large for packet",
			    (guint)datasz);
		return FALSE;
	}
	if (datasz != blocksz) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "block size 0x%x != 0x%x untested",
			    (guint)datasz,
			    (guint)blocksz);
		return FALSE;
	}

	/* data */
	memcpy(&req.data, data, datasz);

	/* cmd and data checksums */
	req.data_unused[0] =
	    fu_wacom_emr_device_calc_checksum(0x05 + 0x00 + 0x4c + 0x00, (const guint8 *)&req, 8);
	req.data_unused[1] = fu_wacom_emr_device_calc_checksum(0x00, data, datasz);
	if (!fu_wacom_device_cmd(FU_WACOM_DEVICE(self),
				 &req,
				 &rsp,
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
	if (fu_device_has_instance_id(device, "WacomEMR_W9013")) {
		if (!fu_wacom_emr_device_w9013_erase_data(self, error))
			return FALSE;
		for (guint i = 127; i >= 8; i--) {
			if (!fu_wacom_emr_device_w9013_erase_code(self, idx++, i, error))
				return FALSE;
		}
	}

	/* erase W9021 */
	if (fu_device_has_instance_id(device, "WacomEMR_W9021")) {
		if (!fu_wacom_device_w9021_erase_all(self, error))
			return FALSE;
	}
	fu_progress_step_done(progress);

	/* write */
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = fu_chunk_array_index(chunks, i);
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

static void
fu_wacom_emr_device_init(FuWacomEmrDevice *self)
{
	fu_device_set_name(FU_DEVICE(self), "Wacom EMR Device");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
}

static void
fu_wacom_emr_device_class_init(FuWacomEmrDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	FuWacomDeviceClass *klass_wac_device = FU_WACOM_DEVICE_CLASS(klass);
	klass_device->setup = fu_wacom_emr_device_setup;
	klass_device->attach = fu_wacom_emr_device_attach;
	klass_wac_device->write_firmware = fu_wacom_emr_device_write_firmware;
}
