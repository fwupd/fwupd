/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-wacom-aes-device.h"
#include "fu-wacom-common.h"

typedef struct __attribute__((packed)) {
	guint8 report_id;
	guint8 cmd;
	guint8 echo;
	guint32 addr;
	guint8 size8;
	guint8 data[128];
} FuWacomRawVerifyResponse;

struct _FuWacomAesDevice {
	FuWacomDevice parent_instance;
};

G_DEFINE_TYPE(FuWacomAesDevice, fu_wacom_aes_device, FU_TYPE_WACOM_DEVICE)

static gboolean
fu_wacom_aes_add_recovery_hwid(FuDevice *device, GError **error)
{
	FuWacomRawRequest cmd = {
	    .report_id = FU_WACOM_RAW_BL_REPORT_ID_SET,
	    .cmd = FU_WACOM_RAW_BL_CMD_VERIFY_FLASH,
	    .echo = 0x01,
	    .addr = FU_WACOM_RAW_BL_START_ADDR,
	    .size8 = FU_WACOM_RAW_BL_BYTES_CHECK / 8,
	};
	FuWacomRawVerifyResponse rsp = {.report_id = FU_WACOM_RAW_BL_REPORT_ID_GET,
					.size8 = 0x00,
					.data = {0x00}};
	guint16 pid;

	if (!fu_wacom_device_set_feature(FU_WACOM_DEVICE(device),
					 (guint8 *)&cmd,
					 sizeof(cmd),
					 error)) {
		g_prefix_error(error, "failed to send: ");
		return FALSE;
	}
	if (!fu_wacom_device_get_feature(FU_WACOM_DEVICE(device),
					 (guint8 *)&rsp,
					 sizeof(rsp),
					 error)) {
		g_prefix_error(error, "failed to receive: ");
		return FALSE;
	}
	if (rsp.size8 != cmd.size8) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "firmware does not support this feature");
		return FALSE;
	}

	pid = (rsp.data[7] << 8) + (rsp.data[6]);
	if ((pid == 0xFFFF) || (pid == 0x0000)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "invalid recovery product ID %04x",
			    pid);
		return FALSE;
	}

	/* add recovery IDs */
	fu_device_add_instance_u16(device, "VEN", 0x2D1F);
	fu_device_add_instance_u16(device, "DEV", pid);
	if (!fu_device_build_instance_id(device, error, "HIDRAW", "VID", "PID", NULL))
		return FALSE;
	fu_device_add_instance_u16(device, "VEN", 0x056A);
	return fu_device_build_instance_id(device, error, "HIDRAW", "VID", "PID", NULL);
}

static gboolean
fu_wacom_aes_query_operation_mode(FuWacomAesDevice *self, GError **error)
{
	guint8 buf[FU_WACOM_RAW_FW_REPORT_SZ] = {
	    FU_WACOM_RAW_FW_REPORT_ID,
	    FU_WACOM_RAW_FW_CMD_QUERY_MODE,
	};

	/* 0x00=runtime, 0x02=bootloader */
	if (!fu_wacom_device_get_feature(FU_WACOM_DEVICE(self), buf, sizeof(buf), error))
		return FALSE;
	if (buf[1] == 0x00) {
		fu_device_remove_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		return TRUE;
	}
	if (buf[1] == 0x02) {
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		return TRUE;
	}

	/* unsupported */
	g_set_error(error,
		    G_IO_ERROR,
		    G_IO_ERROR_FAILED,
		    "Failed to query operation mode, got 0x%x",
		    buf[1]);
	return FALSE;
}

static gboolean
fu_wacom_aes_device_setup(FuDevice *device, GError **error)
{
	FuWacomAesDevice *self = FU_WACOM_AES_DEVICE(device);
	g_autoptr(GError) error_local = NULL;

	/* find out if in bootloader mode already */
	if (!fu_wacom_aes_query_operation_mode(self, error))
		return FALSE;

	/* get firmware version */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		fu_device_set_version(device, "0.0");
		/* get the recovery PID if supported */
		if (!fu_wacom_aes_add_recovery_hwid(device, &error_local))
			g_debug("failed to get HwID: %s", error_local->message);
	} else {
		guint16 fw_ver;
		guint8 data[FU_WACOM_RAW_STATUS_REPORT_SZ] = {FU_WACOM_RAW_STATUS_REPORT_ID, 0x0};
		g_autofree gchar *version = NULL;

		if (!fu_wacom_device_get_feature(FU_WACOM_DEVICE(self), data, sizeof(data), error))
			return FALSE;
		if (!fu_memread_uint16_safe(data,
					    sizeof(data),
					    11,
					    &fw_ver,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
		version = g_strdup_printf("%04x.%02x", fw_ver, data[13]);
		fu_device_set_version(device, version);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_wacom_aes_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuWacomDevice *self = FU_WACOM_DEVICE(device);
	FuWacomRawRequest req = {.report_id = FU_WACOM_RAW_BL_REPORT_ID_TYPE,
				 .cmd = FU_WACOM_RAW_BL_TYPE_FINALIZER,
				 0x00};

	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in runtime mode, skipping");
		return TRUE;
	}
	if (!fu_wacom_device_set_feature(self, (const guint8 *)&req, sizeof(req), error)) {
		g_prefix_error(error, "failed to finalize the device: ");
		return FALSE;
	}

	/* wait for device back to runtime mode */
	fu_device_sleep(device, 500); /* ms */
	fu_device_remove_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	return TRUE;
}

static gboolean
fu_wacom_aes_device_erase_all(FuWacomAesDevice *self, FuProgress *progress, GError **error)
{
	FuWacomRawRequest req = {.cmd = FU_WACOM_RAW_BL_CMD_ALL_ERASE,
				 .echo = FU_WACOM_RAW_ECHO_DEFAULT,
				 0x00};
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
	fu_device_sleep_full(FU_DEVICE(self), 2000, progress);
	return TRUE;
}

static gboolean
fu_wacom_aes_device_write_block(FuWacomAesDevice *self,
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
	if (datasz != blocksz) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "block size 0x%x != 0x%x untested",
			    (guint)datasz,
			    (guint)blocksz);
		return FALSE;
	}
	if (!fu_memcpy_safe((guint8 *)&req.data,
			    sizeof(req.data),
			    0x0, /* dst */
			    data,
			    datasz,
			    0x0, /* src */
			    datasz,
			    error))
		return FALSE;

	/* write */
	if (!fu_wacom_device_cmd(FU_WACOM_DEVICE(self),
				 &req,
				 &rsp,
				 1, /* ms */
				 FU_WACOM_DEVICE_CMD_FLAG_NONE,
				 error)) {
		g_prefix_error(error, "failed to write block %u: ", idx);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_wacom_aes_device_write_firmware(FuDevice *device,
				   FuChunkArray *chunks,
				   FuProgress *progress,
				   GError **error)
{
	FuWacomAesDevice *self = FU_WACOM_AES_DEVICE(device);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 20, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 80, NULL);

	/* erase */
	if (!fu_wacom_aes_device_erase_all(self, progress, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write */
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = fu_chunk_array_index(chunks, i);
		if (!fu_wacom_aes_device_write_block(self,
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
fu_wacom_aes_device_init(FuWacomAesDevice *self)
{
	fu_device_set_name(FU_DEVICE(self), "Wacom AES Device");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
}

static void
fu_wacom_aes_device_class_init(FuWacomAesDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	FuWacomDeviceClass *klass_wac_device = FU_WACOM_DEVICE_CLASS(klass);
	klass_device->setup = fu_wacom_aes_device_setup;
	klass_device->attach = fu_wacom_aes_device_attach;
	klass_wac_device->write_firmware = fu_wacom_aes_device_write_firmware;
}
