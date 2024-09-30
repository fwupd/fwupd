/*
 * Copyright 2020 Jimmy Yu <Jimmy_yu@pixart.com>
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "config.h"

#include "fu-pxi-firmware.h"
#include "fu-pxi-receiver-device.h"
#include "fu-pxi-struct.h"
#include "fu-pxi-wireless-device.h"

struct _FuPxiReceiverDevice {
	FuHidrawDevice parent_instance;
	struct ota_fw_state fwstate;
	guint8 sn;
};

G_DEFINE_TYPE(FuPxiReceiverDevice, fu_pxi_receiver_device, FU_TYPE_HIDRAW_DEVICE)

static void
fu_pxi_receiver_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuPxiReceiverDevice *self = FU_PXI_RECEIVER_DEVICE(device);
	fu_pxi_ota_fw_state_to_string(&self->fwstate, idt, str);
}

static FuFirmware *
fu_pxi_receiver_device_prepare_firmware(FuDevice *device,
					GInputStream *stream,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_pxi_firmware_new();

	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;

	if (fu_device_has_private_flag(device, FU_PXI_DEVICE_FLAG_IS_HPAC) &&
	    fu_pxi_firmware_is_hpac(FU_PXI_FIRMWARE(firmware))) {
		guint32 hpac_fw_size = 0;
		g_autoptr(GInputStream) stream_new = NULL;

		if (!fu_input_stream_read_u32(stream, 9, &hpac_fw_size, G_LITTLE_ENDIAN, error))
			return NULL;
		stream_new = fu_partial_input_stream_new(stream, 9, hpac_fw_size + 264, error);
		if (stream_new == NULL)
			return NULL;
		if (!fu_firmware_set_stream(firmware, stream_new, error))
			return NULL;
	} else if (fu_device_has_private_flag(device, FU_PXI_DEVICE_FLAG_IS_HPAC) !=
		   fu_pxi_firmware_is_hpac(FU_PXI_FIRMWARE(firmware))) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "The firmware is incompatible with the device");
		return NULL;
	}

	return g_steal_pointer(&firmware);
}

static gboolean
fu_pxi_receiver_device_fw_ota_init_new(FuPxiReceiverDevice *device, gsize bufsz, GError **error)
{
	guint8 fw_version[10] = {0x0};
	g_autoptr(GByteArray) receiver_device_cmd = g_byte_array_new();
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new();
	FuPxiReceiverDevice *self = FU_PXI_RECEIVER_DEVICE(device);

	fu_byte_array_append_uint8(ota_cmd, 0X06); /* ota init new command length */
	fu_byte_array_append_uint8(ota_cmd,
				   FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW); /* ota init new op code */
	fu_byte_array_append_uint32(ota_cmd, bufsz, G_LITTLE_ENDIAN);  /* fw size */
	fu_byte_array_append_uint8(ota_cmd, 0x0);		       /* ota setting */
	g_byte_array_append(ota_cmd, fw_version, sizeof(fw_version));  /* ota version */

	self->sn++;
	if (!fu_pxi_composite_receiver_cmd(FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW,
					   self->sn,
					   FU_PXI_WIRELESS_DEVICE_TARGET_RECEIVER,
					   receiver_device_cmd,
					   ota_cmd,
					   error))
		return FALSE;

	return fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					    receiver_device_cmd->data,
					    receiver_device_cmd->len,
					    FU_UDEV_DEVICE_IOCTL_FLAG_NONE,
					    error);
}

static gboolean
fu_pxi_receiver_device_fw_ota_ini_new_check(FuPxiReceiverDevice *device, GError **error)
{
	FuPxiReceiverDevice *self = FU_PXI_RECEIVER_DEVICE(device);
	guint8 buf[FU_PXI_RECEIVER_DEVICE_OTA_BUF_SZ] = {0x0};
	g_autoptr(GByteArray) receiver_device_cmd = g_byte_array_new();
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new();

	/* ota command */
	fu_byte_array_append_uint8(ota_cmd, 0x1); /* ota command length */
	fu_byte_array_append_uint8(ota_cmd,
				   FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW_CHECK); /* ota command */
	self->sn++;

	/* get pixart wireless module ota command */
	if (!fu_pxi_composite_receiver_cmd(FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW_CHECK,
					   self->sn,
					   FU_PXI_WIRELESS_DEVICE_TARGET_RECEIVER,
					   receiver_device_cmd,
					   ota_cmd,
					   error))
		return FALSE;

	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  receiver_device_cmd->data,
					  receiver_device_cmd->len,
					  FU_UDEV_DEVICE_IOCTL_FLAG_NONE,
					  error))
		return FALSE;

	/* delay for wireless module device read command */
	fu_device_sleep(FU_DEVICE(device), 5); /* ms */
	buf[0] = PXI_HID_WIRELESS_DEV_OTA_REPORT_ID;
	if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(self),
					  buf,
					  sizeof(buf),
					  FU_UDEV_DEVICE_IOCTL_FLAG_NONE,
					  error))
		return FALSE;

	/* shared state */
	return fu_pxi_ota_fw_state_parse(&self->fwstate, buf, sizeof(buf), 0x09, error);
}

static gboolean
fu_pxi_receiver_device_get_cmd_response(FuPxiReceiverDevice *self,
					guint8 *buf,
					guint bufsz,
					GError **error)
{
	guint16 retry = 0;
	while (1) {
		guint8 sn = 0x0;
		memset(buf, 0, bufsz);
		buf[0] = PXI_HID_WIRELESS_DEV_OTA_REPORT_ID;

		fu_device_sleep(FU_DEVICE(self), 5); /* ms */

		if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(self),
						  buf,
						  bufsz,
						  FU_UDEV_DEVICE_IOCTL_FLAG_NONE,
						  error))
			return FALSE;

		if (!fu_memread_uint8_safe(buf, bufsz, 0x4, &sn, error))
			return FALSE;

		if (self->sn != sn)
			retry++;
		else
			break;

		if (retry == FU_PXI_WIRELESS_DEVICE_RETRY_MAXIMUM) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "reach retry maximum, hid sn fail, "
				    "got 0x%04x, expected 0x%04x",
				    sn,
				    self->sn);
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_pxi_receiver_device_check_crc(FuDevice *device, guint16 checksum, GError **error)
{
	FuPxiReceiverDevice *self = FU_PXI_RECEIVER_DEVICE(device);
	guint8 buf[FU_PXI_RECEIVER_DEVICE_OTA_BUF_SZ] = {0x0};
	guint8 status = 0x0;
	g_autoptr(GByteArray) receiver_device_cmd = g_byte_array_new();
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new();

	/* ota check crc command */
	fu_byte_array_append_uint8(ota_cmd, 0x3); /* ota command length */
	fu_byte_array_append_uint8(ota_cmd, FU_PXI_DEVICE_CMD_FW_OTA_CHECK_CRC); /* ota command */
	fu_byte_array_append_uint16(ota_cmd, checksum, G_LITTLE_ENDIAN);	 /* checkesum */

	/* increase the serial number */
	self->sn++;

	/* get pixart wireless module for ota check crc command */
	if (!fu_pxi_composite_receiver_cmd(FU_PXI_DEVICE_CMD_FW_OTA_CHECK_CRC,
					   self->sn,
					   FU_PXI_WIRELESS_DEVICE_TARGET_RECEIVER,
					   receiver_device_cmd,
					   ota_cmd,
					   error))
		return FALSE;

	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  receiver_device_cmd->data,
					  receiver_device_cmd->len,
					  FU_UDEV_DEVICE_IOCTL_FLAG_NONE,
					  error))
		return FALSE;

	if (!fu_pxi_receiver_device_get_cmd_response(self, buf, sizeof(buf), error))
		return FALSE;

	if (!fu_memread_uint8_safe(buf, sizeof(buf), 0x5, &status, error))
		return FALSE;

	if (status == FU_PXI_WIRELESS_MODULE_OTA_RSP_CODE_ERROR) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "checksum error: expected 0x%04x",
			    checksum);
		return FALSE;
	}
	/* success */
	return TRUE;
}

static gboolean
fu_pxi_receiver_device_fw_object_create(FuDevice *device, FuChunk *chk, GError **error)
{
	FuPxiReceiverDevice *self = FU_PXI_RECEIVER_DEVICE(device);
	guint8 buf[FU_PXI_RECEIVER_DEVICE_OTA_BUF_SZ] = {0x0};
	guint8 status = 0x0;
	g_autoptr(GByteArray) receiver_device_cmd = g_byte_array_new();
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new();

	/* ota object create command */
	fu_byte_array_append_uint8(ota_cmd, 0x9); /* ota command length */
	fu_byte_array_append_uint8(ota_cmd, FU_PXI_DEVICE_CMD_FW_OBJECT_CREATE); /* ota command */
	fu_byte_array_append_uint32(ota_cmd, fu_chunk_get_address(chk), G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(ota_cmd, fu_chunk_get_data_sz(chk), G_LITTLE_ENDIAN);

	/* increase the serial number */
	self->sn++;

	/* get pixart wireless module for ota object create command */
	if (!fu_pxi_composite_receiver_cmd(FU_PXI_DEVICE_CMD_FW_OBJECT_CREATE,
					   self->sn,
					   FU_PXI_WIRELESS_DEVICE_TARGET_RECEIVER,
					   receiver_device_cmd,
					   ota_cmd,
					   error))
		return FALSE;

	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  receiver_device_cmd->data,
					  receiver_device_cmd->len,
					  FU_UDEV_DEVICE_IOCTL_FLAG_NONE,
					  error))
		return FALSE;

	if (!fu_pxi_receiver_device_get_cmd_response(self, buf, sizeof(buf), error))
		return FALSE;

	if (!fu_memread_uint8_safe(buf, sizeof(buf), 0x5, &status, error))
		return FALSE;

	if (status != FU_PXI_WIRELESS_MODULE_OTA_RSP_CODE_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "cmd rsp check fail: %s [0x%02x]",
			    fu_pxi_wireless_module_ota_rsp_code_to_string(status),
			    status);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_receiver_device_write_payload(FuDevice *device, FuChunk *chk, GError **error)
{
	FuPxiReceiverDevice *self = FU_PXI_RECEIVER_DEVICE(device);
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new();
	g_autoptr(GByteArray) receiver_device_cmd = g_byte_array_new();

	/* ota write payload command */
	fu_byte_array_append_uint8(ota_cmd, fu_chunk_get_data_sz(chk)); /* ota command length */
	g_byte_array_append(ota_cmd,
			    fu_chunk_get_data(chk),
			    fu_chunk_get_data_sz(chk)); /* payload content */

	/* increase the serial number */
	self->sn++;

	/* get pixart wireless module for writes payload command */
	if (!fu_pxi_composite_receiver_cmd(FU_PXI_DEVICE_CMD_FW_OTA_PAYLOAD_CONTENT,
					   self->sn,
					   FU_PXI_WIRELESS_DEVICE_TARGET_RECEIVER,
					   receiver_device_cmd,
					   ota_cmd,
					   error))
		return FALSE;

	return fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					    receiver_device_cmd->data,
					    receiver_device_cmd->len,
					    FU_UDEV_DEVICE_IOCTL_FLAG_NONE,
					    error);
}

static gboolean
fu_pxi_receiver_device_write_chunk(FuDevice *device, FuChunk *chk, GError **error)
{
	FuPxiReceiverDevice *self = FU_PXI_RECEIVER_DEVICE(device);
	guint32 prn = 0;
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GBytes) chk_bytes = fu_chunk_get_bytes(chk);

	/* send create fw object command */
	if (!fu_pxi_receiver_device_fw_object_create(device, chk, error))
		return FALSE;

	/* write payload */
	chunks = fu_chunk_array_new_from_bytes(chk_bytes,
					       fu_chunk_get_address(chk),
					       self->fwstate.mtu_size);

	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk2 = NULL;

		/* prepare chunk */
		chk2 = fu_chunk_array_index(chunks, i, error);
		if (chk2 == NULL)
			return FALSE;

		/* calculate checksum of each payload packet */
		self->fwstate.checksum +=
		    fu_sum16(fu_chunk_get_data(chk2), fu_chunk_get_data_sz(chk2));
		if (!fu_pxi_receiver_device_write_payload(device, chk2, error))
			return FALSE;
		prn++;
		/* check crc at fw when PRN over threshold write or
		 * offset reach max object sz or write offset reach fw length */
		if (prn >= self->fwstate.prn_threshold || i == fu_chunk_array_length(chunks) - 1) {
			if (!fu_pxi_receiver_device_check_crc(device,
							      self->fwstate.checksum,
							      error))
				return FALSE;
			prn = 0;
		}
	}
	/* success */
	return TRUE;
}

static gboolean
fu_pxi_receiver_device_fw_upgrade(FuDevice *device,
				  FuFirmware *firmware,
				  FuProgress *progress,
				  GError **error)
{
	FuPxiReceiverDevice *self = FU_PXI_RECEIVER_DEVICE(device);
	const gchar *version;
	guint8 fw_version[5] = {0x0};
	guint8 res[FU_PXI_RECEIVER_DEVICE_OTA_BUF_SZ] = {0x0};
	guint8 result = 0x0;
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new();
	g_autoptr(GByteArray) receiver_device_cmd = g_byte_array_new();
	g_autoptr(GBytes) fw = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 5, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 95, NULL);

	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* ota fw upgrade command */
	fu_byte_array_append_uint8(ota_cmd, 0x0c); /* ota fw upgrade command length */
	fu_byte_array_append_uint8(
	    ota_cmd,
	    FU_PXI_DEVICE_CMD_FW_UPGRADE); /* ota fw upgrade command opccode */
	fu_byte_array_append_uint32(ota_cmd,
				    g_bytes_get_size(fw),
				    G_LITTLE_ENDIAN); /* ota fw upgrade command fw size */
	fu_byte_array_append_uint16(ota_cmd,
				    fu_sum16_bytes(fw),
				    G_LITTLE_ENDIAN); /* ota fw upgrade command checksum */

	if (!fu_device_has_private_flag(FU_DEVICE(self), FU_PXI_DEVICE_FLAG_IS_HPAC)) {
		version = fu_firmware_get_version(firmware);
		if (!fu_memcpy_safe(fw_version,
				    sizeof(fw_version),
				    0x0, /* dst */
				    (guint8 *)version,
				    strlen(version),
				    0x0, /* src */
				    sizeof(fw_version),
				    error))
			return FALSE;
	}

	g_byte_array_append(ota_cmd, fw_version, sizeof(fw_version));
	fu_dump_raw(G_LOG_DOMAIN, "ota_cmd ", ota_cmd->data, ota_cmd->len);
	self->sn++;

	/* get pixart wireless module ota command */
	if (!fu_pxi_composite_receiver_cmd(FU_PXI_DEVICE_CMD_FW_UPGRADE,
					   self->sn,
					   FU_PXI_WIRELESS_DEVICE_TARGET_RECEIVER,
					   receiver_device_cmd,
					   ota_cmd,
					   error))
		return FALSE;
	fu_progress_step_done(progress);

	/* send ota fw upgrade command */
	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  receiver_device_cmd->data,
					  receiver_device_cmd->len,
					  FU_UDEV_DEVICE_IOCTL_FLAG_NONE,
					  error))
		return FALSE;

	/* delay for wireless module device read command */
	fu_device_sleep(device, 5); /* ms */

	if (!fu_pxi_receiver_device_get_cmd_response(self, res, sizeof(res), error))
		return FALSE;

	if (!fu_memread_uint8_safe(res, sizeof(res), 0x5, &result, error))
		return FALSE;
	if (result != FU_PXI_WIRELESS_MODULE_OTA_RSP_CODE_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "cmd rsp check fail: %s [0x%02x]",
			    fu_pxi_wireless_module_ota_rsp_code_to_string(result),
			    result);
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_receiver_device_reset(FuDevice *device, GError **error)
{
	FuPxiReceiverDevice *self = FU_PXI_RECEIVER_DEVICE(device);
	g_autoptr(GByteArray) receiver_device_cmd = g_byte_array_new();
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new();

	/* ota mcu reset command */
	fu_byte_array_append_uint8(ota_cmd, 0x1); /* ota mcu reset command */
	fu_byte_array_append_uint8(
	    ota_cmd,
	    FU_PXI_DEVICE_CMD_FW_MCU_RESET);		/* ota mcu reset command op code */
	fu_byte_array_append_uint8(ota_cmd, OTA_RESET); /* ota mcu reset command reason */

	self->sn++;
	/* get pixart wireless module ota command */
	if (!fu_pxi_composite_receiver_cmd(FU_PXI_DEVICE_CMD_FW_MCU_RESET,
					   self->sn,
					   FU_PXI_WIRELESS_DEVICE_TARGET_RECEIVER,
					   receiver_device_cmd,
					   ota_cmd,
					   error))
		return FALSE;

	/* send ota mcu reset command */
	return fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					    receiver_device_cmd->data,
					    receiver_device_cmd->len,
					    FU_UDEV_DEVICE_IOCTL_FLAG_NONE,
					    error);
}

static gboolean
fu_pxi_receiver_device_write_firmware(FuDevice *device,
				      FuFirmware *firmware,
				      FuProgress *progress,
				      FwupdInstallFlags flags,
				      GError **error)
{
	FuPxiReceiverDevice *self = FU_PXI_RECEIVER_DEVICE(device);
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 9, "ota-init");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 90, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, NULL);

	/* get the default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* send fw ota init command */
	if (!fu_pxi_receiver_device_fw_ota_init_new(self, g_bytes_get_size(fw), error))
		return FALSE;

	if (!fu_pxi_receiver_device_fw_ota_ini_new_check(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	chunks = fu_chunk_array_new_from_bytes(fw, 0x0, FU_PXI_DEVICE_OBJECT_SIZE_MAX);
	/* prepare write fw into device */
	self->fwstate.offset = 0;
	self->fwstate.checksum = 0;

	/* write fw into device */
	for (guint i = self->fwstate.offset; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_pxi_receiver_device_write_chunk(device, chk, error))
			return FALSE;
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						(gsize)i + self->fwstate.offset + 1,
						(gsize)fu_chunk_array_length(chunks));
	}
	fu_progress_step_done(progress);

	/* fw upgrade command */
	if (!fu_pxi_receiver_device_fw_upgrade(device,
					       firmware,
					       fu_progress_get_child(progress),
					       error))
		return FALSE;
	fu_progress_step_done(progress);

	/* delay for wireless module device read command */
	fu_device_sleep(device, 5); /* ms */

	/* send device reset command */
	if (!fu_pxi_receiver_device_reset(device, error))
		return FALSE;
	fu_progress_step_done(progress);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_receiver_device_get_peripheral_info(FuPxiReceiverDevice *self,
					   struct ota_fw_dev_model *model,
					   guint idx,
					   GError **error)
{
	guint8 buf[FU_PXI_RECEIVER_DEVICE_OTA_BUF_SZ] = {0x0};
	guint16 checksum = 0;
	guint16 hpac_ver = 0;
	g_autofree gchar *version_str = NULL;
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new();
	g_autoptr(GByteArray) receiver_device_cmd = g_byte_array_new();

	fu_byte_array_append_uint8(ota_cmd, 0x1); /* ota init new command length */
	fu_byte_array_append_uint8(ota_cmd, FU_PXI_DEVICE_CMD_FW_OTA_GET_MODEL);
	fu_byte_array_append_uint8(ota_cmd, idx);
	self->sn++;

	if (!fu_pxi_composite_receiver_cmd(FU_PXI_DEVICE_CMD_FW_OTA_GET_MODEL,
					   self->sn,
					   FU_PXI_WIRELESS_DEVICE_TARGET_RECEIVER,
					   receiver_device_cmd,
					   ota_cmd,
					   error))
		return FALSE;
	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  receiver_device_cmd->data,
					  receiver_device_cmd->len,
					  FU_UDEV_DEVICE_IOCTL_FLAG_NONE,
					  error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), 5); /* ms */
	buf[0] = PXI_HID_WIRELESS_DEV_OTA_REPORT_ID;

	if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(self),
					  buf,
					  sizeof(buf),
					  FU_UDEV_DEVICE_IOCTL_FLAG_NONE,
					  error))
		return FALSE;

	fu_dump_raw(G_LOG_DOMAIN, "model_info", buf, sizeof(buf));

	if (!fu_memread_uint8_safe(buf, sizeof(buf), 0x9, &model->status, error))
		return FALSE;

	if (!fu_memcpy_safe(model->name,
			    FU_PXI_DEVICE_MODEL_NAME_LEN,
			    0x0, /* dst */
			    buf,
			    FU_PXI_RECEIVER_DEVICE_OTA_BUF_SZ,
			    0xa, /* src */
			    FU_PXI_DEVICE_MODEL_NAME_LEN,
			    error))
		return FALSE;

	if (!fu_memread_uint8_safe(buf, sizeof(buf), 0x16, &model->type, error))
		return FALSE;
	if (!fu_memread_uint8_safe(buf, sizeof(buf), 0x17, &model->target, error))
		return FALSE;

	if (!fu_memcpy_safe(model->version,
			    5,
			    0x0, /* dst */
			    buf,
			    sizeof(buf),
			    0x18, /* src */
			    5,
			    error))
		return FALSE;

	if (!fu_memread_uint16_safe(buf, sizeof(buf), 0x1D, &checksum, G_LITTLE_ENDIAN, error))
		return FALSE;

	/* set current version and model name */
	model->checksum = checksum;
	g_debug("checksum %x", model->checksum);
	if (!fu_device_has_private_flag(FU_DEVICE(self), FU_PXI_DEVICE_FLAG_IS_HPAC)) {
		version_str = g_strndup((gchar *)model->version, 5);
	} else {
		if (!fu_memread_uint16_safe(model->version, 5, 3, &hpac_ver, G_BIG_ENDIAN, error))
			return FALSE;
		version_str = fu_pxi_hpac_version_info_parse(hpac_ver);
	}
	g_debug("version_str %s", version_str);

	return TRUE;
}

static gboolean
fu_pxi_receiver_device_get_peripheral_num(FuPxiReceiverDevice *device,
					  guint8 *num_of_models,
					  GError **error)
{
	FuPxiReceiverDevice *self = FU_PXI_RECEIVER_DEVICE(device);
	guint8 buf[FU_PXI_RECEIVER_DEVICE_OTA_BUF_SZ] = {0x0};
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new();
	g_autoptr(GByteArray) receiver_device_cmd = g_byte_array_new();

	fu_byte_array_append_uint8(ota_cmd, 0x1); /* ota init new command length */
	fu_byte_array_append_uint8(
	    ota_cmd,
	    FU_PXI_DEVICE_CMD_FW_OTA_GET_NUM_OF_MODELS); /* ota init new op code */

	self->sn++;
	if (!fu_pxi_composite_receiver_cmd(FU_PXI_DEVICE_CMD_FW_OTA_GET_NUM_OF_MODELS,
					   self->sn,
					   FU_PXI_WIRELESS_DEVICE_TARGET_RECEIVER,
					   receiver_device_cmd,
					   ota_cmd,
					   error))
		return FALSE;
	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  receiver_device_cmd->data,
					  receiver_device_cmd->len,
					  FU_UDEV_DEVICE_IOCTL_FLAG_NONE,
					  error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(device), 5); /* ms */

	buf[0] = PXI_HID_WIRELESS_DEV_OTA_REPORT_ID;
	if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(self),
					  buf,
					  sizeof(buf),
					  FU_UDEV_DEVICE_IOCTL_FLAG_NONE,
					  error))
		return FALSE;
	fu_dump_raw(G_LOG_DOMAIN, "buf from get model num", buf, sizeof(buf));
	if (!fu_memread_uint8_safe(buf, sizeof(buf), 0xA, num_of_models, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_receiver_device_add_peripherals(FuPxiReceiverDevice *device, guint idx, GError **error)
{
	struct ota_fw_dev_model model = {0x0};
	guint16 hpac_ver = 0;
	g_autofree gchar *model_name = NULL;
	g_autofree gchar *model_version = NULL;

	/* get the all wireless peripherals info */
	if (!fu_pxi_receiver_device_get_peripheral_info(device, &model, idx, error))
		return FALSE;
	if (!fu_device_has_private_flag(FU_DEVICE(device), FU_PXI_DEVICE_FLAG_IS_HPAC)) {
		model_version = g_strndup((gchar *)model.version, 5);
	} else {
		if (!fu_memread_uint16_safe(model.version, 5, 3, &hpac_ver, G_BIG_ENDIAN, error))
			return FALSE;
		model_version = fu_pxi_hpac_version_info_parse(hpac_ver);
	}
	model_name = g_strndup((gchar *)model.name, FU_PXI_DEVICE_MODEL_NAME_LEN);

	/* idx 0 is for local_device */
	if (idx == 0) {
		fu_device_set_version(FU_DEVICE(device), model_version);
		fu_device_add_instance_u16(FU_DEVICE(device),
					   "VEN",
					   fu_device_get_vid(FU_DEVICE(device)));
		fu_device_add_instance_u16(FU_DEVICE(device),
					   "DEV",
					   fu_device_get_pid(FU_DEVICE(device)));
		fu_device_add_instance_str(FU_DEVICE(device), "MODEL", model_name);
		if (!fu_device_build_instance_id(FU_DEVICE(device),
						 error,
						 "HIDRAW",
						 "VEN",
						 "DEV",
						 "MODEL",
						 NULL))
			return FALSE;
	} else {
		FuContext *ctx = fu_device_get_context(FU_DEVICE(device));
		g_autoptr(FuPxiWirelessDevice) child = fu_pxi_wireless_device_new(ctx, &model);
		g_autofree gchar *logical_id = g_strdup_printf("IDX:0x%02x", idx);
		fu_device_add_instance_u16(FU_DEVICE(child),
					   "VEN",
					   fu_device_get_vid(FU_DEVICE(device)));
		fu_device_add_instance_u16(FU_DEVICE(child),
					   "DEV",
					   fu_device_get_pid(FU_DEVICE(device)));
		fu_device_add_instance_str(FU_DEVICE(child), "MODEL", model_name);
		if (!fu_device_build_instance_id(FU_DEVICE(child),
						 error,
						 "HIDRAW",
						 "VEN",
						 "DEV",
						 "MODEL",
						 NULL))
			return FALSE;
		fu_device_set_name(FU_DEVICE(child), model_name);
		fu_device_set_version(FU_DEVICE(child), model_version);
		fu_device_set_logical_id(FU_DEVICE(child), logical_id);
		fu_device_add_child(FU_DEVICE(device), FU_DEVICE(child));
	}
	return TRUE;
}

static gboolean
fu_pxi_receiver_device_setup_guid(FuPxiReceiverDevice *self, GError **error)
{
	g_autoptr(GString) dev_name = g_string_new(fu_device_get_name(FU_DEVICE(self)));
	g_string_replace(dev_name, " ", "_", 0);
	fu_device_add_instance_strup(FU_DEVICE(self), "NAME", dev_name->str);
	return fu_device_build_instance_id(FU_DEVICE(self),
					   error,
					   "HIDRAW",
					   "VEN",
					   "DEV",
					   "NAME",
					   NULL);
}

static gboolean
fu_pxi_receiver_device_check_peripherals(FuPxiReceiverDevice *device, GError **error)
{
	guint8 num = 0;

	/* add wireless peripherals */
	if (!fu_pxi_receiver_device_get_peripheral_num(device, &num, error))
		return FALSE;
	g_debug("peripheral num: %u", num);
	for (guint8 idx = 0; idx < num; idx++) {
		if (!fu_pxi_receiver_device_add_peripherals(device, idx, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_pxi_receiver_device_setup(FuDevice *device, GError **error)
{
	FuPxiReceiverDevice *self = FU_PXI_RECEIVER_DEVICE(device);

	if (!fu_pxi_receiver_device_setup_guid(self, error)) {
		g_prefix_error(error, "failed to setup GUID: ");
		return FALSE;
	}
	if (!fu_pxi_receiver_device_fw_ota_init_new(self, 0x0000, error)) {
		g_prefix_error(error, "failed to OTA init new: ");
		return FALSE;
	}
	if (!fu_pxi_receiver_device_fw_ota_ini_new_check(self, error)) {
		g_prefix_error(error, "failed to OTA init new check: ");
		return FALSE;
	}
	if (!fu_pxi_receiver_device_check_peripherals(self, error)) {
		g_prefix_error(error, "failed to add wireless module: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pxi_receiver_device_probe(FuDevice *device, GError **error)
{
	g_autofree gchar *iface_nr = NULL;
	g_autoptr(FuDevice) usb_parent = NULL;

	/* check USB interface number */
	usb_parent = fu_device_get_backend_parent_with_subsystem(device, "usb", error);
	if (usb_parent == NULL)
		return FALSE;
	iface_nr = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(usb_parent),
					     "bInterfaceNumber",
					     FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					     error);
	if (iface_nr == NULL)
		return FALSE;
	if (g_strcmp0(iface_nr, "01") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "only USB interface 1 supported");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_pxi_receiver_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static void
fu_pxi_receiver_device_init(FuPxiReceiverDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_icon(FU_DEVICE(self), "usb-receiver");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_build_vendor_id_u16(FU_DEVICE(self), "USB", 0x093A);
	fu_device_add_protocol(FU_DEVICE(self), "com.pixart.rf");
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_PXI_FIRMWARE);
	fu_device_register_private_flag(FU_DEVICE(self), FU_PXI_DEVICE_FLAG_IS_HPAC);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_device_set_remove_delay(FU_DEVICE(self), 10000);
}

static void
fu_pxi_receiver_device_class_init(FuPxiReceiverDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_pxi_receiver_device_to_string;
	device_class->setup = fu_pxi_receiver_device_setup;
	device_class->probe = fu_pxi_receiver_device_probe;
	device_class->write_firmware = fu_pxi_receiver_device_write_firmware;
	device_class->prepare_firmware = fu_pxi_receiver_device_prepare_firmware;
	device_class->set_progress = fu_pxi_receiver_device_set_progress;
}
