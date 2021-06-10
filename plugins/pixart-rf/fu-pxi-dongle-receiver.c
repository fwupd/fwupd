/*
 * Copyright (C) 2020 Jimmy Yu <Jimmy_yu@pixart.com>
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */
#include "config.h"

#ifdef HAVE_HIDRAW_H
#include <linux/hidraw.h>
#include <linux/input.h>
#endif

#include "fu-firmware.h"
#include "fu-common.h"
#include "fu-chunk.h"
#include "fu-pxi-dongle-receiver.h"
#include "fu-pxi-wireless-peripheral.h"
#include "fu-pxi-firmware.h"
#include "fu-pxi-device.h"
#include "fu-pxi-common.h"

struct _FuPxiDongleReceiver {
	FuUdevDevice	 	parent_instance;
	guint8		 	status;
	guint8		 	new_flow;
	guint16		offset;
	guint16		checksum;
	guint32		max_object_size;
	guint16		mtu_size;
	guint16		prn_threshold;
	guint8		 	spec_check_result;
	guint8			sn;
	guint			vendor;
	guint			product;
};

G_DEFINE_TYPE (FuPxiDongleReceiver, fu_pxi_dongle_receiver, FU_TYPE_UDEV_DEVICE)

#ifdef HAVE_HIDRAW_H
static gboolean
fu_pxi_dongle_receiver_get_raw_info (FuPxiDongleReceiver *self, struct hidraw_devinfo *info, GError **error)
{
	if (!fu_udev_device_ioctl (FU_UDEV_DEVICE (self),
				   HIDIOCGRAWINFO, (guint8 *) info,
				   NULL, error)) {
		return FALSE;
	}
	return TRUE;
}
#endif

static gboolean
fu_pxi_dongle_receiver_set_feature (FuPxiDongleReceiver *self, const guint8 *buf, guint bufsz, GError **error)
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
fu_pxi_dongle_receiver_get_feature (FuPxiDongleReceiver *self, guint8 *buf, guint bufsz, GError **error)
{
#ifdef HAVE_HIDRAW_H
	if (!fu_udev_device_ioctl (FU_UDEV_DEVICE (self),
				   HIDIOCGFEATURE(bufsz), buf,
				   NULL, error)) {
		return FALSE;
	}
	if (g_getenv ("FWUPD_PIXART_RF_VERBOSE") != NULL) {
		fu_common_dump_raw (G_LOG_DOMAIN, "GetFeature", buf, bufsz);
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

static void
fu_pxi_dongle_receiver_to_string (FuDevice *device, guint idt, GString *str)
{
	FuPxiDongleReceiver *self = FU_PXI_DONGLE_RECEIVER (device);
	fu_common_string_append_kx (str, idt, "Status", self->status);
	fu_common_string_append_kx (str, idt, "NewFlow", self->new_flow);
	fu_common_string_append_kx (str, idt, "CurrentObjectOffset", self->offset);
	fu_common_string_append_kx (str, idt, "CurrentChecksum", self->checksum);
	fu_common_string_append_kx (str, idt, "MaxObjectSize", self->max_object_size);
	fu_common_string_append_kx (str, idt, "MtuSize", self->mtu_size);
	fu_common_string_append_kx (str, idt, "PacketReceiptNotificationThreshold", self->prn_threshold);
	fu_common_string_append_kx (str, idt, "Vendor", self->vendor);
	fu_common_string_append_kx (str, idt, "Product", self->product);
}

static gboolean
fu_pxi_dongle_receiver_fw_ota_init_new (FuPxiDongleReceiver *device, gsize bufsz, GError **error)
{
	guint8 fw_version[10] = { 0x0 };
	g_autoptr(GByteArray) dongle_receiver_cmd = g_byte_array_new ();
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();
	FuPxiDongleReceiver *self = FU_PXI_DONGLE_RECEIVER (device);

	fu_byte_array_append_uint8 (ota_cmd, 0X06);					/* ota init new command length */
	fu_byte_array_append_uint8 (ota_cmd, FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW);	/* ota ota init new op code */
	fu_byte_array_append_uint32 (ota_cmd, bufsz, G_LITTLE_ENDIAN);			/* fw size */
	fu_byte_array_append_uint8 (ota_cmd, 0x0);					/* ota setting */
	g_byte_array_append (ota_cmd, fw_version, sizeof(fw_version));			/* ota version */

	self->sn++;
	if (!fu_pxi_common_composite_dongle_cmd (FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW,
							self->sn,
							FU_PXI_WIRELESS_PERIPHERAL_TARGET_DONGLE,
							dongle_receiver_cmd,
							ota_cmd, error))
		return FALSE;

	if (!fu_pxi_dongle_receiver_set_feature (self, dongle_receiver_cmd->data, dongle_receiver_cmd->len, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_pxi_dongle_receiver_fw_ota_ini_new_check (FuPxiDongleReceiver *device, GError **error)
{

	g_autoptr(GByteArray) dongle_receiver_cmd = g_byte_array_new ();
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();
	guint8 res[FU_PXI_DONGLE_RECEIVER_OTA_BUF_SZ] = { 0x0 };
	FuPxiDongleReceiver *self = FU_PXI_DONGLE_RECEIVER (device);

	/* ota command */
	fu_byte_array_append_uint8 (ota_cmd, 0x1);					/* ota command length */
	fu_byte_array_append_uint8 (ota_cmd, FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW_CHECK);	/* ota command */
	self->sn++;

	/* get pixart wireless module ota command */
	if (!fu_pxi_common_composite_dongle_cmd (FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW_CHECK,
							self->sn,
							FU_PXI_WIRELESS_PERIPHERAL_TARGET_DONGLE,
							dongle_receiver_cmd,
							ota_cmd, error))
		return FALSE;

	if (!fu_pxi_dongle_receiver_set_feature (self, dongle_receiver_cmd->data,
							dongle_receiver_cmd->len, error))
		return FALSE;


	/* delay for wireless module device read command */
	g_usleep (5 * 1000);

	memset (res, 0, sizeof(res));
	res[0] = PXI_HID_WIRELESS_DEV_OTA_REPORT_ID;
	if (!fu_pxi_dongle_receiver_get_feature (self, res, 32, error))
		return FALSE;

	/* shared state */
	if (!fu_common_read_uint8_safe (res, sizeof(res), 0x3 + 0x6,
					&self->status, error))
		return FALSE;
	if (!fu_common_read_uint8_safe (res, sizeof(res), 0x4 + 0x6,
					&self->new_flow, error))
		return FALSE;
	if (!fu_common_read_uint16_safe (res, sizeof(res), 0x5 + 0x6,
					 &self->offset, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint16_safe (res, sizeof(res), 0x7 + 0x6,
					 &self->checksum, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint32_safe (res, sizeof(res), 0x9 + 0x6,
					 &self->max_object_size, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint16_safe (res, sizeof(res), 0xd + 0x6,
					 &self->mtu_size, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint16_safe (res, sizeof(res), 0xf + 0x6,
					 &self->prn_threshold, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint8_safe (res, sizeof(res), 0x11 + 0x6,
					&self->spec_check_result, error))
		return FALSE;


	return TRUE;
}

static gboolean
fu_pxi_dongle_receiver_get_cmd_response (FuPxiDongleReceiver *device, guint8 *res, guint sz, GError **error)
{

	guint16 retry = 0;
	while (1)
	{
		guint8 sn = 0x0;
		memset (res, 0, sz);
		res[0] = PXI_HID_WIRELESS_DEV_OTA_REPORT_ID;

		g_usleep (5 * 1000);

		if (!fu_pxi_dongle_receiver_get_feature (device, res, sz, error))
			return FALSE;

		if (!fu_common_read_uint8_safe (res, sizeof(res), 0x4, &sn, error))
			return FALSE;

		if (device->sn != sn)
			retry++;
		else
			break;

		if (retry == FU_PXI_WIRELESS_PERIPHERAL_RETRY_MAXIMUM) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_READ,
				     "reach retry maximum ,hid sn fail, got 0x%04x, expected 0x%04x",
				     sn,
				     device->sn);
			return FALSE;
		}

	}
	return TRUE;
}

static gboolean
fu_pxi_dongle_receiver_check_crc (FuDevice *device, guint16 checksum, GError **error)
{
	FuPxiDongleReceiver *self = FU_PXI_DONGLE_RECEIVER (device);
	g_autoptr(GByteArray) dongle_receiver_cmd = g_byte_array_new ();
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();
	guint8 res[FU_PXI_DONGLE_RECEIVER_OTA_BUF_SZ] = { 0x0 };

	guint8 status = 0x0;

	/* ota check crc command */
	fu_byte_array_append_uint8 (ota_cmd, 0x3);					/* ota command length */
	fu_byte_array_append_uint8 (ota_cmd, FU_PXI_DEVICE_CMD_FW_OTA_CHECK_CRC);	/* ota command */
	fu_byte_array_append_uint16 (ota_cmd, checksum, G_LITTLE_ENDIAN);		/* checkesum */

	/* increase the serial number */
	self->sn++;

	/* get pixart wireless module for ota check crc command */
	if (!fu_pxi_common_composite_dongle_cmd (FU_PXI_DEVICE_CMD_FW_OTA_CHECK_CRC,
							self->sn,
							FU_PXI_WIRELESS_PERIPHERAL_TARGET_DONGLE,
							dongle_receiver_cmd,
							ota_cmd, error))
		return FALSE;

	if (!fu_pxi_dongle_receiver_set_feature (self, dongle_receiver_cmd->data, dongle_receiver_cmd->len, error))
		return FALSE;

	if (!fu_pxi_dongle_receiver_get_cmd_response (self, res, sizeof(res), error))
		return FALSE;

	if (!fu_common_read_uint8_safe (res, sizeof(res), 0x5, &status, error))
		return FALSE;

	if(status ==  OTA_RSP_CODE_ERROR) {
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
fu_pxi_dongle_receiver_fw_object_create (FuDevice *device, FuChunk *chk, GError **error)
{
	g_autoptr(GByteArray) dongle_receiver_cmd = g_byte_array_new ();
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();
	FuPxiDongleReceiver *self = FU_PXI_DONGLE_RECEIVER (device);
	guint8 res[FU_PXI_DONGLE_RECEIVER_OTA_BUF_SZ] = { 0x0 };
	guint8 status = 0x0;
	/* ota object create command */
	fu_byte_array_append_uint8 (ota_cmd, 0x9);					/* ota command length */
	fu_byte_array_append_uint8 (ota_cmd, FU_PXI_DEVICE_CMD_FW_OBJECT_CREATE);	/* ota command */
	fu_byte_array_append_uint32 (ota_cmd, fu_chunk_get_address (chk), G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32 (ota_cmd, fu_chunk_get_data_sz (chk), G_LITTLE_ENDIAN);

	/* increase the serial number */
	self->sn++;

	/* get pixart wireless module for ota object create command */
	if (!fu_pxi_common_composite_dongle_cmd (FU_PXI_DEVICE_CMD_FW_OBJECT_CREATE,
							self->sn,
							FU_PXI_WIRELESS_PERIPHERAL_TARGET_DONGLE,
							dongle_receiver_cmd,
							ota_cmd, error))
		return FALSE;

	if (!fu_pxi_dongle_receiver_set_feature (self, dongle_receiver_cmd->data, dongle_receiver_cmd->len, error))
		return FALSE;

	if (!fu_pxi_dongle_receiver_get_cmd_response (self, res, sizeof(res), error))
		return FALSE;

	if (!fu_common_read_uint8_safe (res, sizeof(res), 0x5, &status, error))
		return FALSE;

	if(status != OTA_RSP_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "cmd rsp check fail: %s [0x%02x]",
			     fu_pxi_common_dongle_cmd_result_to_string (status),
			     status);
		return FALSE;

	}
	/* success */
	return TRUE;
}

static gboolean
fu_pxi_dongle_receiver_write_payload (FuDevice *device, FuChunk *chk, GError **error)
{
	g_autoptr(GByteArray) dongle_receiver_cmd = g_byte_array_new ();
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();
	g_autoptr(GTimer) timer = g_timer_new ();
	FuPxiDongleReceiver *self = FU_PXI_DONGLE_RECEIVER (device);
	guint8 res[FU_PXI_DONGLE_RECEIVER_OTA_BUF_SZ] = { 0x0 };
	guint8 status = 0x0;

	/* ota write payload command */
	fu_byte_array_append_uint8 (ota_cmd, fu_chunk_get_data_sz (chk));			/* ota command length */
	g_byte_array_append (ota_cmd, fu_chunk_get_data (chk), fu_chunk_get_data_sz (chk));	/* payload content */

	/* increase the serial number */
	self->sn++;

	/* get pixart wireless module for writet payload command */
	if (!fu_pxi_common_composite_dongle_cmd (FU_PXI_DEVICE_CMD_FW_OTA_PAYLOPD_CONTENT,
							self->sn,
							FU_PXI_WIRELESS_PERIPHERAL_TARGET_DONGLE,
							dongle_receiver_cmd,
							ota_cmd, error))
		return FALSE;
	if (!fu_pxi_dongle_receiver_set_feature (self, dongle_receiver_cmd->data, dongle_receiver_cmd->len, error))
		return FALSE;
	/* get the write payload command respose */
	if (!fu_pxi_dongle_receiver_get_cmd_response (self, res, sizeof(res), error))
		return FALSE;

	if(status != OTA_RSP_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "cmd rsp check fail: %s [0x%02x]",
			     fu_pxi_common_dongle_cmd_result_to_string (status),
			     status);
		return FALSE;
	}
	/* success */
	return TRUE;
}

static gboolean
fu_pxi_dongle_receiver_write_chunk (FuDevice *device, FuChunk *chk, GError **error)
{
	guint32 prn = 0;
	guint16 checksum;
	g_autoptr(GPtrArray) chunks = NULL;
	FuPxiDongleReceiver *self = FU_PXI_DONGLE_RECEIVER (device);
	/* send create fw object command */
	if (!fu_pxi_dongle_receiver_fw_object_create (device, chk, error))
		return FALSE;

	/* write payload */
	chunks = fu_chunk_array_new (fu_chunk_get_data (chk),
				     fu_chunk_get_data_sz (chk),
				     fu_chunk_get_address (chk),
				     0x0, self->mtu_size);

	/* the checksum of chunk */
	checksum = fu_pxi_common_calculate_16bit_checksum (fu_chunk_get_data (chk),
						     fu_chunk_get_data_sz (chk));
	self->checksum += checksum;
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk2 = g_ptr_array_index (chunks, i);
		if (!fu_pxi_dongle_receiver_write_payload (device, chk2, error))
			return FALSE;
		prn++;
		/* check crc at fw when PRN over threshold write or
		 * offset reach max object sz or write offset reach fw length */
		if (prn >= self->prn_threshold || i == chunks->len - 1) {
			if (!fu_pxi_dongle_receiver_check_crc (device, self->checksum, error))
				return FALSE;
			prn = 0;
		}
	}
	/* success */
	return TRUE;
}


static gboolean
fu_pxi_dongle_receiver_fw_upgrade (FuDevice *device, FuFirmware *firmware, GError **error)
{
	const gchar *version;
	const guint8 *buf;
	gsize bufsz = 0;
	guint8 fw_version[5] = { 0x0 };
	guint16 checksum = 0x0;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GByteArray) dongle_receiver_cmd = g_byte_array_new ();
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();
	FuPxiDongleReceiver *self = FU_PXI_DONGLE_RECEIVER (device);
	guint8 res[FU_PXI_DONGLE_RECEIVER_OTA_BUF_SZ] = { 0x0 };
	guint8 result = 0x0;

	fw = fu_firmware_get_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;


	buf = g_bytes_get_data (fw, &bufsz);
	checksum = fu_pxi_common_calculate_16bit_checksum (buf, bufsz);

	/* ota fw upgrade command */
	fu_byte_array_append_uint8 (ota_cmd, 0x0c);					/* ota fw upgrade command length */
	fu_byte_array_append_uint8 (ota_cmd, FU_PXI_DEVICE_CMD_FW_UPGRADE);		/* ota fw upgrade command opccode */
	fu_byte_array_append_uint32 (ota_cmd, bufsz, G_LITTLE_ENDIAN);		/* ota fw upgrade command fw size */
	fu_byte_array_append_uint16 (ota_cmd, checksum, G_LITTLE_ENDIAN);		/* ota fw upgrade command checksum */

	version = fu_firmware_get_version (firmware);
	if (!fu_memcpy_safe (fw_version, sizeof(fw_version), 0x0,		/* dst */
			     (guint8 *) version, strlen (version), 0x0,	/* src */
			     sizeof(fw_version), error))
		return FALSE;

	g_byte_array_append (ota_cmd, fw_version, sizeof(fw_version));
	fu_common_dump_raw (G_LOG_DOMAIN, "ota_cmd ", ota_cmd->data, ota_cmd->len);
	self->sn++;
	/* get pixart wireless module ota command */
	if (!fu_pxi_common_composite_dongle_cmd (FU_PXI_DEVICE_CMD_FW_UPGRADE,
							self->sn,
							FU_PXI_WIRELESS_PERIPHERAL_TARGET_DONGLE,
							dongle_receiver_cmd,
							ota_cmd, error))
		return FALSE;

	/* update device status */
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_VERIFY);
	/* send ota  fw upgrade command */
	if (!fu_pxi_dongle_receiver_set_feature (self, dongle_receiver_cmd->data,
							dongle_receiver_cmd->len, error))
		return FALSE;

	/* delay for wireless module device read command */
	g_usleep (5 * 1000);

	if (!fu_pxi_dongle_receiver_get_cmd_response (self, res, sizeof(res), error))
		return FALSE;

	if (!fu_common_read_uint8_safe (res, sizeof(res), 0x5, &result, error))
		return FALSE;

	if (result != OTA_RSP_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "cmd rsp check fail: %s [0x%02x]",
			     fu_pxi_common_dongle_cmd_result_to_string (result),
			     result);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pxi_dongle_receiver_reset (FuDevice *device, GError **error)
{
	g_autoptr(GByteArray) dongle_receiver_cmd = g_byte_array_new ();
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();
	FuPxiDongleReceiver *self = FU_PXI_DONGLE_RECEIVER (device);
	/* ota mcu reset command */
	fu_byte_array_append_uint8 (ota_cmd, 0x1);					/* ota mcu reset command */
	fu_byte_array_append_uint8 (ota_cmd, FU_PXI_DEVICE_CMD_FW_MCU_RESET);		/* ota mcu reset command op code */
	fu_byte_array_append_uint8 (ota_cmd, OTA_RESET);				/* ota mcu reset command reason */

	self->sn++;
	/* get pixart wireless module ota command */
	if (!fu_pxi_common_composite_dongle_cmd (FU_PXI_DEVICE_CMD_FW_MCU_RESET,
							self->sn,
							FU_PXI_WIRELESS_PERIPHERAL_TARGET_DONGLE,
							dongle_receiver_cmd,
							ota_cmd, error))
		return FALSE;
	/* update device status */
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_RESTART);

	/* send ota mcu reset command */
	if (!fu_pxi_dongle_receiver_set_feature (self, dongle_receiver_cmd->data,
							dongle_receiver_cmd->len, error))
		return FALSE;
	/* success */
	return TRUE;
}

static FuFirmware *
fu_pxi_dongle_receiver_prepare_firmware (FuDevice *device,
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
fu_pxi_dongle_receiver_write_firmware (FuDevice *device,
				   FuFirmware *firmware,
				   FwupdInstallFlags flags,
				   GError **error)
{
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	FuPxiDongleReceiver *self = FU_PXI_DONGLE_RECEIVER (device);

	/* get the default image */
	fw = fu_firmware_get_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	/* send fw ota init command */
	if (!fu_pxi_dongle_receiver_fw_ota_init_new (self, g_bytes_get_size (fw), error))
		return FALSE;

	if (!fu_pxi_dongle_receiver_fw_ota_ini_new_check (self ,error))
		return FALSE;

	chunks = fu_chunk_array_new_from_bytes (fw, 0x0, 0x0, FU_PXI_DEVICE_OBJECT_SIZE_MAX);
	/* prepare write fw into device */
	self->offset = 0;
	self->checksum = 0;

	/* write fw into device */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	for (guint i = self->offset; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		if (!fu_pxi_dongle_receiver_write_chunk (device, chk, error))
			return FALSE;
		fu_device_set_progress_full (device, (gsize) i, (gsize) chunks->len);
	}
	/* fw upgrade command */
	if (!fu_pxi_dongle_receiver_fw_upgrade (device, firmware, error))
		return FALSE;
	/* delay for wireless module device read command */
	g_usleep (5 * 1000);
	/* send device reset command */
	return fu_pxi_dongle_receiver_reset (device, error);
}

static gboolean
fu_pxi_dongle_receiver_get_peripheral_info (FuPxiDongleReceiver *device, struct ota_fw_dev_model *model, GError **error)
{
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();
	g_autoptr(GByteArray) dongle_receiver_cmd = g_byte_array_new ();
	guint8 res[FU_PXI_DONGLE_RECEIVER_OTA_BUF_SZ] = { 0x0 };
	guint16 checksum = 0;
	g_autofree gchar *version_str = NULL;
	dongle_receiver_cmd = g_byte_array_new ();

	fu_byte_array_append_uint8 (ota_cmd, 0x1);						/* ota init new command length */
	fu_byte_array_append_uint8 (ota_cmd, FU_PXI_DEVICE_CMD_FW_OTA_GET_MODEL);
	device->sn++;

	if (!fu_pxi_common_composite_dongle_cmd (FU_PXI_DEVICE_CMD_FW_OTA_GET_MODEL,
				device->sn,
				FU_PXI_WIRELESS_PERIPHERAL_TARGET_DONGLE,
				dongle_receiver_cmd,
				ota_cmd, error))
		return FALSE;
	if (!fu_pxi_dongle_receiver_set_feature (device, dongle_receiver_cmd->data,
							dongle_receiver_cmd->len, error))
		return FALSE;

	g_usleep (5 * 1000);
	memset (res, 0, sizeof(res));
	res[0] = PXI_HID_WIRELESS_DEV_OTA_REPORT_ID;

	if (!fu_pxi_dongle_receiver_get_feature (device, res, sizeof(res), error))
		return FALSE;

	fu_common_dump_raw (G_LOG_DOMAIN, "model_info", res, 96);

	if (!fu_common_read_uint8_safe (res, sizeof(res), 0x9,
					&model->status, error))
		return FALSE;

	if (!fu_memcpy_safe (model->name, FU_PXI_DEVICE_MODEL_NAME_LEN, 0x0,	/* dst */
			     res ,FU_PXI_DONGLE_RECEIVER_OTA_BUF_SZ, 0xa,				/* src */
			     FU_PXI_DEVICE_MODEL_NAME_LEN, error))
		return FALSE;

	if (!fu_common_read_uint8_safe (res, sizeof(res), 0x16,
					&model->type, error))
		return FALSE;
	if (!fu_common_read_uint8_safe (res, sizeof(res), 0x17,
					&model->target, error))
		return FALSE;

	if (!fu_memcpy_safe (model->version, 5, 0x0,	/* dst */
			     res ,sizeof(res), 0x18,		/* src */
			     5, error))
		return FALSE;

	if (!fu_common_read_uint16_safe (res, sizeof(res), 0x1D,
					 &checksum, G_LITTLE_ENDIAN, error))

		return FALSE;

	/* set current version and model name */
	version_str = g_strndup ((gchar *) model->version, 5);
	model->checksum = checksum;

	if (g_getenv ("FWUPD_PIXART_RF_VERBOSE") != NULL) {
		g_debug ("checksum %x",model->checksum);
		g_debug ("version_str %s",version_str);
	}

	return TRUE;
}

static gboolean
fu_pxi_dongle_receiver_get_peripheral_num (FuPxiDongleReceiver *device, guint8 *num_of_models, GError **error)
{
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();
	g_autoptr(GByteArray) dongle_receiver_cmd = g_byte_array_new ();
	guint8 res[FU_PXI_DONGLE_RECEIVER_OTA_BUF_SZ] = { 0x0 };
	FuPxiDongleReceiver *self = FU_PXI_DONGLE_RECEIVER (device);
	dongle_receiver_cmd = g_byte_array_new ();


	fu_byte_array_append_uint8 (ota_cmd, 0x1);						/* ota init new command length */
	fu_byte_array_append_uint8 (ota_cmd, FU_PXI_DEVICE_CMD_FW_OTA_GET_NUM_OF_MODELS);	/* ota ota init new op code */

	self->sn++;
	if (!fu_pxi_common_composite_dongle_cmd (FU_PXI_DEVICE_CMD_FW_OTA_GET_NUM_OF_MODELS,
					self->sn,
					FU_PXI_WIRELESS_PERIPHERAL_TARGET_DONGLE,
					dongle_receiver_cmd,
					ota_cmd, error))
		return FALSE;
	if (!fu_pxi_dongle_receiver_set_feature (self, dongle_receiver_cmd->data,
							dongle_receiver_cmd->len, error))
		return FALSE;

	g_usleep (5 * 1000);

	memset (res, 0, sizeof(res));
	res[0] = PXI_HID_WIRELESS_DEV_OTA_REPORT_ID;

	if (!fu_pxi_dongle_receiver_get_feature (device, res, sizeof(res), error))
		return FALSE;

	if (g_getenv ("FWUPD_PIXART_RF_VERBOSE") != NULL) {
		fu_common_dump_raw (G_LOG_DOMAIN, "res from get model num",
				    res, sizeof(res));
	}
	if (!fu_common_read_uint8_safe (res, sizeof(res), 0xa,
					num_of_models, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_pxi_dongle_receiver_add_periphearals (FuPxiDongleReceiver *device, GError **error)
{
#ifdef HAVE_HIDRAW_H
	g_autofree gchar *child_id = NULL;
	FuPxiWirelessPeripheral *wireless_peripheral = NULL;
	struct ota_fw_dev_model model = {0x0};

	/* get the all wireless periphearals info */
	if (!fu_pxi_dongle_receiver_get_peripheral_info (device, &model, error))
		return FALSE;
	child_id = g_strdup_printf ("HIDRAW\\VEN_%04X&DEV_%04X&MODEL_%s",
				 device->vendor,
				 device->product,
				 model.name);

	if (model.type == Dongle) {

		fu_device_set_version (FU_DEVICE (device), g_strndup ((gchar *) model.version, 5));
		fu_device_add_guid (FU_DEVICE (device), child_id);

	} else {

		wireless_peripheral = fu_pxi_wireless_peripheral_new (&model);
		fu_device_set_logical_id (FU_DEVICE (wireless_peripheral), child_id);
		fu_device_add_guid (FU_DEVICE (wireless_peripheral), child_id);
		fu_device_set_name (FU_DEVICE (wireless_peripheral), g_strndup ((gchar *) model.name, FU_PXI_DEVICE_MODEL_NAME_LEN));
		fu_device_set_version (FU_DEVICE (wireless_peripheral), g_strndup ((gchar *) model.version, 5));
		fu_device_add_child (FU_DEVICE (device), FU_DEVICE (wireless_peripheral));
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
fu_pxi_dongle_receiver_setup_guid (FuPxiDongleReceiver *device, GError **error)
{
#ifdef HAVE_HIDRAW_H
	struct hidraw_devinfo hid_raw_info = { 0x0 };
	g_autofree gchar *devid = NULL;
	g_autoptr(GString) dev_name = NULL;

	/* extra GUID with device name */
	if (!fu_pxi_dongle_receiver_get_raw_info (device, &hid_raw_info ,error))
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
fu_pxi_dongle_receiver_check_periphearals (FuPxiDongleReceiver *device, GError **error)
{
	guint8 num = 0;

	/* get the num of wireless periphearals */
	if(!fu_pxi_dongle_receiver_get_peripheral_num (device, &num , error))
		return FALSE;

	if (g_getenv ("FWUPD_PIXART_RF_VERBOSE") != NULL) {
		g_debug ("num %u",num);
	}
	/* add wireless periphearals */
	for (guint8 idx = 0; idx < num; idx++) {
		if (!fu_pxi_dongle_receiver_add_periphearals (device, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_pxi_dongle_receiver_setup (FuDevice *device, GError **error)
{
	FuPxiDongleReceiver *self = FU_PXI_DONGLE_RECEIVER(device);

	if (!fu_pxi_dongle_receiver_setup_guid (self ,error)) {
		g_prefix_error (error, "failed to setup GUID: ");
		return FALSE;
	}
	if (!fu_pxi_dongle_receiver_fw_ota_init_new (self, 0x0000, error)) {
		g_prefix_error (error, "failed to OTA init new: ");
		return FALSE;
	}
	if (!fu_pxi_dongle_receiver_fw_ota_ini_new_check (self ,error)) {
		g_prefix_error (error, "failed to OTA init new check: ");
		return FALSE;
	}
	if (!fu_pxi_dongle_receiver_check_periphearals (self ,error)) {
		g_prefix_error (error, "failed to add wireless module: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pxi_dongle_receiver_probe (FuDevice *device, GError **error)
{
	/* set the logical and physical ID */
	if (!fu_udev_device_set_logical_id (FU_UDEV_DEVICE (device), "hid", error))
		return FALSE;
	return fu_udev_device_set_physical_id (FU_UDEV_DEVICE (device), "hid", error);
}

static void
fu_pxi_dongle_receiver_init (FuPxiDongleReceiver *self)
{
	g_debug ("fu_pxi_dongle_receiver_init");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_vendor_id (FU_DEVICE (self), "USB:0x093A");
	fu_device_add_protocol (FU_DEVICE (self), "com.pixart.rf");
}

static void
fu_pxi_dongle_receiver_class_init (FuPxiDongleReceiverClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->to_string = fu_pxi_dongle_receiver_to_string;
	klass_device->setup = fu_pxi_dongle_receiver_setup;
	klass_device->probe = fu_pxi_dongle_receiver_probe;
	klass_device->write_firmware = fu_pxi_dongle_receiver_write_firmware;
	klass_device->prepare_firmware = fu_pxi_dongle_receiver_prepare_firmware;
}

