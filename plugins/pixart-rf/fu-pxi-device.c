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

#include "fu-pxi-device.h"
#include "fu-pxi-firmware.h"

#define PXI_HID_DEV_OTA_INPUT_REPORT_ID		0x05
#define PXI_HID_DEV_OTA_OUTPUT_REPORT_ID	0x06
#define PXI_HID_DEV_OTA_FEATURE_REPORT_ID	0x07

#define FU_PXI_DEVICE_CMD_FW_OTA_INIT		0x10
#define FU_PXI_DEVICE_CMD_FW_WRITE		0x17
#define FU_PXI_DEVICE_CMD_FW_UPGRADE		0x18
#define FU_PXI_DEVICE_CMD_FW_MCU_RESET		0x22	/* remove after windows OTA app ready */
#define FU_PXI_DEVICE_CMD_FW_GET_INFO		0x23
#define FU_PXI_DEVICE_CMD_FW_OBJECT_CREATE	0x25
#define FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW	0x27
#define FU_PXI_DEVICE_CMD_FW_OTA_RETRANSMIT	0x28
#define FU_PXI_DEVICE_CMD_FW_OTA_DISCONNECT	0x29

#define FU_PXI_DEVICE_OBJECT_SIZE_MAX		4096	/* bytes */
#define FU_PXI_DEVICE_OTA_BUF_SZ		32	/* bytes */
#define FU_PXI_DEVICE_NOTTFY_RET_LEN		4	/* bytes */
#define FU_PXI_DEVICE_fW_INFO_RET_LEN		8	/* bytes */

/* OTA target selection */
enum ota_process_setting {
	OTA_MAIN_FW,				/* Main firmware */
	OTA_HELPER_FW,				/* Helper firmware */
	OTA_EXTERNAL_RESOURCE,			/* External resource */
};

/* OTA spec check result */
enum ota_spec_check_result {
	OTA_SPEC_CHECK_OK		= 1,	/* Spec check ok */
	OTA_FW_OUT_OF_BOUNDS		= 2,	/* OTA firmware size out of bound */
	OTA_PROCESS_ILLEGAL		= 3,	/* Illegal OTA process */
	OTA_RECONNECT			= 4,	/* Inform OTA app do reconnect */
	OTA_FW_IMG_VERSION_ERROR	= 5,	/* FW image file version check error */
	OTA_SPEC_CHECK_MAX_NUM,			/* Max number of OTA driver defined error code */
};

/* OTA disconnect reason */
enum ota_disconnect_reason {
	OTA_CODE_JUMP			= 1,	/* OTA code jump */
	OTA_UPDATE_DONE			= 2,	/* OTA update done */
	OTA_RESET,				/* OTA reset */
};

struct _FuPxiDevice {
	FuUdevDevice	 parent_instance;
	guint8		 status;
	guint8		 new_flow;
	guint16		 offset;
	guint16		 checksum;
	guint32		 max_object_size;
	guint16		 mtu_size;
	guint16		 prn_threshold;
	guint8		 spec_check_result;
};

G_DEFINE_TYPE (FuPxiDevice, fu_pxi_device, FU_TYPE_UDEV_DEVICE)

static void
fu_pxi_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuPxiDevice *self = FU_PXI_DEVICE (device);
	fu_common_string_append_kx (str, idt, "Status", self->status);
	fu_common_string_append_kx (str, idt, "NewFlow", self->new_flow);
	fu_common_string_append_kx (str, idt, "CurrentObjectOffset", self->offset);
	fu_common_string_append_kx (str, idt, "CurrentChecksum", self->checksum);
	fu_common_string_append_kx (str, idt, "MaxObjectSize", self->max_object_size);
	fu_common_string_append_kx (str, idt, "MtuSize", self->mtu_size);
	fu_common_string_append_kx (str, idt, "PacketReceiptNotificationThreshold", self->prn_threshold);
	fu_common_string_append_kx (str, idt, "SpecCheckResult", self->spec_check_result);
}

static FuFirmware *
fu_pxi_device_prepare_firmware (FuDevice *device,
				GBytes *fw,
				FwupdInstallFlags flags,
				GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_pxi_rf_firmware_new ();
	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;
	return g_steal_pointer (&firmware);
}

static gboolean
fu_pxi_device_set_feature (FuPxiDevice *self, const guint8 *data, guint datasz, GError **error)
{
#ifdef HAVE_HIDRAW_H
	if (g_getenv ("FWUPD_PIXART_RF_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "SetFeature", data, datasz);
	return fu_udev_device_ioctl (FU_UDEV_DEVICE (self),
				     HIDIOCSFEATURE(datasz), (guint8 *) data,
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
fu_pxi_device_get_feature (FuPxiDevice *self, guint8 *data, guint datasz, GError **error)
{
#ifdef HAVE_HIDRAW_H
	if (!fu_udev_device_ioctl (FU_UDEV_DEVICE (self),
				   HIDIOCGFEATURE(datasz), data,
				   NULL, error)) {
		return FALSE;
	}
	if (g_getenv ("FWUPD_PIXART_RF_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "GetFeature", data, datasz);
	return TRUE;
#else
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "<linux/hidraw.h> not available");
	return FALSE;
#endif
}

static guint16
fu_pxi_device_calculate_checksum (const guint8 *data, gsize sz)
{
	guint16 checksum = 0;
	for (gsize idx = 0; idx < sz; idx++)
		checksum += (guint16) data[idx];
	return checksum;
}

static gboolean
fu_pxi_device_check_support_resume (FuPxiDevice *self,
				 	FuFirmware *firmware,
				  	GError **error)
{
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;
	guint16 checksum_tmp = 0x0;
	
	/* get the default image */
	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL) {
		g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_READ,
		     "fw is null");
	       return FALSE;
	}	
	
	chunks = fu_chunk_array_new_from_bytes (fw, 0x0, 0x0, FU_PXI_DEVICE_OBJECT_SIZE_MAX);
	
	/* check offset is invalid or not */
	if (self->offset > chunks->len) {
		g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_READ,
		     "offset from device is invalidfw got %x ,current maximum %u",
			     self->offset, 
			     chunks->len);
	       return FALSE;
	}
	/* calculate device current checksum */
	for (guint i = 0; i < self->offset; i++) {	
		FuChunk *chk = g_ptr_array_index (chunks, i);
		checksum_tmp += fu_pxi_device_calculate_checksum (chk->data, chk->data_sz);
	}
	
	/* check current file is different with previous fw bin or not */
	if (self->checksum != checksum_tmp) {
		g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_READ,
		     "checksum is different with previous fw got %x, expected %x",
			     self->checksum,
			     checksum_tmp);
	       return FALSE;
        }
	return TRUE;
}

static gboolean
fu_pxi_device_wait_notify (FuPxiDevice *self,
			   goffset port,
			   guint8 *status,
			   guint16 *checksum,
			   GError **error)
{
	guint8 res[FU_PXI_DEVICE_OTA_BUF_SZ] = {
		PXI_HID_DEV_OTA_INPUT_REPORT_ID,
		0x0,
	};
	if (!fu_udev_device_pread_full (FU_UDEV_DEVICE (self),
					port, res, (FU_PXI_DEVICE_NOTTFY_RET_LEN + 1) - port,
					error))
		return FALSE;
	if (status != NULL) {
		if (!fu_common_read_uint8_safe (res, sizeof(res), 0x1,
						status, error))
			return FALSE;
	}
	if (checksum != NULL) {
		if (!fu_common_read_uint16_safe (res, sizeof(res), 0x3,
						 checksum, G_LITTLE_ENDIAN, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_device_fw_object_create (FuPxiDevice *self, FuChunk *chk, GError **error)
{
	g_autoptr(GByteArray) req = g_byte_array_new ();
	guint8 opcode = 0;
	/* request */
	fu_byte_array_append_uint8 (req, PXI_HID_DEV_OTA_OUTPUT_REPORT_ID);
	fu_byte_array_append_uint8 (req, FU_PXI_DEVICE_CMD_FW_OBJECT_CREATE);
	fu_byte_array_append_uint32 (req, chk->address, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32 (req, chk->data_sz, G_LITTLE_ENDIAN);
	if (!fu_udev_device_pwrite_full (FU_UDEV_DEVICE (self), 0x0,
					 req->data, req->len, error))
		return FALSE;

	/* check object create success or not */
	if (!fu_pxi_device_wait_notify (self, 0x0, &opcode, NULL, error))
		return FALSE;
	if (opcode != FU_PXI_DEVICE_CMD_FW_OBJECT_CREATE) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "FwWrite opcode invalid %02x",
			     opcode);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_pxi_device_write_payload (FuPxiDevice *self, FuChunk *chk, GError **error)
{
	g_autoptr(GByteArray) req = g_byte_array_new ();
	fu_byte_array_append_uint8 (req, PXI_HID_DEV_OTA_OUTPUT_REPORT_ID);
	g_byte_array_append (req, chk->data, chk->data_sz);
	return fu_udev_device_pwrite_full (FU_UDEV_DEVICE (self), 0x0,
					   req->data, req->len, error);
}

static gboolean
fu_pxi_device_write_chunk (FuPxiDevice *self, FuChunk *chk, GError **error)
{
	guint32 prn = 0;
	guint16 checksum = fu_pxi_device_calculate_checksum (chk->data, chk->data_sz);
	guint16 checksum_tmp = 0;
	g_autoptr(GPtrArray) chunks = NULL;

	/* send create fw object command */
	if (!fu_pxi_device_fw_object_create (self, chk, error))
		return FALSE;

	/* write payload */
	chunks = fu_chunk_array_new (chk->data, chk->data_sz, chk->address,
				     0x0, self->mtu_size);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk2 = g_ptr_array_index (chunks, i);
		if (!fu_pxi_device_write_payload (self, chk2, error))
			return FALSE;
		prn++;
		if (prn >= self->prn_threshold) {
			guint8 opcode = 0;
			if (!fu_pxi_device_wait_notify (self, 0x0, &opcode, &checksum_tmp, error))
				return FALSE;
			if (opcode != FU_PXI_DEVICE_CMD_FW_WRITE) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_READ,
					     "FwWrite opcode invalid %02x",
					     opcode);
				return FALSE;
			}
			prn = 0;
		}
	}
	self->checksum  +=  checksum;
	/* check chunk is equal to FU_PXI_DEVICE_OBJECT_SIZE_MAX */ 
	if (chk->data_sz == FU_PXI_DEVICE_OBJECT_SIZE_MAX) {
		if (!fu_pxi_device_wait_notify (self, 0x0, NULL, &checksum_tmp, error))
			return FALSE;
	} else {
		/* The last checksum from PRN */
		if (self->checksum == checksum)
			return TRUE;
		else {
			/* The last checksum from device fw length is zero notfiy */
			if (!fu_pxi_device_wait_notify (self, 0x0, NULL, &checksum_tmp, error))
				return FALSE;
		}
	}
	g_debug ("checksum %x, table checksum %x", checksum_tmp, self->checksum);
	if (checksum_tmp != self->checksum ) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "checksum fail, got %x, expected %x",
			     checksum_tmp,
			     checksum);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_device_reset (FuPxiDevice *self, GError **error)
{
	guint8 req[FU_PXI_DEVICE_OTA_BUF_SZ] = {
		PXI_HID_DEV_OTA_OUTPUT_REPORT_ID,
		FU_PXI_DEVICE_CMD_FW_MCU_RESET,
		OTA_RESET,
	};
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_RESTART);
	if (!fu_udev_device_pwrite_full (FU_UDEV_DEVICE (self), 0,
					 req, 0x3,
					 error)) {
		g_prefix_error (error, "failed to reset: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_device_fw_ota_init (FuPxiDevice *self, GError **error)
{
	const guint8 req[] = {
		PXI_HID_DEV_OTA_OUTPUT_REPORT_ID,
		FU_PXI_DEVICE_CMD_FW_OTA_INIT,
	};
	return fu_udev_device_pwrite_full (FU_UDEV_DEVICE (self), 0,
					   req, sizeof(req), error);
}

static gboolean
fu_pxi_device_fw_ota_init_new (FuPxiDevice *self, gsize fw_sz, GError **error)
{
	guint8 res[FU_PXI_DEVICE_OTA_BUF_SZ];
	guint8 fw_version[10] = { 0x0 };
	g_autoptr(GByteArray) req = g_byte_array_new ();

	/* write fw ota init new command */
	fu_byte_array_append_uint8 (req, PXI_HID_DEV_OTA_FEATURE_REPORT_ID);
	fu_byte_array_append_uint8 (req, FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW);
	fu_byte_array_append_uint32 (req, fw_sz, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint8 (req, 0x0);	/* OTA setting */
	g_byte_array_append (req, fw_version, sizeof(fw_version));
	if (!fu_pxi_device_set_feature (self, req->data, req->len, error))
		return FALSE;

	/* delay for BLE device read command */
	g_usleep (10 * 1000);

	/* read fw ota init new command */
	res[0] = PXI_HID_DEV_OTA_FEATURE_REPORT_ID;
	res[1] = FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW;
	if (!fu_pxi_device_get_feature (self, res, sizeof(res), error))
		return FALSE;

	/* shared state */
	if (!fu_common_read_uint8_safe (res, sizeof(res), 0x3,
					&self->status, error))
		return FALSE;
	if (!fu_common_read_uint8_safe (res, sizeof(res), 0x4,
					&self->new_flow, error))
		return FALSE;
	if (!fu_common_read_uint16_safe (res, sizeof(res), 0x5,
					 &self->offset, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint16_safe (res, sizeof(res), 0x7,
					 &self->checksum, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint32_safe (res, sizeof(res), 0x9,
					 &self->max_object_size, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint16_safe (res, sizeof(res), 0xd,
					 &self->mtu_size, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint16_safe (res, sizeof(res), 0xf,
					 &self->prn_threshold, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint8_safe (res, sizeof(res), 0x11,
					&self->spec_check_result, error))
		return FALSE;

	if (self->spec_check_result != OTA_SPEC_CHECK_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "FwInitNew Spec Check Fail %02x",
			     self->spec_check_result);
		return FALSE;	
	}
	/* success */
	return TRUE;
}

static gboolean
fu_pxi_device_fw_upgrade (FuPxiDevice *self, FuFirmware *firmware, GError **error)
{
	const gchar *version;
	const guint8 *fw_buf;
	gsize fw_sz = 0;
	guint8 fw_version[10] = { 0x0 };
	guint8 opcode = 0;
	guint16 checksum;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GByteArray) req = g_byte_array_new ();

	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;
	fw_buf = g_bytes_get_data (fw, &fw_sz);
	checksum = fu_pxi_device_calculate_checksum (fw_buf, fw_sz);
	fu_byte_array_append_uint8 (req, PXI_HID_DEV_OTA_OUTPUT_REPORT_ID);
	fu_byte_array_append_uint8 (req, FU_PXI_DEVICE_CMD_FW_UPGRADE);
	fu_byte_array_append_uint32 (req, fw_sz, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint16 (req, checksum, G_LITTLE_ENDIAN);
	version = fu_firmware_get_version (firmware);
	if (!fu_memcpy_safe (fw_version, sizeof(fw_version), 0x0,	/* dst */
			     (guint8 *) version, strlen (version), 0x0,	/* src */
			     strlen (version), error))
		return FALSE;
	g_byte_array_append (req, fw_version, sizeof(fw_version));

	/* send fw upgrade command */
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_VERIFY);
	if (!fu_udev_device_pwrite_full (FU_UDEV_DEVICE (self), 0,
					 req->data, req->len, error))
		return FALSE;
	if (g_getenv ("FWUPD_PIXART_RF_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "fw upgrade", req->data, req->len);

	/* read fw upgrade command result */
	g_debug ("read fw upgrade result");
	if (!fu_pxi_device_wait_notify (self, 0x1, &opcode, NULL, error))
		return FALSE;
	if (opcode != FU_PXI_DEVICE_CMD_FW_UPGRADE) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "FwUpgrade opcode invalid %02x",
			     opcode);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_device_write_firmware (FuDevice *device,
			      FuFirmware *firmware,
			      FwupdInstallFlags flags,
			      GError **error)
{
	FuPxiDevice *self = FU_PXI_DEVICE (device);
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;
	g_autoptr(GError) error_local = NULL;
	
	/* get the default image */
	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	/* send fw ota init command */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_BUSY);
	if (!fu_pxi_device_fw_ota_init (self, error))
		return FALSE;
	if (!fu_pxi_device_fw_ota_init_new (self, g_bytes_get_size (fw), error))
		return FALSE;

	/* prepare write fw into device */
	chunks = fu_chunk_array_new_from_bytes (fw, 0x0, 0x0, FU_PXI_DEVICE_OBJECT_SIZE_MAX);
	if (!fu_pxi_device_check_support_resume (self, firmware, &error_local)) {
		g_debug ("do not resume: %s", error_local->message);
		self->offset = 0;
		self->checksum = 0;
	}

	/* write fw into device */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	for (guint i = self->offset; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		if (!fu_pxi_device_write_chunk (self, chk, error))
			return FALSE;
		fu_device_set_progress_full (device, (gsize) i, (gsize) chunks->len);
	}

	/* fw upgrade command */
	if (!fu_pxi_device_fw_upgrade (self, firmware, error))
		return FALSE;

	/* send device reset command */
	return fu_pxi_device_reset (self, error);
}

static gboolean
fu_pxi_device_fw_get_info (FuPxiDevice *self, GError **error)
{
	guint8 res[FU_PXI_DEVICE_OTA_BUF_SZ] = { 0x0 };
	guint8 opcode = 0x0;
	guint16 checksum = 0;
	g_autofree gchar *version_str = NULL;
	g_autofree gchar *checksum_str = NULL;
	g_autoptr(GByteArray) req = g_byte_array_new ();

	fu_byte_array_append_uint8 (req, PXI_HID_DEV_OTA_OUTPUT_REPORT_ID);
	fu_byte_array_append_uint8 (req, FU_PXI_DEVICE_CMD_FW_GET_INFO);
	if (!fu_udev_device_pwrite_full (FU_UDEV_DEVICE (self), 0,
					 req->data, req->len, error))
		return FALSE;

	res[0] = PXI_HID_DEV_OTA_FEATURE_REPORT_ID;
	res[1] = FU_PXI_DEVICE_CMD_FW_GET_INFO;
	if (!fu_pxi_device_get_feature (self, res, FU_PXI_DEVICE_fW_INFO_RET_LEN + 1, error))
		return FALSE;
		
	if (g_getenv ("FWUPD_PIXART_RF_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "req", (guint8 *) req, sizeof(req));
	if (!fu_common_read_uint8_safe (res, sizeof(res), 0x2, &opcode, error))
		return FALSE;
	if (opcode != FU_PXI_DEVICE_CMD_FW_GET_INFO) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "FwGetInfo opcode invalid %02x",
			     opcode);
		return FALSE;
	}
	/* set current version */
	version_str = g_strndup ((gchar *) res + 0x4, 5);
	fu_device_set_version (FU_DEVICE (self), version_str);

	/* add current checksum */
	if (!fu_common_read_uint16_safe (res, sizeof(res), 0x9,
					 &checksum, G_LITTLE_ENDIAN, error))
		return FALSE;
	checksum_str = g_strdup_printf ("0x%4x", checksum);
	fu_device_add_checksum (FU_DEVICE (self), checksum_str);

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_device_probe (FuDevice *device, GError **error)
{
	/* set the physical ID */
	return fu_udev_device_set_physical_id (FU_UDEV_DEVICE (device), "hid", error);
}

static gboolean
fu_pxi_device_setup (FuDevice *device, GError **error)
{
	FuPxiDevice *self = FU_PXI_DEVICE (device);
	if (!fu_pxi_device_fw_ota_init (self, error))
		return FALSE;
	if (!fu_pxi_device_fw_get_info (self, error))
		return FALSE;
	return TRUE;
}

static void
fu_pxi_device_init (FuPxiDevice *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_NO_GUID_MATCHING);
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_vendor_id (FU_DEVICE (self), "USB:0x093A");
	fu_device_set_protocol (FU_DEVICE (self), "com.pixart.rf");
}

static void
fu_pxi_device_class_init (FuPxiDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->probe = fu_pxi_device_probe;
	klass_device->setup = fu_pxi_device_setup;
	klass_device->to_string = fu_pxi_device_to_string;
	klass_device->write_firmware = fu_pxi_device_write_firmware;
	klass_device->prepare_firmware = fu_pxi_device_prepare_firmware;
}
