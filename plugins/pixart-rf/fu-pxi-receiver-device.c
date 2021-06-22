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

#include "fu-pxi-receiver-device.h"
#include "fu-pxi-wireless-device.h"
#include "fu-pxi-firmware.h"

struct _FuPxiReceiverDevice {
	FuUdevDevice		 parent_instance;
	struct ota_fw_state	 fwstate;
	guint8			 sn;
	guint			 vendor;
	guint			 product;
};

G_DEFINE_TYPE (FuPxiReceiverDevice, fu_pxi_receiver_device, FU_TYPE_UDEV_DEVICE)

#ifdef HAVE_HIDRAW_H
static gboolean
fu_pxi_receiver_device_get_raw_info (FuPxiReceiverDevice *self, struct hidraw_devinfo *info, GError **error)
{
	if (!fu_udev_device_ioctl (FU_UDEV_DEVICE (self),
				   HIDIOCGRAWINFO, (guint8 *) info,
				   NULL, error)) {
		return FALSE;
	}
	return TRUE;
}
#endif

static void
fu_pxi_receiver_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuPxiReceiverDevice *self = FU_PXI_RECEIVER_DEVICE (device);
	fu_pxi_ota_fw_state_to_string (&self->fwstate, idt, str);
	fu_common_string_append_kx (str, idt, "Vendor", self->vendor);
	fu_common_string_append_kx (str, idt, "Product", self->product);
}

static gboolean
fu_pxi_receiver_device_set_feature (FuPxiReceiverDevice *self, const guint8 *buf, guint bufsz, GError **error)
{
#ifdef HAVE_HIDRAW_H
	if (g_getenv ("FWUPD_PIXART_RF_VERBOSE") != NULL) {
		fu_common_dump_raw (G_LOG_DOMAIN, "SetFeature", buf, bufsz);
	}
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
fu_pxi_receiver_device_get_feature (FuPxiReceiverDevice *self, guint8 *buf, guint bufsz, GError **error)
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

static gboolean
fu_pxi_receiver_device_fw_ota_init_new (FuPxiReceiverDevice *device, gsize bufsz, GError **error)
{
	guint8 fw_version[10] = { 0x0 };
	g_autoptr(GByteArray) receiver_device_cmd = g_byte_array_new ();
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();
	FuPxiReceiverDevice *self = FU_PXI_RECEIVER_DEVICE (device);

	fu_byte_array_append_uint8 (ota_cmd, 0X06);					/* ota init new command length */
	fu_byte_array_append_uint8 (ota_cmd, FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW);	/* ota ota init new op code */
	fu_byte_array_append_uint32 (ota_cmd, bufsz, G_LITTLE_ENDIAN);			/* fw size */
	fu_byte_array_append_uint8 (ota_cmd, 0x0);					/* ota setting */
	g_byte_array_append (ota_cmd, fw_version, sizeof(fw_version));			/* ota version */

	self->sn++;
	if (!fu_pxi_composite_receiver_cmd (FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW,
					    self->sn,
					    FU_PXI_WIRELESS_DEVICE_TARGET_RECEIVER,
					    receiver_device_cmd,
					    ota_cmd,
					    error))
		return FALSE;

	if (!fu_pxi_receiver_device_set_feature (self,
						 receiver_device_cmd->data,
						 receiver_device_cmd->len,
						 error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_pxi_receiver_device_fw_ota_ini_new_check (FuPxiReceiverDevice *device, GError **error)
{
	FuPxiReceiverDevice *self = FU_PXI_RECEIVER_DEVICE (device);
	guint8 buf[FU_PXI_RECEIVER_DEVICE_OTA_BUF_SZ] = { 0x0 };
	g_autoptr(GByteArray) receiver_device_cmd = g_byte_array_new ();
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();

	/* ota command */
	fu_byte_array_append_uint8 (ota_cmd, 0x1);					/* ota command length */
	fu_byte_array_append_uint8 (ota_cmd, FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW_CHECK);	/* ota command */
	self->sn++;

	/* get pixart wireless module ota command */
	if (!fu_pxi_composite_receiver_cmd (FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW_CHECK,
					    self->sn,
					    FU_PXI_WIRELESS_DEVICE_TARGET_RECEIVER,
					    receiver_device_cmd,
					    ota_cmd, error))
		return FALSE;

	if (!fu_pxi_receiver_device_set_feature (self,
						 receiver_device_cmd->data,
						 receiver_device_cmd->len,
						 error))
		return FALSE;

	/* delay for wireless module device read command */
	g_usleep (5 * 1000);
	buf[0] = PXI_HID_WIRELESS_DEV_OTA_REPORT_ID;
	if (!fu_pxi_receiver_device_get_feature (self, buf, 32, error))
		return FALSE;

	/* shared state */
	return fu_pxi_ota_fw_state_parse (&self->fwstate, buf, sizeof(buf), 0x09, error);
}

static gboolean
fu_pxi_receiver_device_get_cmd_response (FuPxiReceiverDevice *device,
					 guint8 *buf, guint bufsz,
					 GError **error)
{
	guint16 retry = 0;
	while (1) {
		guint8 sn = 0x0;
		memset (buf, 0, bufsz);
		buf[0] = PXI_HID_WIRELESS_DEV_OTA_REPORT_ID;

		g_usleep (5 * 1000);

		if (!fu_pxi_receiver_device_get_feature (device, buf, bufsz, error))
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
				     "reach retry maximum, hid sn fail, "
				     "got 0x%04x, expected 0x%04x",
				     sn, device->sn);
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_pxi_receiver_device_check_crc (FuDevice *device, guint16 checksum, GError **error)
{
	FuPxiReceiverDevice *self = FU_PXI_RECEIVER_DEVICE (device);
	guint8 buf[FU_PXI_RECEIVER_DEVICE_OTA_BUF_SZ] = { 0x0 };
	guint8 status = 0x0;
	g_autoptr(GByteArray) receiver_device_cmd = g_byte_array_new ();
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();

	/* ota check crc command */
	fu_byte_array_append_uint8 (ota_cmd, 0x3);					/* ota command length */
	fu_byte_array_append_uint8 (ota_cmd, FU_PXI_DEVICE_CMD_FW_OTA_CHECK_CRC);	/* ota command */
	fu_byte_array_append_uint16 (ota_cmd, checksum, G_LITTLE_ENDIAN);		/* checkesum */

	/* increase the serial number */
	self->sn++;

	/* get pixart wireless module for ota check crc command */
	if (!fu_pxi_composite_receiver_cmd (FU_PXI_DEVICE_CMD_FW_OTA_CHECK_CRC,
					    self->sn,
					    FU_PXI_WIRELESS_DEVICE_TARGET_RECEIVER,
					    receiver_device_cmd,
					    ota_cmd,
					    error))
		return FALSE;

	if (!fu_pxi_receiver_device_set_feature (self,
						 receiver_device_cmd->data,
						 receiver_device_cmd->len,
						 error))
		return FALSE;

	if (!fu_pxi_receiver_device_get_cmd_response (self, buf, sizeof(buf), error))
		return FALSE;

	if (!fu_common_read_uint8_safe (buf, sizeof(buf), 0x5, &status, error))
		return FALSE;

	if (status ==  OTA_RSP_CODE_ERROR) {
		 g_set_error (error,
			FWUPD_ERROR,
			FWUPD_ERROR_READ,
			"checksum error: expected 0x%04x",
			checksum
		 );
		 return FALSE;
	}
	/* success */
	return TRUE;
}

static gboolean
fu_pxi_receiver_device_fw_object_create (FuDevice *device, FuChunk *chk, GError **error)
{
	FuPxiReceiverDevice *self = FU_PXI_RECEIVER_DEVICE (device);
	guint8 buf[FU_PXI_RECEIVER_DEVICE_OTA_BUF_SZ] = { 0x0 };
	guint8 status = 0x0;
	g_autoptr(GByteArray) receiver_device_cmd = g_byte_array_new ();
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();

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
					    FU_PXI_WIRELESS_DEVICE_TARGET_RECEIVER,
					    receiver_device_cmd,
					    ota_cmd, error))
		return FALSE;

	if (!fu_pxi_receiver_device_set_feature (self,
						 receiver_device_cmd->data,
						 receiver_device_cmd->len,
						 error))
		return FALSE;

	if (!fu_pxi_receiver_device_get_cmd_response (self, buf, sizeof(buf), error))
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
fu_pxi_receiver_device_write_payload (FuDevice *device, FuChunk *chk, GError **error)
{
	FuPxiReceiverDevice *self = FU_PXI_RECEIVER_DEVICE (device);
	guint8 buf[FU_PXI_RECEIVER_DEVICE_OTA_BUF_SZ] = { 0x0 };
	guint8 status = 0x0;
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();
	g_autoptr(GByteArray) receiver_device_cmd = g_byte_array_new ();
	g_autoptr(GTimer) timer = g_timer_new ();

	/* ota write payload command */
	fu_byte_array_append_uint8 (ota_cmd, fu_chunk_get_data_sz (chk));			/* ota command length */
	g_byte_array_append (ota_cmd, fu_chunk_get_data (chk), fu_chunk_get_data_sz (chk));	/* payload content */

	/* increase the serial number */
	self->sn++;

	/* get pixart wireless module for writes payload command */
	if (!fu_pxi_composite_receiver_cmd (FU_PXI_DEVICE_CMD_FW_OTA_PAYLOAD_CONTENT,
					    self->sn,
					    FU_PXI_WIRELESS_DEVICE_TARGET_RECEIVER,
					    receiver_device_cmd,
					    ota_cmd,
					    error))
		return FALSE;
	if (!fu_pxi_receiver_device_set_feature (self,
						 receiver_device_cmd->data,
						 receiver_device_cmd->len,
						 error))
		return FALSE;
	/* get the write payload command response */
	if (!fu_pxi_receiver_device_get_cmd_response (self, buf, sizeof(buf), error))
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
fu_pxi_receiver_device_write_chunk (FuDevice *device, FuChunk *chk, GError **error)
{
	FuPxiReceiverDevice *self = FU_PXI_RECEIVER_DEVICE (device);
	guint32 prn = 0;
	guint16 checksum;
	g_autoptr(GPtrArray) chunks = NULL;

	/* send create fw object command */
	if (!fu_pxi_receiver_device_fw_object_create (device, chk, error))
		return FALSE;

	/* write payload */
	chunks = fu_chunk_array_new (fu_chunk_get_data (chk),
				     fu_chunk_get_data_sz (chk),
				     fu_chunk_get_address (chk),
				     0x0, self->fwstate.mtu_size);

	/* the checksum of chunk */
	checksum = fu_pxi_common_sum16 (fu_chunk_get_data (chk), fu_chunk_get_data_sz (chk));
	self->fwstate.checksum += checksum;
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk2 = g_ptr_array_index (chunks, i);
		if (!fu_pxi_receiver_device_write_payload (device, chk2, error))
			return FALSE;
		prn++;
		/* check crc at fw when PRN over threshold write or
		 * offset reach max object sz or write offset reach fw length */
		if (prn >= self->fwstate.prn_threshold || i == chunks->len - 1) {
			if (!fu_pxi_receiver_device_check_crc (device,
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
fu_pxi_receiver_device_fw_upgrade (FuDevice *device, FuFirmware *firmware, GError **error)
{
	FuPxiReceiverDevice *self = FU_PXI_RECEIVER_DEVICE (device);
	const gchar *version;
	const guint8 *buf;
	gsize bufsz = 0;
	guint16 checksum = 0x0;
	guint8 fw_version[5] = { 0x0 };
	guint8 res[FU_PXI_RECEIVER_DEVICE_OTA_BUF_SZ] = { 0x0 };
	guint8 result = 0x0;
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();
	g_autoptr(GByteArray) receiver_device_cmd = g_byte_array_new ();
	g_autoptr(GBytes) fw = NULL;

	fw = fu_firmware_get_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	buf = g_bytes_get_data (fw, &bufsz);
	checksum = fu_pxi_common_sum16 (buf, bufsz);

	/* ota fw upgrade command */
	fu_byte_array_append_uint8 (ota_cmd, 0x0c);				/* ota fw upgrade command length */
	fu_byte_array_append_uint8 (ota_cmd, FU_PXI_DEVICE_CMD_FW_UPGRADE);	/* ota fw upgrade command opccode */
	fu_byte_array_append_uint32 (ota_cmd, bufsz, G_LITTLE_ENDIAN);		/* ota fw upgrade command fw size */
	fu_byte_array_append_uint16 (ota_cmd, checksum, G_LITTLE_ENDIAN);	/* ota fw upgrade command checksum */

	version = fu_firmware_get_version (firmware);
	if (!fu_memcpy_safe (fw_version, sizeof(fw_version), 0x0,	/* dst */
			     (guint8 *) version, strlen (version), 0x0,	/* src */
			     sizeof(fw_version), error))
		return FALSE;

	g_byte_array_append (ota_cmd, fw_version, sizeof(fw_version));
	fu_common_dump_raw (G_LOG_DOMAIN, "ota_cmd ", ota_cmd->data, ota_cmd->len);
	self->sn++;

	/* get pixart wireless module ota command */
	if (!fu_pxi_composite_receiver_cmd (FU_PXI_DEVICE_CMD_FW_UPGRADE,
					    self->sn,
					    FU_PXI_WIRELESS_DEVICE_TARGET_RECEIVER,
					    receiver_device_cmd,
					    ota_cmd,
					    error))
		return FALSE;

	/* update device status */
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_VERIFY);
	/* send ota fw upgrade command */
	if (!fu_pxi_receiver_device_set_feature (self,
						 receiver_device_cmd->data,
						 receiver_device_cmd->len,
						 error))
		return FALSE;

	/* delay for wireless module device read command */
	g_usleep (5 * 1000);

	if (!fu_pxi_receiver_device_get_cmd_response (self, res, sizeof(res), error))
		return FALSE;

	if (!fu_common_read_uint8_safe (res, sizeof(res), 0x5, &result, error))
		return FALSE;
	if (result != OTA_RSP_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "cmd rsp check fail: %s [0x%02x]",
			     fu_pxi_receiver_cmd_result_to_string (result),
			     result);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pxi_receiver_device_reset (FuDevice *device, GError **error)
{
	FuPxiReceiverDevice *self = FU_PXI_RECEIVER_DEVICE (device);
	g_autoptr(GByteArray) receiver_device_cmd = g_byte_array_new ();
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();

	/* ota mcu reset command */
	fu_byte_array_append_uint8 (ota_cmd, 0x1);				/* ota mcu reset command */
	fu_byte_array_append_uint8 (ota_cmd, FU_PXI_DEVICE_CMD_FW_MCU_RESET);	/* ota mcu reset command op code */
	fu_byte_array_append_uint8 (ota_cmd, OTA_RESET);			/* ota mcu reset command reason */

	self->sn++;
	/* get pixart wireless module ota command */
	if (!fu_pxi_composite_receiver_cmd (FU_PXI_DEVICE_CMD_FW_MCU_RESET,
					    self->sn,
					    FU_PXI_WIRELESS_DEVICE_TARGET_RECEIVER,
					    receiver_device_cmd,
					    ota_cmd,
					    error))
		return FALSE;

	/* update device status */
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_RESTART);

	/* send ota mcu reset command */
	return fu_pxi_receiver_device_set_feature (self,
						   receiver_device_cmd->data,
						   receiver_device_cmd->len,
						   error);
}

static FuFirmware *
fu_pxi_receiver_device_prepare_firmware (FuDevice *device,
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
fu_pxi_receiver_device_write_firmware (FuDevice *device,
				       FuFirmware *firmware,
				       FwupdInstallFlags flags,
				       GError **error)
{
	FuPxiReceiverDevice *self = FU_PXI_RECEIVER_DEVICE (device);
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* get the default image */
	fw = fu_firmware_get_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	/* send fw ota init command */
	if (!fu_pxi_receiver_device_fw_ota_init_new (self, g_bytes_get_size (fw), error))
		return FALSE;

	if (!fu_pxi_receiver_device_fw_ota_ini_new_check (self ,error))
		return FALSE;

	chunks = fu_chunk_array_new_from_bytes (fw, 0x0, 0x0, FU_PXI_DEVICE_OBJECT_SIZE_MAX);
	/* prepare write fw into device */
	self->fwstate.offset = 0;
	self->fwstate.checksum = 0;

	/* write fw into device */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	for (guint i = self->fwstate.offset; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		if (!fu_pxi_receiver_device_write_chunk (device, chk, error))
			return FALSE;
		fu_device_set_progress_full (device, (gsize) i, (gsize) chunks->len);
	}

	/* fw upgrade command */
	if (!fu_pxi_receiver_device_fw_upgrade (device, firmware, error))
		return FALSE;

	/* delay for wireless module device read command */
	g_usleep (5 * 1000);

	/* send device reset command */
	return fu_pxi_receiver_device_reset (device, error);
}

static gboolean
fu_pxi_receiver_device_get_peripheral_info (FuPxiReceiverDevice *device,
					    struct ota_fw_dev_model *model,
					    GError **error)
{
	guint8 buf[FU_PXI_RECEIVER_DEVICE_OTA_BUF_SZ] = { 0x0 };
	guint16 checksum = 0;
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();
	g_autoptr(GByteArray) receiver_device_cmd = g_byte_array_new ();

	fu_byte_array_append_uint8 (ota_cmd, 0x1);	/* ota init new command length */
	fu_byte_array_append_uint8 (ota_cmd, FU_PXI_DEVICE_CMD_FW_OTA_GET_MODEL);
	device->sn++;

	if (!fu_pxi_composite_receiver_cmd (FU_PXI_DEVICE_CMD_FW_OTA_GET_MODEL,
					    device->sn,
					    FU_PXI_WIRELESS_DEVICE_TARGET_RECEIVER,
					    receiver_device_cmd,
					    ota_cmd,
					    error))
		return FALSE;
	if (!fu_pxi_receiver_device_set_feature (device,
						 receiver_device_cmd->data,
						 receiver_device_cmd->len,
						 error))
		return FALSE;

	g_usleep (5 * 1000);
	buf[0] = PXI_HID_WIRELESS_DEV_OTA_REPORT_ID;

	if (!fu_pxi_receiver_device_get_feature (device, buf, sizeof(buf), error))
		return FALSE;

	fu_common_dump_raw (G_LOG_DOMAIN, "model_info", buf, 96);

	if (!fu_common_read_uint8_safe (buf, sizeof(buf), 0x9,
					&model->status, error))
		return FALSE;

	if (!fu_memcpy_safe (model->name, FU_PXI_DEVICE_MODEL_NAME_LEN, 0x0,	/* dst */
			     buf ,FU_PXI_RECEIVER_DEVICE_OTA_BUF_SZ, 0xa,	/* src */
			     FU_PXI_DEVICE_MODEL_NAME_LEN, error))
		return FALSE;

	if (!fu_common_read_uint8_safe (buf, sizeof(buf), 0x16,
					&model->type, error))
		return FALSE;
	if (!fu_common_read_uint8_safe (buf, sizeof(buf), 0x17,
					&model->target, error))
		return FALSE;

	if (!fu_memcpy_safe (model->version, 5, 0x0,	/* dst */
			     buf ,sizeof(buf), 0x18,		/* src */
			     5, error))
		return FALSE;

	if (!fu_common_read_uint16_safe (buf, sizeof(buf), 0x1D,
					 &checksum, G_LITTLE_ENDIAN, error))

		return FALSE;

	/* set current version and model name */
	model->checksum = checksum;
	if (g_getenv ("FWUPD_PIXART_RF_VERBOSE") != NULL) {
		g_autofree gchar *version_str = g_strndup ((gchar *) model->version, 5);
		g_debug ("checksum %x", model->checksum);
		g_debug ("version_str %s", version_str);
	}

	return TRUE;
}

static gboolean
fu_pxi_receiver_device_get_peripheral_num (FuPxiReceiverDevice *device, guint8 *num_of_models, GError **error)
{
	FuPxiReceiverDevice *self = FU_PXI_RECEIVER_DEVICE (device);
	guint8 buf[FU_PXI_RECEIVER_DEVICE_OTA_BUF_SZ] = { 0x0 };
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();
	g_autoptr(GByteArray) receiver_device_cmd = g_byte_array_new ();

	fu_byte_array_append_uint8 (ota_cmd, 0x1);						/* ota init new command length */
	fu_byte_array_append_uint8 (ota_cmd, FU_PXI_DEVICE_CMD_FW_OTA_GET_NUM_OF_MODELS);	/* ota ota init new op code */

	self->sn++;
	if (!fu_pxi_composite_receiver_cmd (FU_PXI_DEVICE_CMD_FW_OTA_GET_NUM_OF_MODELS,
					    self->sn,
					    FU_PXI_WIRELESS_DEVICE_TARGET_RECEIVER,
					    receiver_device_cmd,
					    ota_cmd, error))
		return FALSE;
	if (!fu_pxi_receiver_device_set_feature (self,
						 receiver_device_cmd->data,
						 receiver_device_cmd->len,
						 error))
		return FALSE;

	g_usleep (5 * 1000);

	buf[0] = PXI_HID_WIRELESS_DEV_OTA_REPORT_ID;
	if (!fu_pxi_receiver_device_get_feature (device, buf, sizeof(buf), error))
		return FALSE;
	if (g_getenv ("FWUPD_PIXART_RF_VERBOSE") != NULL) {
		fu_common_dump_raw (G_LOG_DOMAIN, "buf from get model num",
				    buf, sizeof(buf));
	}
	if (!fu_common_read_uint8_safe (buf, sizeof(buf), 0xA,
					num_of_models, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_receiver_device_add_peripherals (FuPxiReceiverDevice *device, guint idx, GError **error)
{
#ifdef HAVE_HIDRAW_H
	struct ota_fw_dev_model model = { 0x0 };
	g_autofree gchar *instance_id = NULL;
	g_autofree gchar *model_name = NULL;
	g_autofree gchar *model_version = NULL;

	/* get the all wireless peripherals info */
	if (!fu_pxi_receiver_device_get_peripheral_info (device, &model, error))
		return FALSE;
	model_version = g_strndup ((gchar *) model.version, 5);
	model_name = g_strndup ((gchar *) model.name, FU_PXI_DEVICE_MODEL_NAME_LEN);
	instance_id = g_strdup_printf ("HIDRAW\\VEN_%04X&DEV_%04X&MODEL_%s",
				       device->vendor,
				       device->product,
				       model_name);
	if (model.type == OTA_WIRELESS_MODULE_TYPE_RECEIVER) {
		fu_device_set_version (FU_DEVICE (device), model_version);
		fu_device_add_guid (FU_DEVICE (device), instance_id);
	} else {
		g_autoptr(FuPxiWirelessDevice) wireless_device = fu_pxi_wireless_device_new (&model);
		g_autofree gchar *logical_id = g_strdup_printf ("IDX:0x%02x", idx);
		fu_device_add_guid (FU_DEVICE (wireless_device), instance_id);
		fu_device_set_name (FU_DEVICE (wireless_device), model_name);
		fu_device_set_version (FU_DEVICE (wireless_device), model_version);
		fu_device_set_logical_id (FU_DEVICE (wireless_device), logical_id);
		fu_device_add_child (FU_DEVICE (device), FU_DEVICE (wireless_device));
	}
	return TRUE;
#else
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "<linux/hidraw.h> not available");
	return FALSE;
#endif
}

static gboolean
fu_pxi_receiver_device_setup_guid (FuPxiReceiverDevice *device, GError **error)
{
#ifdef HAVE_HIDRAW_H
	struct hidraw_devinfo hid_raw_info = { 0x0 };
	g_autofree gchar *devid = NULL;
	g_autoptr(GString) dev_name = NULL;

	/* extra GUID with device name */
	if (!fu_pxi_receiver_device_get_raw_info (device, &hid_raw_info ,error))
		return FALSE;

	device->vendor = (guint) hid_raw_info.vendor;
	device->product = (guint) hid_raw_info.product;

	dev_name = g_string_new (fu_device_get_name (FU_DEVICE (device)));
	g_string_ascii_up (dev_name);
	fu_common_string_replace (dev_name, " ", "_");
	devid = g_strdup_printf ("HIDRAW\\VEN_%04X&DEV_%04X&NAME_%s",
				 (guint) hid_raw_info.vendor,
				 (guint) hid_raw_info.product,
				 dev_name->str);
	fu_device_add_instance_id (FU_DEVICE (device), devid);
	return TRUE;
#else
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "<linux/hidraw.h> not available");
	return FALSE;
#endif
}

static gboolean
fu_pxi_receiver_device_check_peripherals (FuPxiReceiverDevice *device, GError **error)
{
	guint8 num = 0;

	/* add wireless peripherals */
	if (!fu_pxi_receiver_device_get_peripheral_num (device, &num, error))
		return FALSE;
	if (g_getenv ("FWUPD_PIXART_RF_VERBOSE") != NULL)
		g_debug ("peripheral num: %u", num);
	for (guint8 idx = 0; idx < num; idx++) {
		if (!fu_pxi_receiver_device_add_peripherals (device, idx, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_pxi_receiver_device_setup (FuDevice *device, GError **error)
{
	FuPxiReceiverDevice *self = FU_PXI_RECEIVER_DEVICE(device);

	if (!fu_pxi_receiver_device_setup_guid (self ,error)) {
		g_prefix_error (error, "failed to setup GUID: ");
		return FALSE;
	}
	if (!fu_pxi_receiver_device_fw_ota_init_new (self, 0x0000, error)) {
		g_prefix_error (error, "failed to OTA init new: ");
		return FALSE;
	}
	if (!fu_pxi_receiver_device_fw_ota_ini_new_check (self ,error)) {
		g_prefix_error (error, "failed to OTA init new check: ");
		return FALSE;
	}
	if (!fu_pxi_receiver_device_check_peripherals (self ,error)) {
		g_prefix_error (error, "failed to add wireless module: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pxi_receiver_device_probe (FuDevice *device, GError **error)
{
	/* set the logical and physical ID */
	if (!fu_udev_device_set_logical_id (FU_UDEV_DEVICE (device), "hid", error))
		return FALSE;
	return fu_udev_device_set_physical_id (FU_UDEV_DEVICE (device), "hid", error);
}

static void
fu_pxi_receiver_device_init (FuPxiReceiverDevice *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_vendor_id (FU_DEVICE (self), "USB:0x093A");
	fu_device_add_protocol (FU_DEVICE (self), "com.pixart.rf");
}

static void
fu_pxi_receiver_device_class_init (FuPxiReceiverDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->to_string = fu_pxi_receiver_device_to_string;
	klass_device->setup = fu_pxi_receiver_device_setup;
	klass_device->probe = fu_pxi_receiver_device_probe;
	klass_device->write_firmware = fu_pxi_receiver_device_write_firmware;
	klass_device->prepare_firmware = fu_pxi_receiver_device_prepare_firmware;
}
