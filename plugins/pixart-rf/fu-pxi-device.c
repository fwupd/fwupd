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
#define PXI_HID_DEV_OTA_RETRANSMIT_REPORT_ID	0x06
#define PXI_HID_DEV_OTA_FEATURE_REPORT_ID	0x07

#define PXI_HID_DEV_OTA_REPORT_USAGE_PAGE	0xff02u
#define PXI_HID_DEV_OTA_RETRANSMIT_USAGE_PAGE	0xff01u

#define FU_PXI_DEVICE_CMD_FW_OTA_INIT		0x10u
#define FU_PXI_DEVICE_CMD_FW_WRITE		0x17u
#define FU_PXI_DEVICE_CMD_FW_UPGRADE		0x18u
#define FU_PXI_DEVICE_CMD_FW_MCU_RESET		0x22u
#define FU_PXI_DEVICE_CMD_FW_GET_INFO		0x23u
#define FU_PXI_DEVICE_CMD_FW_OBJECT_CREATE	0x25u
#define FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW	0x27u
#define FU_PXI_DEVICE_CMD_FW_OTA_RETRANSMIT	0x28u
#define FU_PXI_DEVICE_CMD_FW_OTA_DISCONNECT	0x29u
#define FU_PXI_DEVICE_CMD_FW_OTA_GET_MODEL	0x2bu
#define ERR_COMMAND_SUCCESS			0x0

#define FU_PXI_DEVICE_OBJECT_SIZE_MAX		4096	/* bytes */
#define FU_PXI_DEVICE_OTA_BUF_SZ		512	/* bytes */
#define FU_PXI_DEVICE_NOTTFY_RET_LEN		4	/* bytes */
#define FU_PXI_DEVICE_FW_INFO_RET_LEN		8	/* bytes */

#define FU_PXI_DEVICE_NOTIFY_TIMEOUT_MS		5000

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
	OTA_DEVICE_LOW_BATTERY		= 6,	/* Device is under low battery */
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
	guint8		 retransmit_id;
	guint8		 status;
	guint8		 new_flow;
	guint16		 offset;
	guint16		 checksum;
	guint32		 max_object_size;
	guint16		 mtu_size;
	guint16		 prn_threshold;
	guint8		 spec_check_result;
	gchar		*model_name;
};

G_DEFINE_TYPE (FuPxiDevice, fu_pxi_device, FU_TYPE_UDEV_DEVICE)

#ifdef HAVE_HIDRAW_H
static gboolean
fu_pxi_device_get_raw_info (FuPxiDevice *self, struct hidraw_devinfo *info, GError **error)
{
	if (!fu_udev_device_ioctl (FU_UDEV_DEVICE (self),
				   HIDIOCGRAWINFO, (guint8 *) info,
				   NULL, error)) {
		return FALSE;
	}
	return TRUE;
}
#endif

static const gchar *
fu_pxi_device_spec_check_result_to_string (guint8 spec_check_result)
{
	if (spec_check_result == OTA_SPEC_CHECK_OK)
		return "ok";
	if (spec_check_result == OTA_FW_OUT_OF_BOUNDS)
		return "fw-out-of-bounds";
	if (spec_check_result == OTA_PROCESS_ILLEGAL)
		return "process-illegal";
	if (spec_check_result == OTA_RECONNECT)
		return "reconnect";
	if (spec_check_result == OTA_FW_IMG_VERSION_ERROR)
		return "fw-img-version-error";
	if (spec_check_result == OTA_DEVICE_LOW_BATTERY)
		return "device battery is too low";
	return NULL;
}

static void
fu_pxi_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuPxiDevice *self = FU_PXI_DEVICE (device);

	/* FuUdevDevice->to_string */
	FU_DEVICE_CLASS (fu_pxi_device_parent_class)->to_string (device, idt, str);

	fu_common_string_append_kv (str, idt, "ModelName", self->model_name);
	fu_common_string_append_kx (str, idt, "Status", self->status);
	fu_common_string_append_kx (str, idt, "NewFlow", self->new_flow);
	fu_common_string_append_kx (str, idt, "CurrentObjectOffset", self->offset);
	fu_common_string_append_kx (str, idt, "CurrentChecksum", self->checksum);
	fu_common_string_append_kx (str, idt, "MaxObjectSize", self->max_object_size);
	fu_common_string_append_kx (str, idt, "MtuSize", self->mtu_size);
	fu_common_string_append_kx (str, idt, "PacketReceiptNotificationThreshold", self->prn_threshold);
	fu_common_string_append_kv (str, idt, "SpecCheckResult",
				    fu_pxi_device_spec_check_result_to_string (self->spec_check_result));
	fu_common_string_append_kx (str, idt, "RetransmitID", self->retransmit_id);
}

static FuFirmware *
fu_pxi_device_prepare_firmware (FuDevice *device,
				GBytes *fw,
				FwupdInstallFlags flags,
				GError **error)
{
	FuPxiDevice *self = FU_PXI_DEVICE (device);
	const gchar *model_name;
	g_autoptr(FuFirmware) firmware = fu_pxi_firmware_new ();

	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;

	/* check is compatible with hardware */
	model_name = fu_pxi_firmware_get_model_name (FU_PXI_FIRMWARE (firmware));
	if ((flags & FWUPD_INSTALL_FLAG_FORCE) == 0) {
		if (self->model_name == NULL || model_name == NULL) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "legacy device or firmware detected, "
					     "--force required");
			return NULL;
		}
		if (g_strcmp0 (self->model_name, model_name) != 0) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "incompatible firmware, got %s, expected %s.",
				     model_name, self->model_name);
			return NULL;
		}
	}

	return g_steal_pointer (&firmware);
}

static gboolean
fu_pxi_device_set_feature (FuPxiDevice *self, GByteArray *req, GError **error)
{
#ifdef HAVE_HIDRAW_H
	if (g_getenv ("FWUPD_PIXART_RF_VERBOSE") != NULL) {
		fu_common_dump_raw (G_LOG_DOMAIN, "SetFeature",
				    req->data, req->len);
	}
	return fu_udev_device_ioctl (FU_UDEV_DEVICE (self),
				     HIDIOCSFEATURE(req->len), (guint8 *) req->data,
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
fu_pxi_device_get_feature (FuPxiDevice *self, guint8 *buf, guint bufsz, GError **error)
{
#ifdef HAVE_HIDRAW_H
	if (!fu_udev_device_ioctl (FU_UDEV_DEVICE (self),
				   HIDIOCGFEATURE(bufsz), buf,
				   NULL, error)) {
		return FALSE;
	}
	if (g_getenv ("FWUPD_PIXART_RF_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "GetFeature", buf, bufsz);

	/* prepend the report-id and cmd for versions of bluez that do not have
	 * https://github.com/bluez/bluez/commit/35a2c50437cca4d26ac6537ce3a964bb509c9b62 */
	if (bufsz > 2 && buf[0] != PXI_HID_DEV_OTA_FEATURE_REPORT_ID) {
		g_debug ("doing fixup for old bluez version");
		memmove (buf + 2, buf, bufsz - 2);
		buf[0] = PXI_HID_DEV_OTA_FEATURE_REPORT_ID;
		buf[1] = 0x0;
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

static guint16
fu_pxi_device_calculate_checksum (const guint8 *buf, gsize bufsz)
{
	guint16 checksum = 0;
	for (gsize idx = 0; idx < bufsz; idx++)
		checksum += (guint16) buf[idx];
	return checksum;
}

static gboolean
fu_pxi_device_search_hid_usage_page (guint8 *report_descriptor, gint size,
				     guint8 *usage_page, guint8 usage_page_sz)
{
	gint pos = 0;

	if (g_getenv ("FWUPD_PIXART_RF_VERBOSE") != NULL) {
		fu_common_dump_raw (G_LOG_DOMAIN, "target usage_page",
				    usage_page, usage_page_sz);
	}

	while (pos < size) {
		/* HID info define by HID specification */
		guint8 item = report_descriptor[pos];
		guint8 report_size = item & 0x03;
		guint8 report_tag =  item & 0xF0;
		guint8 usage_page_tmp[4] = {0x00};

		report_size = (report_size == 3) ? 4 : report_size;

		if (report_tag != 0) {
			pos += report_size + 1;
			continue;
		}

		memmove (usage_page, &report_descriptor[pos + 1], report_size);
		if (memcmp (usage_page, usage_page_tmp, usage_page_sz) == 0) {
			if (g_getenv ("FWUPD_PIXART_RF_VERBOSE") != NULL) {
				g_debug ("hit item: %x  ",item);
				fu_common_dump_raw (G_LOG_DOMAIN, "usage_page", usage_page, report_size);
				g_debug ("hit pos %d",pos);
			}
			return TRUE; 	/* finished processing */
		}
		pos += report_size + 1;
	}

	return FALSE ; /* finished processing */
}

static gboolean
fu_pxi_device_check_support_report_id (FuPxiDevice *self,
				       GError **error)
{
#ifdef HAVE_HIDRAW_H
	gint desc_size = 0;
	g_autoptr(GByteArray) req = g_byte_array_new ();

	struct hidraw_report_descriptor rpt_desc;

	/* Get Report Descriptor Size */
	if (!fu_udev_device_ioctl (FU_UDEV_DEVICE (self), HIDIOCGRDESCSIZE,
				   (guint8*)&desc_size, NULL, error))
		return FALSE;

	rpt_desc.size = desc_size;
	if (!fu_udev_device_ioctl (FU_UDEV_DEVICE (self), HIDIOCGRDESC,
			           (guint8*)&rpt_desc,
				   NULL, error))
		return FALSE;
	fu_common_dump_raw (G_LOG_DOMAIN, "HID descriptor",
			    rpt_desc.value, rpt_desc.size);


	/* check ota retransmit feature report usage page exist or not */
	fu_byte_array_append_uint16 (req, PXI_HID_DEV_OTA_RETRANSMIT_USAGE_PAGE, G_LITTLE_ENDIAN);
	if (!fu_pxi_device_search_hid_usage_page (rpt_desc.value, rpt_desc.size,
						  req->data, req->len)) {
		/* replace retransmit report id with feature report id, if retransmit report id not found */
		self->retransmit_id = PXI_HID_DEV_OTA_FEATURE_REPORT_ID;
	}
	return TRUE;

#else
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "<linux/hidraw.h> not available");
	return FALSE
#endif
}

static gboolean
fu_pxi_device_fw_ota_check_retransmit (FuPxiDevice *self, GError **error)
{
	g_autoptr(GByteArray) req = g_byte_array_new ();

	/* write fw ota retransmit command to reset the ota state */
	fu_byte_array_append_uint8 (req, self->retransmit_id);
	fu_byte_array_append_uint8 (req, FU_PXI_DEVICE_CMD_FW_OTA_RETRANSMIT);
	return fu_pxi_device_set_feature (self, req, error);
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
	if (fw == NULL)
		return FALSE;

	/* check offset is invalid or not */
	chunks = fu_chunk_array_new_from_bytes (fw, 0x0, 0x0, FU_PXI_DEVICE_OBJECT_SIZE_MAX);
	if (self->offset > chunks->len) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "offset from device is invalid: "
			     "got 0x%x, current maximum 0x%x",
			     self->offset,
			     chunks->len);
		return FALSE;
	}

	/* calculate device current checksum */
	for (guint i = 0; i < self->offset; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		checksum_tmp += fu_pxi_device_calculate_checksum (fu_chunk_get_data (chk),
								  fu_chunk_get_data_sz (chk));
	}

	/* check current file is different with previous fw bin or not */
	if (self->checksum != checksum_tmp) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "checksum is different from previous fw: "
			     "got 0x%04x, expected 0x%04x",
			     self->checksum,
			     checksum_tmp);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_device_wait_notify (FuPxiDevice *self,
			   goffset port,
			   guint8 *status,
			   guint16 *checksum,
			   GError **error)
{
	g_autoptr(GTimer) timer = g_timer_new ();
	guint8 res[FU_PXI_DEVICE_OTA_BUF_SZ] = { 0 };
	guint8 cmd_status = 0x0;

	/* skip the wrong report id ,and keep polling until result is correct */
	while (g_timer_elapsed (timer, NULL) * 1000.f < FU_PXI_DEVICE_NOTIFY_TIMEOUT_MS) {
		if (!fu_udev_device_pread_full (FU_UDEV_DEVICE (self),
						port, res, (FU_PXI_DEVICE_NOTTFY_RET_LEN + 1) - port,
						error))
			return FALSE;
		if (res[0] == PXI_HID_DEV_OTA_INPUT_REPORT_ID)
			break;
	}

	/* timeout */
	if (res[0] != PXI_HID_DEV_OTA_INPUT_REPORT_ID) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Timed-out waiting for HID report");
		return FALSE;
	}
	/* get the opcode if status is not null */
	if (status != NULL) {
		guint8 status_tmp = 0x0;
		if (!fu_common_read_uint8_safe (res, sizeof(res), 0x1,
						&status_tmp, error))
			return FALSE;
		/* need check command result if command is fw upgrade */
		if (status_tmp == FU_PXI_DEVICE_CMD_FW_UPGRADE) {
			if (!fu_common_read_uint8_safe (res, sizeof(res), 0x2,
							&cmd_status, error))
				return FALSE;
			if (cmd_status != ERR_COMMAND_SUCCESS) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_READ,
					     "cmd status was 0x%02x",
					     cmd_status);
				return FALSE;
			}
		}

		/* propagate */
		*status = status_tmp;
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
	guint8 opcode = 0;
	g_autoptr(GByteArray) req = g_byte_array_new ();

	/* request */
	fu_byte_array_append_uint8 (req, PXI_HID_DEV_OTA_FEATURE_REPORT_ID);
	fu_byte_array_append_uint8 (req, FU_PXI_DEVICE_CMD_FW_OBJECT_CREATE);
	fu_byte_array_append_uint32 (req, fu_chunk_get_address (chk), G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32 (req, fu_chunk_get_data_sz (chk), G_LITTLE_ENDIAN);
	if (!fu_pxi_device_set_feature (self, req, error))
		return FALSE;

	/* check object create success or not */
	if (!fu_pxi_device_wait_notify (self, 0x0, &opcode, NULL, error))
		return FALSE;
	if (opcode != FU_PXI_DEVICE_CMD_FW_OBJECT_CREATE) {
		 g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "FwObjectCreate opcode got 0x%02x, expected 0x%02x",
			     opcode,
			     FU_PXI_DEVICE_CMD_FW_OBJECT_CREATE);
		 return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_device_write_payload (FuPxiDevice *self, FuChunk *chk, GError **error)
{
	g_autoptr(GByteArray) req = g_byte_array_new ();
	fu_byte_array_append_uint8 (req, PXI_HID_DEV_OTA_FEATURE_REPORT_ID);
	g_byte_array_append (req, fu_chunk_get_data (chk), fu_chunk_get_data_sz (chk));
	return fu_pxi_device_set_feature (self, req, error);
}

static gboolean
fu_pxi_device_write_chunk (FuPxiDevice *self, FuChunk *chk, GError **error)
{
	guint32 prn = 0;
	guint16 checksum;
	guint16 checksum_device = 0;
	g_autoptr(GPtrArray) chunks = NULL;

	/* send create fw object command */
	if (!fu_pxi_device_fw_object_create (self, chk, error))
		return FALSE;

	/* write payload */
	chunks = fu_chunk_array_new (fu_chunk_get_data (chk),
				     fu_chunk_get_data_sz (chk),
				     fu_chunk_get_address (chk),
				     0x0, self->mtu_size);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk2 = g_ptr_array_index (chunks, i);
		if (!fu_pxi_device_write_payload (self, chk2, error))
			return FALSE;
		prn++;
		/* wait notify from device when PRN over threshold write or
		 * offset reach max object sz or write offset reach fw length */
		if (prn >= self->prn_threshold || i == chunks->len - 1) {
			guint8 opcode = 0;
			if (!fu_pxi_device_wait_notify (self, 0x0,
							&opcode,
							&checksum_device,
							error))
				return FALSE;
			if (opcode != FU_PXI_DEVICE_CMD_FW_WRITE) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_READ,
					     "FwWrite opcode invalid 0x%02x",
					     opcode);
				return FALSE;
			}
			prn = 0;
		}
	}

	/* the last chunk */
	checksum = fu_pxi_device_calculate_checksum (fu_chunk_get_data (chk),
						     fu_chunk_get_data_sz (chk));
	self->checksum += checksum;
	if (checksum_device != self->checksum ) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "checksum fail, got 0x%04x, expected 0x%04x",
			     checksum_device,
			     checksum);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_device_reset (FuPxiDevice *self, GError **error)
{
	g_autoptr(GByteArray) req = g_byte_array_new ();
	fu_byte_array_append_uint8 (req, PXI_HID_DEV_OTA_FEATURE_REPORT_ID);
	fu_byte_array_append_uint8 (req, FU_PXI_DEVICE_CMD_FW_MCU_RESET);	/* OTA reset command */
	fu_byte_array_append_uint8 (req, OTA_RESET);				/* OTA reset reason  */
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_RESTART);

	if (!fu_pxi_device_set_feature (self, req, error)) {
		g_prefix_error (error, "failed to reset: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_device_fw_ota_init (FuPxiDevice *self, GError **error)
{

	g_autoptr(GByteArray) req = g_byte_array_new ();

	/* write fw ota init command */
	fu_byte_array_append_uint8 (req, PXI_HID_DEV_OTA_FEATURE_REPORT_ID);
	fu_byte_array_append_uint8 (req, FU_PXI_DEVICE_CMD_FW_OTA_INIT);
	return fu_pxi_device_set_feature (self, req, error);
}

static gboolean
fu_pxi_device_fw_ota_init_new (FuPxiDevice *self, gsize bufsz, GError **error)
{
	guint8 res[FU_PXI_DEVICE_OTA_BUF_SZ] = { 0x0 };
	guint8 fw_version[10] = { 0x0 };
	g_autoptr(GByteArray) req = g_byte_array_new ();

	/* write fw ota init new command */
	fu_byte_array_append_uint8 (req, PXI_HID_DEV_OTA_FEATURE_REPORT_ID);
	fu_byte_array_append_uint8 (req, FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW);
	fu_byte_array_append_uint32 (req, bufsz, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint8 (req, 0x0);	/* OTA setting */
	g_byte_array_append (req, fw_version, sizeof(fw_version));
	if (!fu_pxi_device_set_feature (self, req, error))
		return FALSE;

	/* delay for BLE device read command */
	g_usleep (10 * 1000);

	/* read fw ota init new command */
	res[0] = PXI_HID_DEV_OTA_FEATURE_REPORT_ID;
	res[1] = FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW;
	if (!fu_pxi_device_get_feature (self, res, sizeof(res), error))
		return FALSE;

	/* shared state */
	if (!fu_common_read_uint8_safe (res, sizeof(res), 0x5,
					&self->status, error))
		return FALSE;
	if (!fu_common_read_uint8_safe (res, sizeof(res), 0x6,
					&self->new_flow, error))
		return FALSE;
	if (!fu_common_read_uint16_safe (res, sizeof(res), 0x7,
					 &self->offset, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint16_safe (res, sizeof(res), 0x9,
					 &self->checksum, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint32_safe (res, sizeof(res), 0xb,
					 &self->max_object_size, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint16_safe (res, sizeof(res), 0xf,
					 &self->mtu_size, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint16_safe (res, sizeof(res), 0x11,
					 &self->prn_threshold, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint8_safe (res, sizeof(res), 0x13,
					&self->spec_check_result, error))
		return FALSE;

	/* sanity check */
	if (self->spec_check_result != OTA_SPEC_CHECK_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "FwInitNew spec check fail: %s [0x%02x]",
			     fu_pxi_device_spec_check_result_to_string (self->spec_check_result),
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
	const guint8 *buf;
	gsize bufsz = 0;
	guint8 fw_version[5] = { 0x0 };
	guint8 opcode = 0;
	guint16 checksum;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GByteArray) req = g_byte_array_new ();

	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;
	buf = g_bytes_get_data (fw, &bufsz);
	checksum = fu_pxi_device_calculate_checksum (buf, bufsz);
	fu_byte_array_append_uint8 (req, PXI_HID_DEV_OTA_FEATURE_REPORT_ID);
	fu_byte_array_append_uint8 (req, FU_PXI_DEVICE_CMD_FW_UPGRADE);
	fu_byte_array_append_uint32 (req, bufsz, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint16 (req, checksum, G_LITTLE_ENDIAN);
	version = fu_firmware_get_version (firmware);
	if (!fu_memcpy_safe (fw_version, sizeof(fw_version), 0x0,	/* dst */
			     (guint8 *) version, strlen (version), 0x0,	/* src */
			     strlen (version), error))
		return FALSE;
	g_byte_array_append (req, fw_version, sizeof(fw_version));

	/* send fw upgrade command */
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_VERIFY);
	if (!fu_pxi_device_set_feature (self, req, error))
		return FALSE;

	if (g_getenv ("FWUPD_PIXART_RF_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "fw upgrade", req->data, req->len);

	/* wait fw upgrade command result */
	if (!fu_pxi_device_wait_notify (self, 0x1, &opcode, NULL, error)) {
		g_prefix_error (error,
				"FwUpgrade command fail, "
				"fw-checksum: 0x%04x fw-size: %" G_GSIZE_FORMAT ": ",
				checksum,
				bufsz);
		return FALSE;
	}
	if (opcode != FU_PXI_DEVICE_CMD_FW_UPGRADE) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "FwUpgrade opcode invalid 0x%02x",
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


	/* send fw ota retransmit command to reset status */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_BUSY);
	if (!fu_pxi_device_fw_ota_check_retransmit (self, error)) {
		g_prefix_error (error, "failed to OTA check retransmit: ");
		return FALSE;
	}
	/* send fw ota init command */
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
	g_autoptr(GByteArray) req = g_byte_array_new ();

	fu_byte_array_append_uint8 (req, PXI_HID_DEV_OTA_FEATURE_REPORT_ID);
	fu_byte_array_append_uint8 (req, FU_PXI_DEVICE_CMD_FW_GET_INFO);
	if (!fu_pxi_device_set_feature (self, req, error))
		return FALSE;


	/* delay for BLE device read command */
	g_usleep (10 * 1000);

	res[0] = PXI_HID_DEV_OTA_FEATURE_REPORT_ID;
	res[1] = FU_PXI_DEVICE_CMD_FW_GET_INFO;

	if (!fu_pxi_device_get_feature (self, res, FU_PXI_DEVICE_FW_INFO_RET_LEN + 3, error))
		return FALSE;
	if (!fu_common_read_uint8_safe (res, sizeof(res), 0x4, &opcode, error))
		return FALSE;
	if (opcode != FU_PXI_DEVICE_CMD_FW_GET_INFO) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "FwGetInfo opcode invalid 0x%02x",
			     opcode);
		return FALSE;
	}
	/* set current version */
	version_str = g_strndup ((gchar *) res + 0x6, 5);
	fu_device_set_version (FU_DEVICE (self), version_str);

	/* add current checksum */
	if (!fu_common_read_uint16_safe (res, sizeof(res), 0xb,
					 &checksum, G_LITTLE_ENDIAN, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_device_get_model_info (FuPxiDevice *self, GError **error)
{
	guint8 res[FU_PXI_DEVICE_OTA_BUF_SZ] = { 0x0 };
	guint8 opcode = 0x0;
	guint8 model_name[FU_PXI_DEVICE_MODEL_NAME_LEN] = { 0x0 };
	g_autoptr(GByteArray) req = g_byte_array_new ();

	fu_byte_array_append_uint8 (req, PXI_HID_DEV_OTA_FEATURE_REPORT_ID);
	fu_byte_array_append_uint8 (req, FU_PXI_DEVICE_CMD_FW_OTA_GET_MODEL);

	if (!fu_pxi_device_set_feature (self, req, error))
		return FALSE;

	/* delay for BLE device read command */
	g_usleep (10 * 1000);

	res[0] = PXI_HID_DEV_OTA_FEATURE_REPORT_ID;
	if (!fu_pxi_device_get_feature (self, res, sizeof(res), error))
		return FALSE;
	if (!fu_common_read_uint8_safe (res, sizeof(res), 0x4, &opcode, error))
		return FALSE;

	/* old firmware */
	if (opcode != FU_PXI_DEVICE_CMD_FW_OTA_GET_MODEL)
		return TRUE;

	/* get model from res */
	if (!fu_memcpy_safe (model_name, sizeof(model_name), 0x0,	/* dst */
			     (guint8 *) res, sizeof(res), 0x6,		/* src */
			     sizeof(model_name), error))
		return FALSE;
	g_clear_pointer (&self->model_name, g_free);
	if (model_name[0] != 0x00 && model_name[0] != 0xFF)
		self->model_name = g_strndup ((gchar *) model_name, sizeof(model_name));

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_device_probe (FuDevice *device, GError **error)
{
	/* set the logical and physical ID */
	if (!fu_udev_device_set_logical_id (FU_UDEV_DEVICE (device), "hid", error))
		return FALSE;
	return fu_udev_device_set_physical_id (FU_UDEV_DEVICE (device), "hid", error);
}

static gboolean
fu_pxi_device_setup_guid (FuPxiDevice *self, GError **error)
{
#ifdef HAVE_HIDRAW_H
	struct hidraw_devinfo hid_raw_info = { 0x0 };
	g_autofree gchar *devid = NULL;
	g_autoptr(GString) dev_name = NULL;

	/* extra GUID with device name */
	if (!fu_pxi_device_get_raw_info (self, &hid_raw_info ,error))
		return FALSE;
	dev_name = g_string_new (fu_device_get_name (FU_DEVICE (self)));
	g_string_ascii_up (dev_name);
	fu_common_string_replace (dev_name, " ", "_");
	devid = g_strdup_printf ("HIDRAW\\VEN_%04X&DEV_%04X&NAME_%s",
				 (guint) hid_raw_info.vendor,
				 (guint) hid_raw_info.product,
				 dev_name->str);
	fu_device_add_instance_id (FU_DEVICE (self), devid);

	/* extra GUID with model name*/
	if (self->model_name != NULL) {
		g_autofree gchar *devid2 = NULL;
		g_autoptr(GString) model_name = NULL;
		model_name = g_string_new (self->model_name);
		g_string_ascii_up (model_name);
		fu_common_string_replace (model_name, " ", "_");
		devid2 = g_strdup_printf ("HIDRAW\\VEN_%04X&DEV_%04X&MODEL_%s",
					 (guint) hid_raw_info.vendor,
					 (guint) hid_raw_info.product,
					 dev_name->str);
		fu_device_add_instance_id (FU_DEVICE (self), devid2);
	}
#endif
	return TRUE;
}

static gboolean
fu_pxi_device_setup (FuDevice *device, GError **error)
{
	FuPxiDevice *self = FU_PXI_DEVICE (device);

	if (!fu_pxi_device_check_support_report_id (self, error)) {
		g_prefix_error (error, "failed to check report id: ");
	}
	if (!fu_pxi_device_fw_ota_check_retransmit (self, error)) {
		g_prefix_error (error, "failed to OTA check retransmit: ");
		return FALSE;
	}
	if (!fu_pxi_device_fw_ota_init (self, error)) {
		g_prefix_error (error, "failed to OTA init: ");
		return FALSE;
	}
	if (!fu_pxi_device_fw_get_info (self, error)) {
		g_prefix_error (error, "failed to get info: ");
		return FALSE;
	}
	if (!fu_pxi_device_get_model_info (self ,error)) {
		g_prefix_error (error, "failed to get model: ");
		return FALSE;
	}
	if (!fu_pxi_device_setup_guid (self ,error)) {
		g_prefix_error (error, "failed to setup GUID: ");
		return FALSE;
	}

	return TRUE;
}

static void
fu_pxi_device_init (FuPxiDevice *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_vendor_id (FU_DEVICE (self), "USB:0x093A");
	fu_device_add_protocol (FU_DEVICE (self), "com.pixart.rf");
	self->retransmit_id = PXI_HID_DEV_OTA_RETRANSMIT_REPORT_ID;
}

static void
fu_pxi_device_finalize (GObject *object)
{
	FuPxiDevice *self = FU_PXI_DEVICE (object);
	g_free (self->model_name);
}

static void
fu_pxi_device_class_init (FuPxiDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	object_class->finalize = fu_pxi_device_finalize;
	klass_device->probe = fu_pxi_device_probe;
	klass_device->setup = fu_pxi_device_setup;
	klass_device->to_string = fu_pxi_device_to_string;
	klass_device->write_firmware = fu_pxi_device_write_firmware;
	klass_device->prepare_firmware = fu_pxi_device_prepare_firmware;
}
