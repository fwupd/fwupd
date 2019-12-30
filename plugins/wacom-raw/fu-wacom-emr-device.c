/*
 * Copyright (C) 2018-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>
#include <gio/gio.h>

#include "fu-chunk.h"
#include "fu-wacom-common.h"
#include "fu-wacom-emr-device.h"

struct _FuWacomEmrDevice {
	FuWacomDevice		 parent_instance;
};

G_DEFINE_TYPE (FuWacomEmrDevice, fu_wacom_emr_device, FU_TYPE_WACOM_DEVICE)

static gboolean
fu_wacom_emr_device_setup (FuDevice *device, GError **error)
{
	FuWacomEmrDevice *self = FU_WACOM_EMR_DEVICE (device);

	/* check MPU type */
	if (!fu_wacom_device_check_mpu (FU_WACOM_DEVICE (self), error))
		return FALSE;

	/* get firmware version */
	if (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		fu_device_set_version (device, "0.0", FWUPD_VERSION_FORMAT_PAIR);
	} else {
		guint16 fw_ver;
		guint8 data[19] = { 0x03, 0x0 }; /* 0x03 is an unknown ReportID */
		g_autofree gchar *version = NULL;
		if (!fu_wacom_device_get_feature (FU_WACOM_DEVICE (self),
						  data, sizeof(data), error))
			return FALSE;
		fw_ver = fu_common_read_uint16 (data + 11, G_LITTLE_ENDIAN);
		fu_device_remove_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		version = fu_common_version_from_uint16 (fw_ver, FWUPD_VERSION_FORMAT_PAIR);
		fu_device_set_version (device, version, FWUPD_VERSION_FORMAT_PAIR);
		fu_device_set_version_raw (device, fw_ver);
	}

	/* success */
	return TRUE;
}

static guint8
fu_wacom_emr_device_calc_checksum (guint8 init1, const guint8 *buf, guint8 bufsz)
{
	guint8 sum = 0;
	sum += init1;
	for (guint i = 0; i < bufsz; i++)
		sum += buf[i];
	return ~sum + 1;
}

static gboolean
fu_wacom_emr_device_w9013_erase_data (FuWacomEmrDevice *self, GError **error)
{
	FuWacomRawResponse rsp = { 0x00 };
	FuWacomRawRequest req = {
		.cmd = FU_WACOM_RAW_BL_CMD_ERASE_DATAMEM,
		.echo = FU_WACOM_RAW_ECHO_DEFAULT,
		0x00
	};
	guint8 *buf = (guint8 *) &req.addr;
	buf[0] = 0x00; /* erased block */
	buf[1] = fu_wacom_emr_device_calc_checksum (0x05 + 0x00 + 0x07 + 0x00,
						    (const guint8 *) &req, 4);
	if (!fu_wacom_device_cmd (FU_WACOM_DEVICE (self), &req, &rsp, 50,
				  FU_WACOM_DEVICE_CMD_FLAG_POLL_ON_WAITING, error)) {
		g_prefix_error (error, "failed to erase datamem: ");
		return FALSE;
	}
	g_usleep (50);
	return TRUE;
}

static gboolean
fu_wacom_emr_device_w9013_erase_code (FuWacomEmrDevice *self,
				      guint8 idx,
				      guint8 block_nr,
				      GError **error)
{
	FuWacomRawResponse rsp = { 0x00 };
	FuWacomRawRequest req = {
		.cmd = FU_WACOM_RAW_BL_CMD_ERASE_FLASH,
		.echo = idx,
		0x00
	};
	guint8 *buf = (guint8 *) &req.addr;
	buf[0] = block_nr;
	buf[1] = fu_wacom_emr_device_calc_checksum (0x05 + 0x00 + 0x07 + 0x00,
						    (const guint8 *) &req, 4);
	if (!fu_wacom_device_cmd (FU_WACOM_DEVICE (self), &req, &rsp, 50,
				  FU_WACOM_DEVICE_CMD_FLAG_POLL_ON_WAITING, error)) {
		g_prefix_error (error, "failed to erase codemem: ");
		return FALSE;
	}
	g_usleep (50);
	return TRUE;
}

static gboolean
fu_wacom_device_w9021_erase_all (FuWacomEmrDevice *self, GError **error)
{
	FuWacomRawRequest req = {
		.cmd = FU_WACOM_RAW_BL_CMD_ALL_ERASE,
		.echo = 0x01,
		.addr = 0x00,
	};
	FuWacomRawResponse rsp = { 0x00 };
	if (!fu_wacom_device_cmd (FU_WACOM_DEVICE (self), &req, &rsp,
				  2000 * 1000, /* this takes a long time */
				  FU_WACOM_DEVICE_CMD_FLAG_POLL_ON_WAITING, error)) {
		g_prefix_error (error, "failed to send eraseall command: ");
		return FALSE;
	}
	if (!fu_wacom_common_rc_set_error (&rsp, error)) {
		g_prefix_error (error, "failed to erase");
		return FALSE;
	}
	g_usleep (50);
	return TRUE;
}

static gboolean
fu_wacom_emr_device_write_block (FuWacomEmrDevice *self,
				 guint32 idx,
				 guint32 address,
				 const guint8 *data,
				 guint16 datasz,
				 GError **error)
{
	guint blocksz = fu_wacom_device_get_block_sz (FU_WACOM_DEVICE (self));
	FuWacomRawRequest req = {
		.cmd = FU_WACOM_RAW_BL_CMD_WRITE_FLASH,
		.echo = (guint8) idx + 1,
		.addr = GUINT32_TO_LE(address),
		.size8 = datasz / 8,
		.data = { 0x00 },
	};
	FuWacomRawResponse rsp = { 0x00 };

	/* check size */
	if (datasz > sizeof(req.data)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "data size 0x%x too large for packet",
			     datasz);
		return FALSE;
	}
	if (datasz != blocksz) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "block size 0x%x != 0x%x untested",
			     datasz, (guint) blocksz);
		return FALSE;
	}

	/* data */
	memcpy (&req.data, data, datasz);

	/* cmd and data checksums */
	req.data[blocksz + 0] = fu_wacom_emr_device_calc_checksum (0x05 + 0x00 + 0x4c + 0x00,
								   (const guint8 *) &req, 8);
	req.data[blocksz + 1] = fu_wacom_emr_device_calc_checksum (0x00, data, datasz);
	if (!fu_wacom_device_cmd (FU_WACOM_DEVICE (self), &req, &rsp, 50,
				  FU_WACOM_DEVICE_CMD_FLAG_NONE, error)) {
		g_prefix_error (error, "failed to write at 0x%x: ", address);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_wacom_emr_device_write_firmware (FuDevice *device, GPtrArray *chunks, GError **error)
{
	FuWacomEmrDevice *self = FU_WACOM_EMR_DEVICE (device);
	guint8 idx = 0;

	/* erase W9013 */
	if (fu_device_has_instance_id (device, "WacomEMR_W9013")) {
		fu_device_set_status (device, FWUPD_STATUS_DEVICE_ERASE);
		if (!fu_wacom_emr_device_w9013_erase_data (self, error))
			return FALSE;
		for (guint i = 127; i >= 8; i--) {
			if (!fu_wacom_emr_device_w9013_erase_code (self, idx++, i, error))
				return FALSE;
		}
	}

	/* erase W9021 */
	if (fu_device_has_instance_id (device, "WacomEMR_W9021")) {
		if (!fu_wacom_device_w9021_erase_all (self, error))
			return FALSE;
	}

	/* write */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		if (fu_wacom_common_block_is_empty (chk->data, chk->data_sz))
			continue;
		if (!fu_wacom_emr_device_write_block (self,
						      chk->idx,
						      chk->address,
						      chk->data,
						      chk->data_sz,
						      error))
			return FALSE;
		fu_device_set_progress_full (device, (gsize) i, (gsize) chunks->len);
	}

	fu_device_set_progress (device, 100);
	return TRUE;
}

static void
fu_wacom_emr_device_init (FuWacomEmrDevice *self)
{
	fu_device_set_name (FU_DEVICE (self), "Wacom EMR Device");
}

static void
fu_wacom_emr_device_class_init (FuWacomEmrDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuWacomDeviceClass *klass_wac_device = FU_WACOM_DEVICE_CLASS (klass);
	klass_device->setup = fu_wacom_emr_device_setup;
	klass_wac_device->write_firmware = fu_wacom_emr_device_write_firmware;
}

FuWacomEmrDevice *
fu_wacom_emr_device_new (FuUdevDevice *device)
{
	FuWacomEmrDevice *self = g_object_new (FU_TYPE_WACOM_EMR_DEVICE, NULL);
	fu_device_incorporate (FU_DEVICE (self), FU_DEVICE (device));
	return self;
}
