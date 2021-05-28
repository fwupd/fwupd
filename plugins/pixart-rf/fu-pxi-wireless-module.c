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

#include "fu-common.h"
#include "fu-chunk.h"
#include "fu-pxi-wireless-module.h"
#include "fu-pxi-firmware.h"
#include "fu-pxi-wireless-device.h"
#include "fu-pxi-common.h"

struct _FuPxiWirelessModule {
	FuDevice	 	parent_instance;
	guint8		 	status;
	guint8		 	new_flow;
	guint16		 	offset;
	guint16		 	checksum;
	guint32		 	max_object_size;
	guint16		 	mtu_size;
	guint16		 	prn_threshold;
	guint8		 	spec_check_result;
	guint8			sn;
	struct ota_fw_dev_model 	model;
};

G_DEFINE_TYPE (FuPxiWirelessModule, fu_pxi_wireless_module, FU_TYPE_DEVICE)

static gboolean
fu_pxi_wireless_module_set_feature (FuDevice *self, const guint8 *buf, guint bufsz, GError **error)
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
fu_pxi_wireless_module_get_feature (FuDevice *self, guint8 *buf, guint bufsz, GError **error)
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
fu_pxi_wireless_module_to_string (FuDevice *device, guint idt, GString *str)
{
	FuPxiWirelessModule *self = FU_PXI_WIRELESS_MODULE (device);
	fu_common_string_append_kx (str, idt, "Status", self->status);
	fu_common_string_append_kx (str, idt, "NewFlow", self->new_flow);
	fu_common_string_append_kx (str, idt, "CurrentObjectOffset", self->offset);
	fu_common_string_append_kx (str, idt, "CurrentChecksum", self->checksum);
	fu_common_string_append_kx (str, idt, "MaxObjectSize", self->max_object_size);
	fu_common_string_append_kx (str, idt, "MtuSize", self->mtu_size);
	fu_common_string_append_kx (str, idt, "PacketReceiptNotificationThreshold", self->prn_threshold);
	fu_common_string_append_kv (str, idt, "ModelName", (gchar*)self->model.name);
	fu_common_string_append_kx (str, idt, "Modeltype", self->model.type);
	fu_common_string_append_kx (str, idt, "Modeltarget", self->model.target);
}

static FuPxiWirelessDevice *
fu_pxi_wireless_module_get_parent (FuDevice *self, GError **error)
{
	FuDevice *parent = fu_device_get_parent (FU_DEVICE (self));
	if (parent == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "no parent set");
		return NULL;
	}
	return FU_PXI_WIRELESS_DEVICE (FU_UDEV_DEVICE (parent));
}

static gboolean
fu_pxi_wireless_module_open (FuDevice *device, GError **error)
{
	FuDevice *parent = fu_device_get_parent (device);
	if (parent == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "no parent device");
		return FALSE;
	}
	return fu_device_open (parent, error);
}

static gboolean
fu_pxi_wireless_module_close (FuDevice *device, GError **error)
{
	FuDevice *parent = fu_device_get_parent (device);
	if (parent == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "no parent device");
		return FALSE;
	}
	return fu_device_close (parent, error);
}

static gboolean
fu_pxi_wireless_module_get_cmd_response (FuPxiWirelessModule *device, guint8 *res, guint sz, GError **error) 
{
	
	FuPxiWirelessDevice *parent = fu_pxi_wireless_module_get_parent (FU_DEVICE (device) ,error);
	guint16 retry = 0;
	guint8 status = 0x0;

	while (1)
	{	
		guint8 sn = 0x0;
		memset (res, 0, sz);
		res[0] = PXI_HID_WIRELESS_DEV_OTA_REPORT_ID;

		g_usleep (5 * 1000);

		if (!fu_pxi_wireless_module_get_feature (FU_DEVICE (parent), res, sz, error))
			return FALSE;

		if (!fu_common_read_uint8_safe (res, sizeof(res), 0x4, &sn, error))
			return FALSE;

		if (device->sn != sn)
			retry++;
		else
			break;

		if (retry == FU_PXI_WIRELESS_MODULE_RETRY_MAXIMUM) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_READ,
				     "reach retry maximum hid sn fail, got 0x%04x, expected 0x%04x",
				     sn,
				     device->sn);
			return FALSE;
		}
		/*if wireless device not reply to dongle, keep to wait */
		if (!fu_common_read_uint8_safe (res, sizeof(res), 0x5, &status, error))
			return FALSE;

		if(status == OTA_RSP_NOT_READY) {
			retry = 0x0;
			g_debug ("OTA_RSP_NOT_READY");
		}
	}
	return TRUE;
}

static gboolean
fu_pxi_wireless_module_check_crc (FuDevice *device, guint16 checksum, GError **error)
{	
	g_autoptr(GByteArray) wireless_module_cmd = g_byte_array_new ();	
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();
	FuPxiWirelessDevice *parent = fu_pxi_wireless_module_get_parent (device ,error);
	FuPxiWirelessModule *self = FU_PXI_WIRELESS_MODULE (device);
	guint8 res[FU_PXI_WIRELESS_MODULE_OTA_BUF_SZ] = { 0x0 };
	guint16 checksum_device = 0x0;
	guint8 status = 0x0;	
	g_debug ("fu_pxi_wireless_module_check_crc");
	
	/* ota check crc command */
	fu_byte_array_append_uint8 (ota_cmd, 0x3);					/* ota command length */
	fu_byte_array_append_uint8 (ota_cmd, FU_PXI_DEVICE_CMD_FW_OTA_CHECK_CRC);	/* ota command */
	fu_byte_array_append_uint16 (ota_cmd, checksum, G_LITTLE_ENDIAN);		/* checkesum */
	
	/* increase the serial number */
	self->sn++;
	
	if (!fu_pxi_common_composite_module_cmd (FU_PXI_DEVICE_CMD_FW_OTA_CHECK_CRC,
							self->sn,
							self->model.target,
							wireless_module_cmd,
							ota_cmd, error))
		return FALSE;

	if (!fu_pxi_wireless_module_set_feature (FU_DEVICE (parent), 
						 wireless_module_cmd->data, 
						 wireless_module_cmd->len, error))
		return FALSE;

	if (!fu_pxi_wireless_module_get_cmd_response (self, res, sizeof(res), error))
			return FALSE;

	if (!fu_common_read_uint8_safe (res, sizeof(res), 0x5, &status, error))
			return FALSE;

	if (!fu_common_read_uint16_safe (res, sizeof(res), 0x6, 
					&checksum_device, G_LITTLE_ENDIAN, error))
			return FALSE;

	fu_common_dump_raw (G_LOG_DOMAIN, "crc res ", res, sizeof(res));

	if(status ==  OTA_RSP_CODE_ERROR) {
		g_set_error (error,
		FWUPD_ERROR,
		FWUPD_ERROR_READ,
		"checksum fail, got 0x%04x, expected 0x%04x",
		checksum_device,
		checksum
		);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_wireless_module_fw_object_create (FuDevice *device, FuChunk *chk, GError **error)
{
	g_autoptr(GByteArray) wireless_module_cmd = g_byte_array_new ();	
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();
	FuPxiWirelessModule *self = FU_PXI_WIRELESS_MODULE (device);
	FuPxiWirelessDevice *parent = fu_pxi_wireless_module_get_parent (device ,error);
	
	/* ota object create command */
	fu_byte_array_append_uint8 (ota_cmd, 0x9);					/* ota command length */
	fu_byte_array_append_uint8 (ota_cmd, FU_PXI_DEVICE_CMD_FW_OBJECT_CREATE);	/* ota command */
	fu_byte_array_append_uint32 (ota_cmd, fu_chunk_get_address (chk), G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32 (ota_cmd, fu_chunk_get_data_sz (chk), G_LITTLE_ENDIAN);
	
	/* increase the serial number */	
	self->sn++;

	/* get pixart wireless module for ota object create command */
	if (!fu_pxi_common_composite_module_cmd (FU_PXI_DEVICE_CMD_FW_OBJECT_CREATE,
							self->sn,
							self->model.target,
							wireless_module_cmd,
							ota_cmd, error))
		return FALSE;

	/* delay for wireless module device get command response*/
	g_usleep (50 * 1000);

	if (!fu_pxi_wireless_module_set_feature (FU_DEVICE (parent), wireless_module_cmd->data, wireless_module_cmd->len, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_wireless_module_write_payload (FuDevice *device, FuChunk *chk, GError **error)
{
	g_autoptr(GByteArray) wireless_module_cmd = g_byte_array_new ();	
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();
	FuPxiWirelessDevice *parent = fu_pxi_wireless_module_get_parent (device ,error);
	FuPxiWirelessModule *self = FU_PXI_WIRELESS_MODULE (device);
	g_autoptr(GTimer) timer = g_timer_new ();
	guint8 res[FU_PXI_WIRELESS_MODULE_OTA_BUF_SZ] = { 0x0 };
	guint8 status = 0x0;
	
	/* ota write payload command */
	fu_byte_array_append_uint8 (ota_cmd, fu_chunk_get_data_sz (chk));			/* ota command length */
	g_byte_array_append (ota_cmd, fu_chunk_get_data (chk), fu_chunk_get_data_sz (chk));	/* payload content */
	
	/* increase the serial number */
	self->sn++;

	/* get pixart wireless module for ota write payload command */
	if (!fu_pxi_common_composite_module_cmd (FU_PXI_DEVICE_CMD_FW_OTA_PAYLOPD_CONTENT,
							self->sn,
							self->model.target,
							wireless_module_cmd, 
							ota_cmd, error))
		return FALSE;
	
	if (!fu_pxi_wireless_module_set_feature (FU_DEVICE (parent), wireless_module_cmd->data, wireless_module_cmd->len, error))
		return FALSE;
	
	/* delay for wireless module device get command response*/
	g_usleep (50 * 1000);

	if (!fu_pxi_wireless_module_get_cmd_response (self, res, sizeof(res), error))
			return FALSE;

	if (!fu_common_read_uint8_safe (res, sizeof(res), 0x5, &status, error))
		return FALSE;

	if(status != OTA_RSP_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "cmd rsp check fail: %s [0x%02x]",
			     fu_pxi_common_wireless_module_cmd_result_to_string (status),
			     status);
		return FALSE;
	}

	/* success */
	return TRUE;	
}

static gboolean
fu_pxi_wireless_module_write_chunk (FuDevice *device, FuChunk *chk, GError **error)
{
	guint32 prn = 0;
	guint16 checksum;
	g_autoptr(GPtrArray) chunks = NULL;
	FuPxiWirelessModule *self = FU_PXI_WIRELESS_MODULE (device);
	
	/* send create fw object command */
	if (!fu_pxi_wireless_module_fw_object_create (device, chk, error))
		return FALSE;

	/* write payload */
	chunks = fu_chunk_array_new (fu_chunk_get_data (chk),
				     fu_chunk_get_data_sz (chk),
				     fu_chunk_get_address (chk),
				     0x0, self->mtu_size);

	/* the checksum of chunk */
	checksum = fu_pxi_common_calculate_16bit_checksum (fu_chunk_get_data (chk),
						     fu_chunk_get_data_sz (chk));
	/* calculate checksum */		
	self->checksum += checksum; 
	
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk2 = g_ptr_array_index (chunks, i);
		if (!fu_pxi_wireless_module_write_payload (device, chk2, error))
			return FALSE;
		prn++;
		/* check crc at fw when PRN over threshold write or
		 * offset reach max object sz or write offset reach fw length */
		if (prn >= self->prn_threshold || i == chunks->len - 1) {
			if (!fu_pxi_wireless_module_check_crc (device, self->checksum, error))
				return FALSE;
			prn = 0;
		}
	}
	/* success */
	return TRUE;
}

static gboolean
fu_pxi_wireless_module_fw_ota_init_new (FuDevice *device, gsize bufsz, GError **error)
{
	guint8 fw_version[10] = { 0x0 };
	g_autoptr(GByteArray) wireless_module_cmd = g_byte_array_new ();	
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();
	FuPxiWirelessDevice *parent = fu_pxi_wireless_module_get_parent (device ,error);
	FuPxiWirelessModule *self = FU_PXI_WIRELESS_MODULE (device);
	
	fu_byte_array_append_uint8 (ota_cmd, 0X06);					/* ota init new command length */
	fu_byte_array_append_uint8 (ota_cmd, FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW);	/* ota ota init new op code */
	fu_byte_array_append_uint32 (ota_cmd, bufsz, G_LITTLE_ENDIAN);			/* fw size */
	fu_byte_array_append_uint8 (ota_cmd, 0x0);					/* ota setting */
	g_byte_array_append (ota_cmd, fw_version, sizeof(fw_version));			/* ota version */

	/* increase the serial number */
	self->sn++;

	if (!fu_pxi_common_composite_module_cmd (FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW,
							self->sn,
							self->model.target,
							wireless_module_cmd,
							ota_cmd, error))
		return FALSE;
	if (!fu_pxi_wireless_module_set_feature (FU_DEVICE (parent), wireless_module_cmd->data, wireless_module_cmd->len, error))
		return FALSE;

	return TRUE;
}


static gboolean
fu_pxi_wireless_module_fw_ota_ini_new_check (FuDevice *device, GError **error)
{

	g_autoptr(GByteArray) wireless_module_cmd = g_byte_array_new ();	
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();	
	FuPxiWirelessDevice *parent = fu_pxi_wireless_module_get_parent (device ,error);
	guint8 res[FU_PXI_WIRELESS_MODULE_OTA_BUF_SZ] = { 0x0 };
	FuPxiWirelessModule *self = FU_PXI_WIRELESS_MODULE (device);
	guint8 status = 0x0;
	
	/* ota command */
	fu_byte_array_append_uint8 (ota_cmd, 0x1);					/* ota command length */
	fu_byte_array_append_uint8 (ota_cmd, FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW_CHECK);	/* ota command */
	
	/* increase the serial number */
	self->sn++;	
	/* get pixart wireless module ota command */
	if (!fu_pxi_common_composite_module_cmd (FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW_CHECK,
							self->sn,
							self->model.target,
							wireless_module_cmd,
							ota_cmd, error))
		return FALSE;
	if (!fu_pxi_wireless_module_set_feature (FU_DEVICE (parent), wireless_module_cmd->data, 
							wireless_module_cmd->len, error))
		return FALSE;
	/* delay for wireless module device get command response*/
	g_usleep (50 * 1000);

	if (!fu_pxi_wireless_module_get_cmd_response (self, res, sizeof(res), error))
			return FALSE;

	if (!fu_common_read_uint8_safe (res, sizeof(res), 0x5, &status, error))
		return FALSE;

	if(status != OTA_RSP_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "cmd rsp check fail: %s [0x%02x]",
			     fu_pxi_common_wireless_module_cmd_result_to_string (status),
			     status);
		return FALSE;

	}

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
fu_pxi_wireless_module_fw_upgrade (FuDevice *device, FuFirmware *firmware, GError **error)
{
	const gchar *version;
	const guint8 *buf;
	gsize bufsz = 0;
	guint8 fw_version[5] = { 0x0 };
	guint16 checksum = 0x0;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GByteArray) wireless_module_cmd = g_byte_array_new ();	
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();	
	FuPxiWirelessDevice *parent = fu_pxi_wireless_module_get_parent (device ,error);
	FuPxiWirelessModule *self = FU_PXI_WIRELESS_MODULE (device);
	
	fw = fu_firmware_get_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;


	buf = g_bytes_get_data (fw, &bufsz);
	checksum = fu_pxi_common_calculate_16bit_checksum (buf, bufsz);
	
	
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
	if (!fu_pxi_common_composite_module_cmd (FU_PXI_DEVICE_CMD_FW_UPGRADE,
							self->sn,
							self->model.target,
							wireless_module_cmd,
							ota_cmd, error))
		return FALSE;
	/* update device status */
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_VERIFY);
	

	/* send ota  fw upgrade command */
	if (!fu_pxi_wireless_module_set_feature (FU_DEVICE (parent), wireless_module_cmd->data, 
							wireless_module_cmd->len, error))
		return FALSE;
		

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_wireless_module_reset (FuDevice *device, GError **error)
{
	g_autoptr(GByteArray) wireless_module_cmd = g_byte_array_new ();	
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();	
	FuPxiWirelessDevice *parent = fu_pxi_wireless_module_get_parent (device ,error);
	FuPxiWirelessModule *self = FU_PXI_WIRELESS_MODULE (device);
	
	/* ota mcu reset command */
	fu_byte_array_append_uint8 (ota_cmd, 0x1);					/* ota mcu reset command */
	fu_byte_array_append_uint8 (ota_cmd, FU_PXI_DEVICE_CMD_FW_MCU_RESET);		/* ota mcu reset command op code */
	fu_byte_array_append_uint8 (ota_cmd, OTA_RESET);				/* ota mcu reset command reason */
	
	self->sn++;	
	/* get pixart wireless module ota command */
	if (!fu_pxi_common_composite_module_cmd (FU_PXI_DEVICE_CMD_FW_MCU_RESET,
							self->sn,
							self->model.target,
							wireless_module_cmd,
							ota_cmd, error))
		return FALSE;
	/* update device status */
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_RESTART);					
	

	/* send ota mcu reset command */
	if (!fu_pxi_wireless_module_set_feature (FU_DEVICE (parent), wireless_module_cmd->data, 
							wireless_module_cmd->len, error))
		return FALSE;

	
	/* success */
	return TRUE;
}

static FuFirmware *
fu_pxi_wireless_module_prepare_firmware (FuDevice *device,
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
fu_pxi_wireless_module_write_firmware (FuDevice *device,
				   FuFirmware *firmware,
				   FwupdInstallFlags flags,
				   GError **error)
{
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;
	g_autoptr(GError) error_local = NULL;
	FuPxiWirelessModule *self = FU_PXI_WIRELESS_MODULE (device);
	g_debug ("fu_pxi_wireless_module_write_firmware");
	/* get the default image */
	fw = fu_firmware_get_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;
		
	/* send fw ota init command */
	if (!fu_pxi_wireless_module_fw_ota_init_new (device, g_bytes_get_size (fw), error))
		return FALSE;
	
	if (!fu_pxi_wireless_module_fw_ota_ini_new_check (device ,error))
		return FALSE;
	
	chunks = fu_chunk_array_new_from_bytes (fw, 0x0, 0x0, FU_PXI_DEVICE_OBJECT_SIZE_MAX);	
	/* prepare write fw into device */
	self->offset = 0;
	self->checksum = 0;
	
	
	/* write fw into device */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	for (guint i = self->offset; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		if (!fu_pxi_wireless_module_write_chunk (device, chk, error))
			return FALSE;
		fu_device_set_progress_full (device, (gsize) i, (gsize) chunks->len);
	}

	/* fw upgrade command */
	if (!fu_pxi_wireless_module_fw_upgrade (device, firmware, error))
		return FALSE;
	/* delay for wireless module device read command */
	g_usleep (50 * 1000);
	/* send device reset command */
	return fu_pxi_wireless_module_reset (device, error);

}

static void
fu_pxi_wireless_module_init (FuPxiWirelessModule *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_vendor_id (FU_DEVICE (self), "USB:0x093A");
	fu_device_add_protocol (FU_DEVICE (self), "com.pixart.rf");
}

static void
fu_pxi_wireless_module_class_init (FuPxiWirelessModuleClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->open = fu_pxi_wireless_module_open;
	klass_device->close = fu_pxi_wireless_module_close;
	klass_device->write_firmware = fu_pxi_wireless_module_write_firmware;
	klass_device->prepare_firmware = fu_pxi_wireless_module_prepare_firmware;
	klass_device->to_string = fu_pxi_wireless_module_to_string;
}

FuPxiWirelessModule *
fu_pxi_wireless_module_new (struct ota_fw_dev_model *model)
{
	FuPxiWirelessModule *self = NULL;
	self = g_object_new (FU_TYPE_PXI_WIRELESS_MODULE, NULL);

	self->model.status = model->status;
	for (guint idx = 0; idx < FU_PXI_DEVICE_MODEL_NAME_LEN; idx++)
		self->model.name[idx] = model->name[idx];
	self->model.type = model->type;
	self->model.target = model->target;
	self->sn = model->target;
	return self;
}
