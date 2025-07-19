/*
 * Copyright 2020 Jimmy Yu <Jimmy_yu@pixart.com>
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "config.h"

#ifdef HAVE_HIDRAW_H
#include <linux/hidraw.h>
#include <linux/input.h>
#endif

#include "fu-pxi-common.h"
#include "fu-pxi-firmware.h"
#include "fu-pxi-receiver-device.h"
#include "fu-pxi-struct.h"
#include "fu-pxi-wireless-device.h"

#define FU_PXI_WIRELESS_DEV_DELAY_MS	     50
#define FU_PXI_WIRELESS_DEV_PAYLOAD_DELAY_MS 15

struct _FuPxiWirelessDevice {
	FuDevice parent_instance;
	struct ota_fw_state fwstate;
	guint8 sn;
	struct ota_fw_dev_model model;
};

G_DEFINE_TYPE(FuPxiWirelessDevice, fu_pxi_wireless_device, FU_TYPE_DEVICE)

static void
fu_pxi_wireless_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuPxiWirelessDevice *self = FU_PXI_WIRELESS_DEVICE(device);
	fu_pxi_ota_fw_state_to_string(&self->fwstate, idt, str);
	fwupd_codec_string_append(str, idt, "ModelName", (gchar *)self->model.name);
	fwupd_codec_string_append_hex(str, idt, "ModelType", self->model.type);
	fwupd_codec_string_append_hex(str, idt, "ModelTarget", self->model.target);
}

static FuPxiReceiverDevice *
fu_pxi_wireless_device_get_parent(FuDevice *self, GError **error)
{
	FuDevice *parent = fu_device_get_parent(FU_DEVICE(self));
	if (parent == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "no parent set");
		return NULL;
	}
	return FU_PXI_RECEIVER_DEVICE(FU_UDEV_DEVICE(parent));
}

static FuFirmware *
fu_pxi_wireless_device_prepare_firmware(FuDevice *device,
					GInputStream *stream,
					FuProgress *progress,
					FuFirmwareParseFlags flags,
					GError **error)
{
	FuPxiReceiverDevice *parent;
	g_autoptr(FuFirmware) firmware = fu_pxi_firmware_new();

	parent = fu_pxi_wireless_device_get_parent(device, error);
	if (parent == NULL)
		return NULL;

	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;

	if (fu_device_has_private_flag(FU_DEVICE(parent), FU_PXI_DEVICE_FLAG_IS_HPAC) &&
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
	} else if (fu_device_has_private_flag(FU_DEVICE(parent), FU_PXI_DEVICE_FLAG_IS_HPAC) !=
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
fu_pxi_wireless_device_get_cmd_response(FuPxiWirelessDevice *device,
					guint8 *buf,
					guint bufsz,
					GError **error)
{
	FuPxiReceiverDevice *parent;
	guint16 retry = 0;
	guint8 status = 0x0;

	parent = fu_pxi_wireless_device_get_parent(FU_DEVICE(device), error);
	if (parent == NULL)
		return FALSE;

	while (1) {
		guint8 sn = 0x0;
		memset(buf, 0, bufsz);
		buf[0] = PXI_HID_WIRELESS_DEV_OTA_REPORT_ID;

		fu_device_sleep(FU_DEVICE(device), FU_PXI_WIRELESS_DEV_DELAY_MS); /* ms */

		if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(parent),
						  buf,
						  bufsz,
						  FU_IOCTL_FLAG_NONE,
						  error))
			return FALSE;
		if (!fu_memread_uint8_safe(buf, bufsz, 0x4, &sn, error))
			return FALSE;

		if (device->sn != sn)
			retry++;
		else
			break;

		if (retry == FU_PXI_WIRELESS_DEVICE_RETRY_MAXIMUM) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "reach retry maximum hid sn fail, got 0x%04x, expected 0x%04x",
				    sn,
				    device->sn);
			return FALSE;
		}

		/*if wireless device not reply to receiver, keep to wait */
		if (!fu_memread_uint8_safe(buf, bufsz, 0x5, &status, error))
			return FALSE;
		if (status == FU_PXI_WIRELESS_MODULE_OTA_RSP_CODE_NOT_READY) {
			retry = 0x0;
			g_debug("FU_PXI_WIRELESS_MODULE_OTA_RSP_CODE_NOT_READY");
		}
	}
	return TRUE;
}

static gboolean
fu_pxi_wireless_device_check_crc(FuDevice *device, guint16 checksum, GError **error)
{
	FuPxiReceiverDevice *parent;
	FuPxiWirelessDevice *self = FU_PXI_WIRELESS_DEVICE(device);
	guint8 buf[FU_PXI_RECEIVER_DEVICE_OTA_BUF_SZ] = {0x0};
	guint16 checksum_device = 0x0;
	guint8 status = 0x0;
	g_autoptr(GByteArray) receiver_cmd = g_byte_array_new();
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new();

	/* proxy */
	parent = fu_pxi_wireless_device_get_parent(device, error);
	if (parent == NULL)
		return FALSE;

	/* ota check crc command */
	fu_byte_array_append_uint8(ota_cmd, 0x3); /* ota command length */
	fu_byte_array_append_uint8(ota_cmd, FU_PXI_DEVICE_CMD_FW_OTA_CHECK_CRC); /* ota command */
	fu_byte_array_append_uint16(ota_cmd, checksum, G_LITTLE_ENDIAN);	 /* checksum */

	/* increase the serial number */
	self->sn++;

	if (!fu_pxi_composite_receiver_cmd(FU_PXI_DEVICE_CMD_FW_OTA_CHECK_CRC,
					   self->sn,
					   FU_PXI_WIRELESS_DEVICE_TARGET_RECEIVER,
					   receiver_cmd,
					   ota_cmd,
					   error))
		return FALSE;
	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(parent),
					  receiver_cmd->data,
					  receiver_cmd->len,
					  FU_IOCTL_FLAG_NONE,
					  error))
		return FALSE;
	if (!fu_pxi_wireless_device_get_cmd_response(self, buf, sizeof(buf), error))
		return FALSE;

	fu_dump_raw(G_LOG_DOMAIN, "crc buf", buf, sizeof(buf));

	if (!fu_memread_uint8_safe(buf, sizeof(buf), 0x5, &status, error))
		return FALSE;
	if (!fu_memread_uint16_safe(buf,
				    sizeof(buf),
				    0x6,
				    &checksum_device,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (status == FU_PXI_WIRELESS_MODULE_OTA_RSP_CODE_ERROR) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "checksum fail, got 0x%04x, expected 0x%04x",
			    checksum_device,
			    checksum);
		return FALSE;
	}
	if (status != FU_PXI_WIRELESS_MODULE_OTA_RSP_CODE_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "status:%s",
			    fu_pxi_wireless_module_ota_rsp_code_to_string(status));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_wireless_device_fw_object_create(FuDevice *device, FuChunk *chk, GError **error)
{
	FuPxiReceiverDevice *parent;
	FuPxiWirelessDevice *self = FU_PXI_WIRELESS_DEVICE(device);
	guint8 buf[FU_PXI_RECEIVER_DEVICE_OTA_BUF_SZ] = {0x0};
	guint8 status = 0x0;
	g_autoptr(GByteArray) receiver_cmd = g_byte_array_new();
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new();

	/* proxy */
	parent = fu_pxi_wireless_device_get_parent(device, error);
	if (parent == NULL)
		return FALSE;

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
					   self->model.target,
					   receiver_cmd,
					   ota_cmd,
					   error))
		return FALSE;

	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(parent),
					  receiver_cmd->data,
					  receiver_cmd->len,
					  FU_IOCTL_FLAG_NONE,
					  error))
		return FALSE;

	/* delay for wireless module device get command response*/
	fu_device_sleep(device, FU_PXI_WIRELESS_DEV_DELAY_MS);

	if (!fu_pxi_wireless_device_get_cmd_response(self, buf, sizeof(buf), error))
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
fu_pxi_wireless_device_write_payload(FuDevice *device, FuChunk *chk, GError **error)
{
	FuPxiReceiverDevice *parent;
	FuPxiWirelessDevice *self = FU_PXI_WIRELESS_DEVICE(device);
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new();
	g_autoptr(GByteArray) receiver_cmd = g_byte_array_new();

	/* proxy */
	parent = fu_pxi_wireless_device_get_parent(device, error);
	if (parent == NULL)
		return FALSE;

	/* ota write payload command */
	fu_byte_array_append_uint8(ota_cmd, fu_chunk_get_data_sz(chk)); /* ota command length */
	g_byte_array_append(ota_cmd,
			    fu_chunk_get_data(chk),
			    fu_chunk_get_data_sz(chk)); /* payload content */

	/* increase the serial number */
	self->sn++;

	/* get pixart wireless module for ota write payload command */
	if (!fu_pxi_composite_receiver_cmd(FU_PXI_DEVICE_CMD_FW_OTA_PAYLOAD_CONTENT,
					   self->sn,
					   self->model.target,
					   receiver_cmd,
					   ota_cmd,
					   error))
		return FALSE;
	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(parent),
					  receiver_cmd->data,
					  receiver_cmd->len,
					  FU_IOCTL_FLAG_NONE,
					  error))
		return FALSE;

	/* delay for each payload packet */
	fu_device_sleep(device, FU_PXI_WIRELESS_DEV_PAYLOAD_DELAY_MS);

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_wireless_device_write_chunk(FuDevice *device, FuChunk *chk, GError **error)
{
	FuPxiWirelessDevice *self = FU_PXI_WIRELESS_DEVICE(device);
	guint32 prn = 0;
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GBytes) chk_bytes = fu_chunk_get_bytes(chk);

	/* send create fw object command */
	if (!fu_pxi_wireless_device_fw_object_create(device, chk, error))
		return FALSE;

	/* write payload */
	chunks = fu_chunk_array_new_from_bytes(chk_bytes,
					       fu_chunk_get_address(chk),
					       FU_CHUNK_PAGESZ_NONE,
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
		if (!fu_pxi_wireless_device_write_payload(device, chk2, error))
			return FALSE;
		prn++;
		/* check crc at fw when PRN over threshold write or
		 * offset reach max object sz or write offset reach fw length */
		if (prn >= self->fwstate.prn_threshold ||
		    i == (fu_chunk_array_length(chunks) - 1)) {
			if (!fu_pxi_wireless_device_check_crc(device,
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
fu_pxi_wireless_device_fw_ota_preceding(FuDevice *device, GError **error)
{
	FuPxiReceiverDevice *parent;
	FuPxiWirelessDevice *self = FU_PXI_WIRELESS_DEVICE(device);
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new();
	g_autoptr(GByteArray) receiver_cmd = g_byte_array_new();

	/* proxy */
	parent = fu_pxi_wireless_device_get_parent(device, error);
	if (parent == NULL)
		return FALSE;

	fu_byte_array_append_uint8(ota_cmd, 0x01); /* ota preceding command length */
	fu_byte_array_append_uint8(ota_cmd,
				   FU_PXI_DEVICE_CMD_FW_OTA_PRECEDING); /* ota preceding op code */

	/* increase the serial number */
	self->sn++;
	if (!fu_pxi_composite_receiver_cmd(FU_PXI_DEVICE_CMD_FW_OTA_PRECEDING,
					   self->sn,
					   self->model.target,
					   receiver_cmd,
					   ota_cmd,
					   error))
		return FALSE;

	return fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(parent),
					    receiver_cmd->data,
					    receiver_cmd->len,
					    FU_IOCTL_FLAG_NONE,
					    error);
}

static gboolean
fu_pxi_wireless_device_fw_ota_init_new(FuDevice *device, gsize bufsz, GError **error)
{
	FuPxiReceiverDevice *parent;
	FuPxiWirelessDevice *self = FU_PXI_WIRELESS_DEVICE(device);
	guint8 buf[FU_PXI_RECEIVER_DEVICE_OTA_BUF_SZ] = {0x0};
	guint8 status = 0x0;
	guint8 fw_version[10] = {0x0};
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new();
	g_autoptr(GByteArray) receiver_cmd = g_byte_array_new();

	/* proxy */
	parent = fu_pxi_wireless_device_get_parent(device, error);
	if (parent == NULL)
		return FALSE;

	fu_byte_array_append_uint8(ota_cmd, 0X06); /* ota init new command length */
	fu_byte_array_append_uint8(ota_cmd,
				   FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW); /* ota init new op code */
	fu_byte_array_append_uint32(ota_cmd, bufsz, G_LITTLE_ENDIAN);  /* fw size */
	fu_byte_array_append_uint8(ota_cmd, 0x0);		       /* ota setting */
	g_byte_array_append(ota_cmd, fw_version, sizeof(fw_version));  /* ota version */

	/* increase the serial number */
	self->sn++;
	if (!fu_pxi_composite_receiver_cmd(FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW,
					   self->sn,
					   self->model.target,
					   receiver_cmd,
					   ota_cmd,
					   error))
		return FALSE;

	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(parent),
					  receiver_cmd->data,
					  receiver_cmd->len,
					  FU_IOCTL_FLAG_NONE,
					  error))
		return FALSE;

	/* delay for wireless module device get command response*/
	fu_device_sleep(device, FU_PXI_WIRELESS_DEV_DELAY_MS);

	if (!fu_pxi_wireless_device_get_cmd_response(self, buf, sizeof(buf), error))
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
fu_pxi_wireless_device_fw_ota_ini_new_check(FuDevice *device, GError **error)
{
	FuPxiReceiverDevice *parent;
	FuPxiWirelessDevice *self = FU_PXI_WIRELESS_DEVICE(device);
	guint8 buf[FU_PXI_RECEIVER_DEVICE_OTA_BUF_SZ] = {0x0};
	guint8 status = 0x0;
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new();
	g_autoptr(GByteArray) receiver_cmd = g_byte_array_new();

	/* proxy */
	parent = fu_pxi_wireless_device_get_parent(device, error);
	if (parent == NULL)
		return FALSE;

	/* ota command */
	fu_byte_array_append_uint8(ota_cmd, 0x1); /* ota command length */
	fu_byte_array_append_uint8(ota_cmd,
				   FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW_CHECK); /* ota command */

	/* increase the serial number */
	self->sn++;

	/* get pixart wireless module ota command */
	if (!fu_pxi_composite_receiver_cmd(FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW_CHECK,
					   self->sn,
					   self->model.target,
					   receiver_cmd,
					   ota_cmd,
					   error))
		return FALSE;
	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(parent),
					  receiver_cmd->data,
					  receiver_cmd->len,
					  FU_IOCTL_FLAG_NONE,
					  error))
		return FALSE;

	/* delay for wireless module device get command response*/
	fu_device_sleep(device, FU_PXI_WIRELESS_DEV_DELAY_MS);

	if (!fu_pxi_wireless_device_get_cmd_response(self, buf, sizeof(buf), error))
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

	/* shared state */
	return fu_pxi_ota_fw_state_parse(&self->fwstate, buf, sizeof(buf), 0x09, error);
}

static gboolean
fu_pxi_wireless_device_fw_upgrade(FuDevice *device,
				  FuFirmware *firmware,
				  FuProgress *progress,
				  GError **error)
{
	FuPxiReceiverDevice *parent;
	FuPxiWirelessDevice *self = FU_PXI_WIRELESS_DEVICE(device);
	guint8 buf[FU_PXI_RECEIVER_DEVICE_OTA_BUF_SZ] = {0x0};
	guint8 status = 0x0;
	const gchar *version;
	guint8 fw_version[5] = {0x0};
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new();
	g_autoptr(GByteArray) receiver_cmd = g_byte_array_new();
	g_autoptr(GBytes) fw = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 5, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 95, NULL);

	/* proxy */
	parent = fu_pxi_wireless_device_get_parent(device, error);
	if (parent == NULL)
		return FALSE;

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

	if (!fu_device_has_private_flag(FU_DEVICE(parent), FU_PXI_DEVICE_FLAG_IS_HPAC)) {
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

	self->sn++;
	/* get pixart wireless module ota command */
	if (!fu_pxi_composite_receiver_cmd(FU_PXI_DEVICE_CMD_FW_UPGRADE,
					   self->sn,
					   self->model.target,
					   receiver_cmd,
					   ota_cmd,
					   error))
		return FALSE;
	fu_progress_step_done(progress);

	/* send ota fw upgrade command */
	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(parent),
					  receiver_cmd->data,
					  receiver_cmd->len,
					  FU_IOCTL_FLAG_NONE,
					  error))
		return FALSE;

	/* delay for wireless module device get command response*/
	fu_device_sleep(device, FU_PXI_WIRELESS_DEV_DELAY_MS);

	if (!fu_pxi_wireless_device_get_cmd_response(self, buf, sizeof(buf), error))
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

	fu_progress_step_done(progress);
	return TRUE;
}

static gboolean
fu_pxi_wireless_device_reset(FuDevice *device, GError **error)
{
	FuPxiReceiverDevice *parent;
	FuPxiWirelessDevice *self = FU_PXI_WIRELESS_DEVICE(device);
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new();
	g_autoptr(GByteArray) receiver_cmd = g_byte_array_new();

	/* proxy */
	parent = fu_pxi_wireless_device_get_parent(device, error);
	if (parent == NULL)
		return FALSE;

	/* ota mcu reset command */
	fu_byte_array_append_uint8(ota_cmd, 0x1); /* ota mcu reset command */
	fu_byte_array_append_uint8(
	    ota_cmd,
	    FU_PXI_DEVICE_CMD_FW_MCU_RESET); /* ota mcu reset command op code */
	fu_byte_array_append_uint8(
	    ota_cmd,
	    FU_PXI_OTA_DISCONNECT_REASON_RESET); /* ota mcu reset command reason */

	self->sn++;
	/* get pixart wireless module ota command */
	if (!fu_pxi_composite_receiver_cmd(FU_PXI_DEVICE_CMD_FW_MCU_RESET,
					   self->sn,
					   self->model.target,
					   receiver_cmd,
					   ota_cmd,
					   error))
		return FALSE;

	/* send ota mcu reset command to device*/
	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(parent),
					  receiver_cmd->data,
					  receiver_cmd->len,
					  FU_IOCTL_FLAG_NONE,
					  error))
		return FALSE;

	self->sn++;
	/* send ota mcu reset command to receiver */
	g_byte_array_set_size(receiver_cmd, 0);
	if (!fu_pxi_composite_receiver_cmd(FU_PXI_DEVICE_CMD_FW_MCU_RESET,
					   self->sn,
					   FU_PXI_WIRELESS_DEVICE_TARGET_RECEIVER,
					   receiver_cmd,
					   ota_cmd,
					   error))
		return FALSE;

	return fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(parent),
					    receiver_cmd->data,
					    receiver_cmd->len,
					    FU_IOCTL_FLAG_NONE,
					    error);
}

static gboolean
fu_pxi_wireless_device_write_firmware(FuDevice *device,
				      FuFirmware *firmware,
				      FuProgress *progress,
				      FwupdInstallFlags flags,
				      GError **error)
{
	FuPxiReceiverDevice *parent;
	FuPxiWirelessDevice *self = FU_PXI_WIRELESS_DEVICE(device);
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

	/* send preceding command */
	if (!fu_pxi_wireless_device_fw_ota_preceding(device, error))
		return FALSE;
	/* send fw ota init command */
	if (!fu_pxi_wireless_device_fw_ota_init_new(device, g_bytes_get_size(fw), error))
		return FALSE;
	if (!fu_pxi_wireless_device_fw_ota_ini_new_check(device, error))
		return FALSE;
	fu_progress_step_done(progress);

	chunks = fu_chunk_array_new_from_bytes(fw,
					       FU_CHUNK_ADDR_OFFSET_NONE,
					       FU_CHUNK_PAGESZ_NONE,
					       FU_PXI_DEVICE_OBJECT_SIZE_MAX);
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
		if (!fu_pxi_wireless_device_write_chunk(device, chk, error))
			return FALSE;
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						(gsize)i + self->fwstate.offset + 1,
						(gsize)fu_chunk_array_length(chunks));
	}
	fu_progress_step_done(progress);

	/* fw upgrade command */
	if (!fu_pxi_wireless_device_fw_upgrade(device,
					       firmware,
					       fu_progress_get_child(progress),
					       error))
		return FALSE;
	fu_progress_step_done(progress);

	/* send device reset command */
	fu_device_sleep(device, FU_PXI_WIRELESS_DEV_DELAY_MS);
	if (!fu_pxi_wireless_device_reset(device, error))
		return FALSE;

	parent = fu_pxi_wireless_device_get_parent(device, error);
	if (parent == NULL)
		return FALSE;

	fu_progress_step_done(progress);
	fu_device_add_flag(FU_DEVICE(parent), FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	/* success */
	return TRUE;
}

static void
fu_pxi_wireless_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static void
fu_pxi_wireless_device_init(FuPxiWirelessDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PARENT_FOR_OPEN);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_build_vendor_id_u16(FU_DEVICE(self), "USB", 0x093A);
	fu_device_add_protocol(FU_DEVICE(self), "com.pixart.rf");
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_PXI_FIRMWARE);
	fu_device_set_remove_delay(FU_DEVICE(self), 10000);
}

static void
fu_pxi_wireless_device_class_init(FuPxiWirelessDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->write_firmware = fu_pxi_wireless_device_write_firmware;
	device_class->to_string = fu_pxi_wireless_device_to_string;
	device_class->prepare_firmware = fu_pxi_wireless_device_prepare_firmware;
	device_class->set_progress = fu_pxi_wireless_device_set_progress;
}

FuPxiWirelessDevice *
fu_pxi_wireless_device_new(FuContext *ctx, struct ota_fw_dev_model *model)
{
	FuPxiWirelessDevice *self = NULL;
	self = g_object_new(FU_TYPE_PXI_WIRELESS_DEVICE, "context", ctx, NULL);

	self->model.status = model->status;
	for (guint idx = 0; idx < FU_PXI_DEVICE_MODEL_NAME_LEN; idx++)
		self->model.name[idx] = model->name[idx];
	self->model.type = model->type;
	self->model.target = model->target;
	self->sn = model->target;
	return self;
}
