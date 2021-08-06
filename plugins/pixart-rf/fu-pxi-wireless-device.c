/*
 * Copyright (C) 2020 Jimmy Yu <Jimmy_yu@pixart.com>
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */
#include "config.h"

#ifdef HAVE_HIDRAW_H
#include <linux/hidraw.h>
#include <linux/input.h>
#endif

#include <fwupdplugin.h>

#include "fu-pxi-wireless-device.h"
#include "fu-pxi-firmware.h"
#include "fu-pxi-receiver-device.h"
#include "fu-pxi-common.h"

#define FU_PXI_WIRELESS_DEV_DELAY_US	50000

struct _FuPxiWirelessDevice {
	FuDevice		 parent_instance;
	struct ota_fw_state	 fwstate;
	guint8			 sn;
	struct ota_fw_dev_model	 model;
};

G_DEFINE_TYPE (FuPxiWirelessDevice, fu_pxi_wireless_device, FU_TYPE_DEVICE)

static void
fu_pxi_wireless_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuPxiWirelessDevice *self = FU_PXI_WIRELESS_DEVICE (device);
	fu_pxi_ota_fw_state_to_string (&self->fwstate, idt, str);
	fu_common_string_append_kv (str, idt, "ModelName", (gchar*)self->model.name);
	fu_common_string_append_kx (str, idt, "ModelType", self->model.type);
	fu_common_string_append_kx (str, idt, "ModelTarget", self->model.target);
}

static gboolean
fu_pxi_wireless_device_set_feature (FuDevice *self,
				    const guint8 *buf, guint bufsz,
				    GError **error)
{
#ifdef HAVE_HIDRAW_H
	if (g_getenv ("FWUPD_PIXART_RF_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "SetFeature", buf, bufsz);
	return fu_udev_device_ioctl (FU_UDEV_DEVICE (self),
				     HIDIOCSFEATURE(bufsz), (guint8 *) buf,
				     NULL, error);
#else
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "<linux/hidraw.h> not available");
	return FALSE;
#endif
}

static gboolean
fu_pxi_wireless_device_get_feature (FuDevice *self,
				    guint8 *buf, guint bufsz,
				    GError **error)
{
#ifdef HAVE_HIDRAW_H
	if (!fu_udev_device_ioctl (FU_UDEV_DEVICE (self),
				   HIDIOCGFEATURE(bufsz), buf,
				   NULL, error)) {
		return FALSE;
	}
	if (g_getenv ("FWUPD_PIXART_RF_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "GetFeature", buf, bufsz);
	return TRUE;
#else
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "<linux/hidraw.h> not available");
	return FALSE;
#endif
}

static FuPxiReceiverDevice *
fu_pxi_wireless_device_get_parent (FuDevice *self, GError **error)
{
	FuDevice *parent = fu_device_get_parent (FU_DEVICE (self));
	if (parent == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "no parent set");
		return NULL;
	}
	return FU_PXI_RECEIVER_DEVICE (FU_UDEV_DEVICE (parent));
}

static gboolean
fu_pxi_wireless_device_get_cmd_response (FuPxiWirelessDevice *device,
					 guint8 *buf, guint bufsz,
					 GError **error)
{
	FuPxiReceiverDevice *parent;
	guint16 retry = 0;
	guint8 status = 0x0;

	parent = fu_pxi_wireless_device_get_parent (FU_DEVICE (device), error);
	if (parent == NULL)
		return FALSE;

	while (1) {
		guint8 sn = 0x0;
		memset (buf, 0, bufsz);
		buf[0] = PXI_HID_WIRELESS_DEV_OTA_REPORT_ID;

		g_usleep (5 * 1000);

		if (!fu_pxi_wireless_device_get_feature (FU_DEVICE (parent), buf, bufsz, error))
			return FALSE;

		if (!fu_common_read_uint8_safe (buf, bufsz, 0x4, &sn, error))
			return FALSE;

		if (device->sn != sn)
			retry++;
		else
			break;

		if (retry == FU_PXI_WIRELESS_DEVICE_RETRY_MAXIMUM) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_READ,
				     "reach retry maximum hid sn fail, got 0x%04x, expected 0x%04x",
				     sn, device->sn);
			return FALSE;
		}

		/*if wireless device not reply to receiver, keep to wait */
		if (!fu_common_read_uint8_safe (buf, bufsz, 0x5, &status, error))
			return FALSE;
		if (status == OTA_RSP_NOT_READY) {
			retry = 0x0;
			g_debug ("OTA_RSP_NOT_READY");
		}
	}
	return TRUE;
}

static gboolean
fu_pxi_wireless_device_check_crc (FuDevice *device, guint16 checksum, GError **error)
{
	FuPxiReceiverDevice *parent;
	FuPxiWirelessDevice *self = FU_PXI_WIRELESS_DEVICE (device);
	guint8 buf[FU_PXI_RECEIVER_DEVICE_OTA_BUF_SZ] = { 0x0 };
	guint16 checksum_device = 0x0;
	guint8 status = 0x0;
	g_autoptr(GByteArray) receiver_cmd = g_byte_array_new ();
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();

	/* proxy */
	parent = fu_pxi_wireless_device_get_parent (device, error);
	if (parent == NULL)
		return FALSE;

	/* ota check crc command */
	fu_byte_array_append_uint8 (ota_cmd, 0x3);					/* ota command length */
	fu_byte_array_append_uint8 (ota_cmd, FU_PXI_DEVICE_CMD_FW_OTA_CHECK_CRC);	/* ota command */
	fu_byte_array_append_uint16 (ota_cmd, checksum, G_LITTLE_ENDIAN);		/* checkesum */

	/* increase the serial number */
	self->sn++;

	if (!fu_pxi_composite_receiver_cmd (FU_PXI_DEVICE_CMD_FW_OTA_CHECK_CRC,
					    self->sn,
					    self->model.target,
					    receiver_cmd,
					    ota_cmd,
					    error))
		return FALSE;
	if (!fu_pxi_wireless_device_set_feature (FU_DEVICE (parent),
						 receiver_cmd->data,
						 receiver_cmd->len,
						 error))
		return FALSE;
	if (!fu_pxi_wireless_device_get_cmd_response (self, buf, sizeof(buf), error))
		return FALSE;

	if (g_getenv ("FWUPD_PIXART_RF_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "crc buf", buf, sizeof(buf));

	if (!fu_common_read_uint8_safe (buf, sizeof(buf), 0x5, &status, error))
		return FALSE;
	if (!fu_common_read_uint16_safe (buf, sizeof(buf), 0x6,
					 &checksum_device, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (status == OTA_RSP_CODE_ERROR) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "checksum fail, got 0x%04x, expected 0x%04x",
			     checksum_device,
			     checksum);
		return FALSE;
	}
	if (status != OTA_RSP_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "status:%s",
			     fu_pxi_receiver_cmd_result_to_string (status));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_wireless_device_fw_object_create (FuDevice *device, FuChunk *chk, GError **error)
{
	FuPxiReceiverDevice *parent;
	FuPxiWirelessDevice *self = FU_PXI_WIRELESS_DEVICE (device);
	g_autoptr(GByteArray) receiver_cmd = g_byte_array_new ();
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();

	/* proxy */
	parent = fu_pxi_wireless_device_get_parent (device, error);
	if (parent == NULL)
		return FALSE;

	/* ota object create command */
	fu_byte_array_append_uint8 (ota_cmd, 0x9);					/* ota command length */
	fu_byte_array_append_uint8 (ota_cmd, FU_PXI_DEVICE_CMD_FW_OBJECT_CREATE);	/* ota command */
	fu_byte_array_append_uint32 (ota_cmd, fu_chunk_get_address (chk), G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32 (ota_cmd, fu_chunk_get_data_sz (chk), G_LITTLE_ENDIAN);

	/* increase the serial number */
	self->sn++;

	/* get pixart wireless module for ota object create command */
	if (!fu_pxi_composite_receiver_cmd (FU_PXI_DEVICE_CMD_FW_OBJECT_CREATE,
					    self->sn,
					    self->model.target,
					    receiver_cmd,
					    ota_cmd, error))
		return FALSE;

	/* delay for wireless module device get command response*/
	g_usleep (FU_PXI_WIRELESS_DEV_DELAY_US);

	return fu_pxi_wireless_device_set_feature (FU_DEVICE (parent),
						   receiver_cmd->data,
						   receiver_cmd->len,
						   error);
}

static gboolean
fu_pxi_wireless_device_write_payload (FuDevice *device, FuChunk *chk, GError **error)
{
	FuPxiReceiverDevice *parent;
	FuPxiWirelessDevice *self = FU_PXI_WIRELESS_DEVICE (device);
	guint8 buf[FU_PXI_RECEIVER_DEVICE_OTA_BUF_SZ] = { 0x0 };
	guint8 status = 0x0;
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();
	g_autoptr(GByteArray) receiver_cmd = g_byte_array_new ();

	/* proxy */
	parent = fu_pxi_wireless_device_get_parent (device, error);
	if (parent == NULL)
		return FALSE;

	/* ota write payload command */
	fu_byte_array_append_uint8 (ota_cmd, fu_chunk_get_data_sz (chk));			/* ota command length */
	g_byte_array_append (ota_cmd, fu_chunk_get_data (chk), fu_chunk_get_data_sz (chk));	/* payload content */

	/* increase the serial number */
	self->sn++;

	/* get pixart wireless module for ota write payload command */
	if (!fu_pxi_composite_receiver_cmd (FU_PXI_DEVICE_CMD_FW_OTA_PAYLOAD_CONTENT,
					    self->sn,
					    self->model.target,
					    receiver_cmd,
					    ota_cmd, error))
		return FALSE;
	if (!fu_pxi_wireless_device_set_feature (FU_DEVICE (parent),
						 receiver_cmd->data,
						 receiver_cmd->len,
						 error))
		return FALSE;

	/* delay for wireless module device get command response*/
	g_usleep (FU_PXI_WIRELESS_DEV_DELAY_US);

	if (!fu_pxi_wireless_device_get_cmd_response (self, buf, sizeof(buf), error))
		return FALSE;
	if (!fu_common_read_uint8_safe (buf, sizeof(buf), 0x5, &status, error))
		return FALSE;
	if (status != OTA_RSP_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "cmd rsp check fail: %s [0x%02x]",
			     fu_pxi_receiver_cmd_result_to_string (status),
			     status);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_wireless_device_write_chunk (FuDevice *device, FuChunk *chk, GError **error)
{
	FuPxiWirelessDevice *self = FU_PXI_WIRELESS_DEVICE (device);
	guint16 checksum;
	guint32 prn = 0;
	g_autoptr(GPtrArray) chunks = NULL;

	/* send create fw object command */
	if (!fu_pxi_wireless_device_fw_object_create (device, chk, error))
		return FALSE;

	/* write payload */
	chunks = fu_chunk_array_new (fu_chunk_get_data (chk),
				     fu_chunk_get_data_sz (chk),
				     fu_chunk_get_address (chk),
				     0x0, self->fwstate.mtu_size);

	/* calculate checksum of chunk */
	checksum = fu_pxi_common_sum16 (fu_chunk_get_data (chk), fu_chunk_get_data_sz (chk));
	self->fwstate.checksum += checksum;

	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk2 = g_ptr_array_index (chunks, i);
		if (!fu_pxi_wireless_device_write_payload (device, chk2, error))
			return FALSE;
		prn++;
		/* check crc at fw when PRN over threshold write or
		 * offset reach max object sz or write offset reach fw length */
		if (prn >= self->fwstate.prn_threshold || i == (chunks->len - 1)) {
			if (!fu_pxi_wireless_device_check_crc (device, self->fwstate.checksum, error))
				return FALSE;
			prn = 0;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_wireless_device_fw_ota_init_new (FuDevice *device, gsize bufsz, GError **error)
{
	FuPxiReceiverDevice *parent;
	FuPxiWirelessDevice *self = FU_PXI_WIRELESS_DEVICE (device);
	guint8 fw_version[10] = { 0x0 };
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();
	g_autoptr(GByteArray) receiver_cmd = g_byte_array_new ();

	/* proxy */
	parent = fu_pxi_wireless_device_get_parent (device, error);
	if (parent == NULL)
		return FALSE;

	fu_byte_array_append_uint8 (ota_cmd, 0X06);					/* ota init new command length */
	fu_byte_array_append_uint8 (ota_cmd, FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW);	/* ota ota init new op code */
	fu_byte_array_append_uint32 (ota_cmd, bufsz, G_LITTLE_ENDIAN);			/* fw size */
	fu_byte_array_append_uint8 (ota_cmd, 0x0);					/* ota setting */
	g_byte_array_append (ota_cmd, fw_version, sizeof(fw_version));			/* ota version */

	/* increase the serial number */
	self->sn++;
	if (!fu_pxi_composite_receiver_cmd (FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW,
					    self->sn,
					    self->model.target,
					    receiver_cmd,
					    ota_cmd, error))
		return FALSE;
	return fu_pxi_wireless_device_set_feature (FU_DEVICE (parent),
						   receiver_cmd->data,
						   receiver_cmd->len,
						   error);
}

static gboolean
fu_pxi_wireless_device_fw_ota_ini_new_check (FuDevice *device, GError **error)
{
	FuPxiReceiverDevice *parent;
	FuPxiWirelessDevice *self = FU_PXI_WIRELESS_DEVICE (device);
	guint8 buf[FU_PXI_RECEIVER_DEVICE_OTA_BUF_SZ] = { 0x0 };
	guint8 status = 0x0;
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();
	g_autoptr(GByteArray) receiver_cmd = g_byte_array_new ();

	/* proxy */
	parent = fu_pxi_wireless_device_get_parent (device, error);
	if (parent == NULL)
		return FALSE;

	/* ota command */
	fu_byte_array_append_uint8 (ota_cmd, 0x1);					/* ota command length */
	fu_byte_array_append_uint8 (ota_cmd, FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW_CHECK);	/* ota command */

	/* increase the serial number */
	self->sn++;

	/* get pixart wireless module ota command */
	if (!fu_pxi_composite_receiver_cmd (FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW_CHECK,
					    self->sn,
					    self->model.target,
					    receiver_cmd,
					    ota_cmd, error))
		return FALSE;
	if (!fu_pxi_wireless_device_set_feature (FU_DEVICE (parent), receiver_cmd->data,
							receiver_cmd->len, error))
		return FALSE;

	/* delay for wireless module device get command response*/
	g_usleep (FU_PXI_WIRELESS_DEV_DELAY_US);

	if (!fu_pxi_wireless_device_get_cmd_response (self, buf, sizeof(buf), error))
		return FALSE;
	if (!fu_common_read_uint8_safe (buf, sizeof(buf), 0x5, &status, error))
		return FALSE;
	if (status != OTA_RSP_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "cmd rsp check fail: %s [0x%02x]",
			     fu_pxi_receiver_cmd_result_to_string (status),
			     status);
		return FALSE;
	}

	/* shared state */
	return fu_pxi_ota_fw_state_parse (&self->fwstate, buf, sizeof(buf), 0x09, error);
}

static gboolean
fu_pxi_wireless_device_fw_upgrade (FuDevice *device, FuFirmware *firmware, GError **error)
{
	FuPxiReceiverDevice *parent;
	FuPxiWirelessDevice *self = FU_PXI_WIRELESS_DEVICE (device);
	const gchar *version;
	const guint8 *buf;
	gsize bufsz = 0;
	guint16 checksum = 0x0;
	guint8 fw_version[5] = { 0x0 };
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();
	g_autoptr(GByteArray) receiver_cmd = g_byte_array_new ();
	g_autoptr(GBytes) fw = NULL;

	/* proxy */
	parent = fu_pxi_wireless_device_get_parent (device, error);
	if (parent == NULL)
		return FALSE;

	fw = fu_firmware_get_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	buf = g_bytes_get_data (fw, &bufsz);
	checksum = fu_pxi_common_sum16 (buf, bufsz);

	/* ota fw upgrade command */
	fu_byte_array_append_uint8 (ota_cmd, 0x0c);					/* ota fw upgrade command length */
	fu_byte_array_append_uint8 (ota_cmd, FU_PXI_DEVICE_CMD_FW_UPGRADE);		/* ota fw upgrade command opccode */
	fu_byte_array_append_uint32 (ota_cmd, bufsz, G_LITTLE_ENDIAN);			/* ota fw upgrade command fw size */
	fu_byte_array_append_uint16 (ota_cmd, checksum, G_LITTLE_ENDIAN);		/* ota fw upgrade command checksum */

	version = fu_firmware_get_version (firmware);
	if (!fu_memcpy_safe (fw_version, sizeof(fw_version), 0x0,	/* dst */
			     (guint8 *) version, strlen (version), 0x0,	/* src */
			     sizeof(fw_version), error))
		return FALSE;

	g_byte_array_append (ota_cmd, fw_version, sizeof(fw_version));

	self->sn++;
	/* get pixart wireless module ota command */
	if (!fu_pxi_composite_receiver_cmd (FU_PXI_DEVICE_CMD_FW_UPGRADE,
					    self->sn,
					    self->model.target,
					    receiver_cmd,
					    ota_cmd, error))
		return FALSE;
	/* update device status */
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_VERIFY);

	/* send ota fw upgrade command */
	return fu_pxi_wireless_device_set_feature (FU_DEVICE (parent),
						   receiver_cmd->data,
						   receiver_cmd->len,
						   error);
}

static gboolean
fu_pxi_wireless_device_reset (FuDevice *device, GError **error)
{
	FuPxiReceiverDevice *parent;
	FuPxiWirelessDevice *self = FU_PXI_WIRELESS_DEVICE (device);
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();
	g_autoptr(GByteArray) receiver_cmd = g_byte_array_new ();

	/* proxy */
	parent = fu_pxi_wireless_device_get_parent (device, error);
	if (parent == NULL)
		return FALSE;

	/* ota mcu reset command */
	fu_byte_array_append_uint8 (ota_cmd, 0x1);				/* ota mcu reset command */
	fu_byte_array_append_uint8 (ota_cmd, FU_PXI_DEVICE_CMD_FW_MCU_RESET);	/* ota mcu reset command op code */
	fu_byte_array_append_uint8 (ota_cmd, OTA_RESET);			/* ota mcu reset command reason */

	self->sn++;
	/* get pixart wireless module ota command */
	if (!fu_pxi_composite_receiver_cmd (FU_PXI_DEVICE_CMD_FW_MCU_RESET,
					    self->sn,
					    self->model.target,
					    receiver_cmd,
					    ota_cmd,
					    error))
		return FALSE;

	/* send ota mcu reset command */
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_RESTART);
	return fu_pxi_wireless_device_set_feature (FU_DEVICE (parent),
						   receiver_cmd->data,
						   receiver_cmd->len,
						   error);
}

static FuFirmware *
fu_pxi_wireless_device_prepare_firmware (FuDevice *device,
					 GBytes *fw,
					 FwupdInstallFlags flags,
					 GError **error)
{

	g_autoptr(FuFirmware) firmware = fu_pxi_firmware_new ();
	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;
	return g_steal_pointer (&firmware);
}

static gboolean
fu_pxi_wireless_device_write_firmware(FuDevice *device,
				      FuFirmware *firmware,
				      FuProgress *progress,
				      FwupdInstallFlags flags,
				      GError **error)
{
	FuPxiWirelessDevice *self = FU_PXI_WIRELESS_DEVICE (device);
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* get the default image */
	fw = fu_firmware_get_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	/* send fw ota init command */
	if (!fu_pxi_wireless_device_fw_ota_init_new (device, g_bytes_get_size (fw), error))
		return FALSE;
	if (!fu_pxi_wireless_device_fw_ota_ini_new_check (device ,error))
		return FALSE;

	chunks = fu_chunk_array_new_from_bytes (fw, 0x0, 0x0, FU_PXI_DEVICE_OBJECT_SIZE_MAX);
	/* prepare write fw into device */
	self->fwstate.offset = 0;
	self->fwstate.checksum = 0;

	/* write fw into device */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	for (guint i = self->fwstate.offset; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		if (!fu_pxi_wireless_device_write_chunk (device, chk, error))
			return FALSE;
		fu_progress_set_percentage_full(progress, (gsize)i, (gsize)chunks->len);
	}

	/* fw upgrade command */
	if (!fu_pxi_wireless_device_fw_upgrade (device, firmware, error))
		return FALSE;

	/* send device reset command */
	g_usleep (FU_PXI_WIRELESS_DEV_DELAY_US);
	return fu_pxi_wireless_device_reset (device, error);
}

static void
fu_pxi_wireless_device_init (FuPxiWirelessDevice *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_internal_flag (FU_DEVICE (self), FU_DEVICE_INTERNAL_FLAG_USE_PARENT_FOR_OPEN);
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_vendor_id (FU_DEVICE (self), "USB:0x093A");
	fu_device_add_protocol (FU_DEVICE (self), "com.pixart.rf");
}

static void
fu_pxi_wireless_device_class_init (FuPxiWirelessDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->write_firmware = fu_pxi_wireless_device_write_firmware;
	klass_device->prepare_firmware = fu_pxi_wireless_device_prepare_firmware;
	klass_device->to_string = fu_pxi_wireless_device_to_string;
}

FuPxiWirelessDevice *
fu_pxi_wireless_device_new (struct ota_fw_dev_model *model)
{
	FuPxiWirelessDevice *self = NULL;
	self = g_object_new (FU_TYPE_PXI_WIRELESS_DEVICE, NULL);

	self->model.status = model->status;
	for (guint idx = 0; idx < FU_PXI_DEVICE_MODEL_NAME_LEN; idx++)
		self->model.name[idx] = model->name[idx];
	self->model.type = model->type;
	self->model.target = model->target;
	self->sn = model->target;
	return self;
}
