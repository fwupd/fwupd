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

#include "fu-pxi-ble-device.h"
#include "fu-pxi-common.h"
#include "fu-pxi-firmware.h"
#include "fu-pxi-struct.h"

#define PXI_HID_DEV_OTA_INPUT_REPORT_ID	     0x05
#define PXI_HID_DEV_OTA_RETRANSMIT_REPORT_ID 0x06
#define PXI_HID_DEV_OTA_FEATURE_REPORT_ID    0x07

#define PXI_HID_DEV_OTA_REPORT_USAGE_PAGE     0xff02u
#define PXI_HID_DEV_OTA_RETRANSMIT_USAGE_PAGE 0xff01u
#define PXI_HID_DEV_OTA_NOTIFY_USAGE_PAGE     0xff00u

#define ERR_COMMAND_SUCCESS 0x0

#define FU_PXI_DEVICE_OBJECT_SIZE_MAX	  4096 /* bytes */
#define FU_PXI_BLE_DEVICE_OTA_BUF_SZ	  512  /* bytes */
#define FU_PXI_BLE_DEVICE_NOTIFY_RET_LEN  4    /* bytes */
#define FU_PXI_BLE_DEVICE_FW_INFO_RET_LEN 8    /* bytes */

#define FU_PXI_BLE_DEVICE_NOTIFY_TIMEOUT_MS 5000

#define FU_PXI_BLE_DEVICE_SET_REPORT_RETRIES 30

/* OTA target selection */
enum ota_process_setting {
	OTA_MAIN_FW,	       /* Main firmware */
	OTA_HELPER_FW,	       /* Helper firmware */
	OTA_EXTERNAL_RESOURCE, /* External resource */
};

struct _FuPxiBleDevice {
	FuUdevDevice parent_instance;
	struct ota_fw_state fwstate;
	guint8 retransmit_id;
	guint8 feature_report_id;
	guint8 input_report_id;
	gchar *model_name;
};

G_DEFINE_TYPE(FuPxiBleDevice, fu_pxi_ble_device, FU_TYPE_UDEV_DEVICE)

#ifdef HAVE_HIDRAW_H
static gboolean
fu_pxi_ble_device_get_raw_info(FuPxiBleDevice *self, struct hidraw_devinfo *info, GError **error)
{
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  HIDIOCGRAWINFO,
				  (guint8 *)info,
				  NULL,
				  FU_PXI_DEVICE_IOCTL_TIMEOUT,
				  error)) {
		return FALSE;
	}
	return TRUE;
}
#endif

static void
fu_pxi_ble_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuPxiBleDevice *self = FU_PXI_BLE_DEVICE(device);

	/* FuUdevDevice->to_string */
	FU_DEVICE_CLASS(fu_pxi_ble_device_parent_class)->to_string(device, idt, str);

	fu_string_append(str, idt, "ModelName", self->model_name);
	fu_pxi_ota_fw_state_to_string(&self->fwstate, idt, str);
	fu_string_append_kx(str, idt, "RetransmitID", self->retransmit_id);
	fu_string_append_kx(str, idt, "FeatureReportID", self->feature_report_id);
	fu_string_append_kx(str, idt, "InputReportID", self->input_report_id);
}

static FuFirmware *
fu_pxi_ble_device_prepare_firmware(FuDevice *device,
				   GBytes *fw,
				   FwupdInstallFlags flags,
				   GError **error)
{
	FuPxiBleDevice *self = FU_PXI_BLE_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_pxi_firmware_new();

	if (!fu_firmware_parse(firmware, fw, flags, error))
		return NULL;

	if (fu_device_has_private_flag(device, FU_PXI_DEVICE_FLAG_IS_HPAC) &&
	    fu_pxi_firmware_is_hpac(FU_PXI_FIRMWARE(firmware))) {
		g_autoptr(GBytes) fw_tmp = NULL;
		guint32 hpac_fw_size = 0;
		const guint8 *fw_ptr = g_bytes_get_data(fw, NULL);

		if (!fu_memread_uint32_safe(fw_ptr,
					    g_bytes_get_size(fw),
					    9,
					    &hpac_fw_size,
					    G_LITTLE_ENDIAN,
					    error))
			return NULL;
		hpac_fw_size += 264;
		fw_tmp = fu_bytes_new_offset(fw, 9, hpac_fw_size, error);
		if (fw_tmp == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "HPAC F/W preparation failed.");
			return NULL;
		}

		fu_firmware_set_bytes(firmware, fw_tmp);
	} else if (!fu_device_has_private_flag(device, FU_PXI_DEVICE_FLAG_IS_HPAC) &&
		   !fu_pxi_firmware_is_hpac(FU_PXI_FIRMWARE(firmware))) {
		const gchar *model_name;

		/* check is compatible with hardware */
		model_name = fu_pxi_firmware_get_model_name(FU_PXI_FIRMWARE(firmware));
		if ((flags & FWUPD_INSTALL_FLAG_FORCE) == 0) {
			if (self->model_name == NULL || model_name == NULL) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_FILE,
						    "legacy device or firmware detected, "
						    "--force required");
				return NULL;
			}
			if (g_strcmp0(self->model_name, model_name) != 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "incompatible firmware, got %s, expected %s.",
					    model_name,
					    self->model_name);
				return NULL;
			}
		}
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "The firmware is incompatible with the device");
		return NULL;
	}

	return g_steal_pointer(&firmware);
}

#ifdef HAVE_HIDRAW_H
static gboolean
fu_pxi_ble_device_set_feature_cb(FuDevice *device, gpointer user_data, GError **error)
{
	GByteArray *req = (GByteArray *)user_data;
	return fu_udev_device_ioctl(FU_UDEV_DEVICE(device),
				    HIDIOCSFEATURE(req->len),
				    (guint8 *)req->data,
				    NULL,
				    FU_PXI_DEVICE_IOCTL_TIMEOUT,
				    error);
}
#endif

static gboolean
fu_pxi_ble_device_set_feature(FuPxiBleDevice *self, GByteArray *req, GError **error)
{
#ifdef HAVE_HIDRAW_H
	fu_dump_raw(G_LOG_DOMAIN, "SetFeature", req->data, req->len);
	return fu_device_retry(FU_DEVICE(self),
			       fu_pxi_ble_device_set_feature_cb,
			       FU_PXI_BLE_DEVICE_SET_REPORT_RETRIES,
			       req,
			       error);
#else
	g_set_error_literal(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "<linux/hidraw.h> not available");
	return FALSE;
#endif
}

static gboolean
fu_pxi_ble_device_get_feature(FuPxiBleDevice *self, guint8 *buf, guint bufsz, GError **error)
{
#ifdef HAVE_HIDRAW_H
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  HIDIOCGFEATURE(bufsz),
				  buf,
				  NULL,
				  FU_PXI_DEVICE_IOCTL_TIMEOUT,
				  error)) {
		return FALSE;
	}
	fu_dump_raw(G_LOG_DOMAIN, "GetFeature", buf, bufsz);

	/* prepend the report-id and cmd for versions of bluez that do not have
	 * https://github.com/bluez/bluez/commit/35a2c50437cca4d26ac6537ce3a964bb509c9b62 */
	if (bufsz > 2 && buf[0] != self->feature_report_id) {
		g_debug("doing fixup for old bluez version");
		memmove(buf + 2, buf, bufsz - 2);
		buf[0] = self->feature_report_id;
		buf[1] = 0x0;
	}

	return TRUE;
#else
	g_set_error_literal(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "<linux/hidraw.h> not available");
	return FALSE;
#endif
}

static gboolean
fu_pxi_ble_device_search_hid_feature_report_id(FuFirmware *descriptor,
					       guint16 usage_page,
					       guint8 *report_id,
					       GError **error)
{
	g_autoptr(FuFirmware) item_id = NULL;
	g_autoptr(FuHidReport) report = NULL;

	/* check ota retransmit feature report usage page exists */
	report = fu_hid_descriptor_find_report(FU_HID_DESCRIPTOR(descriptor),
					       error,
					       "usage-page",
					       usage_page,
					       "usage",
					       0x01,
					       "feature",
					       0x02,
					       NULL);
	if (report == NULL)
		return FALSE;

	/* find report-id */
	item_id = fu_firmware_get_image_by_id(FU_FIRMWARE(report), "report-id", error);
	if (item_id == NULL)
		return FALSE;

	/* success */
	*report_id = fu_hid_report_item_get_value(FU_HID_REPORT_ITEM(item_id));
	return TRUE;
}

static gboolean
fu_pxi_ble_device_search_hid_input_report_id(FuFirmware *descriptor,
					     guint16 usage_page,
					     guint8 *report_id,
					     GError **error)
{
	g_autoptr(FuHidReport) report = NULL;
	g_autoptr(FuFirmware) item_id = NULL;

	/* check ota retransmit feature report usage page exist or not */
	report = fu_hid_descriptor_find_report(FU_HID_DESCRIPTOR(descriptor),
					       error,
					       "usage-page",
					       usage_page,
					       "usage",
					       0x01,
					       "input",
					       0x02,
					       NULL);
	if (report == NULL)
		return FALSE;

	/* find report-id */
	item_id = fu_firmware_get_image_by_id(FU_FIRMWARE(report), "report-id", error);
	if (item_id == NULL)
		return FALSE;

	/* success */
	*report_id = fu_hid_report_item_get_value(FU_HID_REPORT_ITEM(item_id));
	return TRUE;
}

static gboolean
fu_pxi_ble_device_check_support_report_id(FuPxiBleDevice *self, GError **error)
{
#ifdef HAVE_HIDRAW_H
	gint desc_size = 0;
	g_autoptr(FuFirmware) descriptor = fu_hid_descriptor_new();
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GError) error_local1 = NULL;
	g_autoptr(GError) error_local2 = NULL;
	g_autoptr(GError) error_local3 = NULL;
	g_autoptr(GError) error_local = NULL;

	struct hidraw_report_descriptor rpt_desc = {0x0};

	/* Get Report Descriptor Size */
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  HIDIOCGRDESCSIZE,
				  (guint8 *)&desc_size,
				  NULL,
				  FU_PXI_DEVICE_IOCTL_TIMEOUT,
				  error))
		return FALSE;

	rpt_desc.size = desc_size;
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  HIDIOCGRDESC,
				  (guint8 *)&rpt_desc,
				  NULL,
				  FU_PXI_DEVICE_IOCTL_TIMEOUT,
				  error))
		return FALSE;
	fu_dump_raw(G_LOG_DOMAIN, "HID descriptor", rpt_desc.value, rpt_desc.size);

	/* parse the descriptor, but use the defaults if it fails */
	fw = g_bytes_new(rpt_desc.value, rpt_desc.size);
	if (!fu_firmware_parse(descriptor, fw, FWUPD_INSTALL_FLAG_NONE, &error_local)) {
		g_debug("failed to parse descriptor: %s", error_local->message);
		return TRUE;
	}

	/* check ota retransmit feature report usage page exists */
	if (!fu_pxi_ble_device_search_hid_feature_report_id(descriptor,
							    PXI_HID_DEV_OTA_RETRANSMIT_USAGE_PAGE,
							    &self->retransmit_id,
							    &error_local1)) {
		g_debug("failed to parse descriptor: %s", error_local1->message);
	}
	g_debug("usage-page: 0x%x retransmit_id: %d",
		PXI_HID_DEV_OTA_RETRANSMIT_USAGE_PAGE,
		self->retransmit_id);

	/* check ota feature report usage page exists */
	if (!fu_pxi_ble_device_search_hid_feature_report_id(descriptor,
							    PXI_HID_DEV_OTA_REPORT_USAGE_PAGE,
							    &self->feature_report_id,
							    &error_local2)) {
		g_debug("failed to parse descriptor: %s", error_local2->message);
	}
	g_debug("usage-page: 0x%x feature_report_id: %d",
		PXI_HID_DEV_OTA_RETRANSMIT_USAGE_PAGE,
		self->retransmit_id);

	/* check ota notify input report usage page exist or not */
	if (!fu_pxi_ble_device_search_hid_input_report_id(descriptor,
							  PXI_HID_DEV_OTA_NOTIFY_USAGE_PAGE,
							  &self->input_report_id,
							  &error_local3)) {
		g_debug("failed to parse descriptor: %s", error_local3->message);
	}
	g_debug("usage-page: 0x%x input_report_id: %d",
		PXI_HID_DEV_OTA_RETRANSMIT_USAGE_PAGE,
		self->input_report_id);

	/* success */
	return TRUE;
#else
	g_set_error_literal(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "<linux/hidraw.h> not available");
	return FALSE
#endif
}

static gboolean
fu_pxi_ble_device_fw_ota_check_retransmit(FuPxiBleDevice *self, GError **error)
{
	g_autoptr(GByteArray) req = g_byte_array_new();

	/* write fw ota retransmit command to reset the ota state */
	fu_byte_array_append_uint8(req, self->retransmit_id);
	fu_byte_array_append_uint8(req, FU_PXI_DEVICE_CMD_FW_OTA_RETRANSMIT);
	return fu_pxi_ble_device_set_feature(self, req, error);
}

static gboolean
fu_pxi_ble_device_check_support_resume(FuPxiBleDevice *self,
				       FuFirmware *firmware,
				       FuProgress *progress,
				       GError **error)
{
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;
	guint16 checksum_tmp = 0x0;

	/* get the default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* check offset is invalid or not */
	chunks = fu_chunk_array_new_from_bytes(fw, 0x0, FU_PXI_DEVICE_OBJECT_SIZE_MAX);
	if (self->fwstate.offset > fu_chunk_array_length(chunks)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "offset from device is invalid: "
			    "got 0x%x, current maximum 0x%x",
			    self->fwstate.offset,
			    fu_chunk_array_length(chunks));
		return FALSE;
	}

	/* calculate device current checksum */
	for (guint i = 0; i < self->fwstate.offset; i++) {
		g_autoptr(FuChunk) chk = fu_chunk_array_index(chunks, i);
		checksum_tmp += fu_sum16(fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));
	}

	/* check current file is different with previous fw bin or not */
	if (self->fwstate.checksum != checksum_tmp) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "checksum is different from previous fw: "
			    "got 0x%04x, expected 0x%04x",
			    self->fwstate.checksum,
			    checksum_tmp);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_ble_device_wait_notify(FuPxiBleDevice *self,
			      goffset port,
			      guint8 *status,
			      guint16 *checksum,
			      GError **error)
{
	g_autoptr(GTimer) timer = g_timer_new();
	guint8 res[FU_PXI_BLE_DEVICE_OTA_BUF_SZ] = {0};
	guint8 cmd_status = 0x0;

	/* skip the wrong report id ,and keep polling until result is correct */
	while (g_timer_elapsed(timer, NULL) * 1000.f < FU_PXI_BLE_DEVICE_NOTIFY_TIMEOUT_MS) {
		if (!fu_udev_device_pread(FU_UDEV_DEVICE(self),
					  port,
					  res,
					  (FU_PXI_BLE_DEVICE_NOTIFY_RET_LEN + 1) - port,
					  error))
			return FALSE;
		if (res[0] == self->input_report_id)
			break;
	}

	/* timeout */
	if (res[0] != self->input_report_id) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "Timed-out waiting for HID report");
		return FALSE;
	}
	/* get the opcode if status is not null */
	if (status != NULL) {
		guint8 status_tmp = 0x0;
		if (!fu_memread_uint8_safe(res, sizeof(res), 0x1, &status_tmp, error))
			return FALSE;
		/* need check command result if command is fw upgrade */
		if (status_tmp == FU_PXI_DEVICE_CMD_FW_UPGRADE) {
			if (!fu_memread_uint8_safe(res, sizeof(res), 0x2, &cmd_status, error))
				return FALSE;
			if (cmd_status != ERR_COMMAND_SUCCESS) {
				g_set_error(error,
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
		if (!fu_memread_uint16_safe(res,
					    sizeof(res),
					    0x3,
					    checksum,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_ble_device_fw_object_create(FuPxiBleDevice *self, FuChunk *chk, GError **error)
{
	guint8 opcode = 0;
	g_autoptr(GByteArray) req = g_byte_array_new();

	/* request */
	fu_byte_array_append_uint8(req, self->feature_report_id);
	fu_byte_array_append_uint8(req, FU_PXI_DEVICE_CMD_FW_OBJECT_CREATE);
	fu_byte_array_append_uint32(req, fu_chunk_get_address(chk), G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(req, fu_chunk_get_data_sz(chk), G_LITTLE_ENDIAN);
	if (!fu_pxi_ble_device_set_feature(self, req, error))
		return FALSE;

	/* check object create success or not */
	if (!fu_pxi_ble_device_wait_notify(self, 0x0, &opcode, NULL, error))
		return FALSE;
	if (opcode != FU_PXI_DEVICE_CMD_FW_OBJECT_CREATE) {
		g_set_error(error,
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
fu_pxi_ble_device_write_payload(FuPxiBleDevice *self, FuChunk *chk, GError **error)
{
	g_autoptr(GByteArray) req = g_byte_array_new();
	fu_byte_array_append_uint8(req, self->feature_report_id);
	g_byte_array_append(req, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));
	return fu_pxi_ble_device_set_feature(self, req, error);
}

static gboolean
fu_pxi_ble_device_write_chunk(FuPxiBleDevice *self, FuChunk *chk, GError **error)
{
	guint32 prn = 0;
	guint16 checksum;
	guint16 checksum_device = 0;
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GBytes) chk_bytes = fu_chunk_get_bytes(chk);

	/* send create fw object command */
	if (!fu_pxi_ble_device_fw_object_create(self, chk, error))
		return FALSE;

	/* write payload */
	chunks = fu_chunk_array_new_from_bytes(chk_bytes,
					       fu_chunk_get_address(chk),
					       self->fwstate.mtu_size);
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk2 = fu_chunk_array_index(chunks, i);
		if (!fu_pxi_ble_device_write_payload(self, chk2, error))
			return FALSE;
		prn++;
		/* wait notify from device when PRN over threshold write or
		 * offset reach max object sz or write offset reach fw length */
		if (prn >= self->fwstate.prn_threshold || i == fu_chunk_array_length(chunks) - 1) {
			guint8 opcode = 0;
			if (!fu_pxi_ble_device_wait_notify(self,
							   0x0,
							   &opcode,
							   &checksum_device,
							   error))
				return FALSE;
			if (opcode != FU_PXI_DEVICE_CMD_FW_WRITE) {
				g_set_error(error,
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
	checksum = fu_sum16(fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));
	self->fwstate.checksum += checksum;
	if (checksum_device != self->fwstate.checksum) {
		g_set_error(error,
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
fu_pxi_ble_device_reset(FuPxiBleDevice *self, GError **error)
{
	g_autoptr(GByteArray) req = g_byte_array_new();
	fu_byte_array_append_uint8(req, self->feature_report_id);
	fu_byte_array_append_uint8(req, FU_PXI_DEVICE_CMD_FW_MCU_RESET); /* OTA reset command */
	fu_byte_array_append_uint8(req, OTA_RESET);			 /* OTA reset reason  */

	if (!fu_pxi_ble_device_set_feature(self, req, error)) {
		g_prefix_error(error, "failed to reset: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_ble_device_fw_ota_init(FuPxiBleDevice *self, GError **error)
{
	g_autoptr(GByteArray) req = g_byte_array_new();

	/* write fw ota init command */
	fu_byte_array_append_uint8(req, self->feature_report_id);
	fu_byte_array_append_uint8(req, FU_PXI_DEVICE_CMD_FW_OTA_INIT);
	return fu_pxi_ble_device_set_feature(self, req, error);
}

static gboolean
fu_pxi_ble_device_fw_ota_init_new(FuPxiBleDevice *self, gsize bufsz, GError **error)
{
	guint8 res[FU_PXI_BLE_DEVICE_OTA_BUF_SZ] = {0x0};
	guint8 fw_version[10] = {0x0};
	g_autoptr(GByteArray) req = g_byte_array_new();

	/* write fw ota init new command */
	fu_byte_array_append_uint8(req, self->feature_report_id);
	fu_byte_array_append_uint8(req, FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW);
	fu_byte_array_append_uint32(req, bufsz, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint8(req, 0x0); /* OTA setting */
	g_byte_array_append(req, fw_version, sizeof(fw_version));
	if (!fu_pxi_ble_device_set_feature(self, req, error))
		return FALSE;

	/* delay for BLE device read command */
	fu_device_sleep(FU_DEVICE(self), 10); /* ms */

	/* read fw ota init new command */
	res[0] = self->feature_report_id;
	res[1] = FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW;
	if (!fu_pxi_ble_device_get_feature(self, res, sizeof(res), error))
		return FALSE;

	/* shared state */
	if (!fu_pxi_ota_fw_state_parse(&self->fwstate, res, sizeof(res), 0x05, error))
		return FALSE;
	if (self->fwstate.spec_check_result != FU_PXI_OTA_SPEC_CHECK_RESULT_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "FwInitNew spec check fail: %s [0x%02x]",
			    fu_pxi_ota_spec_check_result_to_string(self->fwstate.spec_check_result),
			    self->fwstate.spec_check_result);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_ble_device_fw_upgrade(FuPxiBleDevice *self,
			     FuFirmware *firmware,
			     FuProgress *progress,
			     GError **error)
{
	const gchar *version;
	guint8 fw_version[5] = {0x0};
	guint8 opcode = 0;
	guint16 checksum;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GByteArray) req = g_byte_array_new();

	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;
	checksum = fu_sum16_bytes(fw);
	fu_byte_array_append_uint8(req, self->feature_report_id);
	fu_byte_array_append_uint8(req, FU_PXI_DEVICE_CMD_FW_UPGRADE);
	fu_byte_array_append_uint32(req, g_bytes_get_size(fw), G_LITTLE_ENDIAN);
	fu_byte_array_append_uint16(req, checksum, G_LITTLE_ENDIAN);
	if (!fu_device_has_private_flag(FU_DEVICE(self), FU_PXI_DEVICE_FLAG_IS_HPAC)) {
		version = fu_firmware_get_version(firmware);
		if (!fu_memcpy_safe(fw_version,
				    sizeof(fw_version),
				    0x0, /* dst */
				    (guint8 *)version,
				    strlen(version),
				    0x0, /* src */
				    strlen(version),
				    error))
			return FALSE;
	}
	g_byte_array_append(req, fw_version, sizeof(fw_version));

	/* send fw upgrade command */
	if (!fu_pxi_ble_device_set_feature(self, req, error))
		return FALSE;

	fu_dump_raw(G_LOG_DOMAIN, "fw upgrade", req->data, req->len);

	/* wait fw upgrade command result */
	if (!fu_pxi_ble_device_wait_notify(self, 0x1, &opcode, NULL, error)) {
		g_prefix_error(error,
			       "FwUpgrade command fail, "
			       "fw-checksum: 0x%04x fw-size: %" G_GSIZE_FORMAT ": ",
			       checksum,
			       g_bytes_get_size(fw));
		return FALSE;
	}
	if (opcode != FU_PXI_DEVICE_CMD_FW_UPGRADE) {
		g_set_error(error,
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
fu_pxi_ble_device_write_firmware(FuDevice *device,
				 FuFirmware *firmware,
				 FuProgress *progress,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuPxiBleDevice *self = FU_PXI_BLE_DEVICE(device);
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GError) error_local = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "ota-init");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 0, "check-support-resume");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 0, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, NULL);

	/* get the default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* send fw ota retransmit command to reset status */
	if (!fu_pxi_ble_device_fw_ota_check_retransmit(self, error)) {
		g_prefix_error(error, "failed to OTA check retransmit: ");
		return FALSE;
	}
	/* send fw ota init command */
	if (!fu_pxi_ble_device_fw_ota_init(self, error))
		return FALSE;
	if (!fu_pxi_ble_device_fw_ota_init_new(self, g_bytes_get_size(fw), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* prepare write fw into device */
	chunks = fu_chunk_array_new_from_bytes(fw, 0x0, FU_PXI_DEVICE_OBJECT_SIZE_MAX);
	if (!fu_pxi_ble_device_check_support_resume(self,
						    firmware,
						    fu_progress_get_child(progress),
						    &error_local)) {
		g_debug("do not resume: %s", error_local->message);
		self->fwstate.offset = 0;
		self->fwstate.checksum = 0;
	}
	fu_progress_step_done(progress);

	/* write fw into device */
	for (guint i = self->fwstate.offset; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = fu_chunk_array_index(chunks, i);
		if (!fu_pxi_ble_device_write_chunk(self, chk, error))
			return FALSE;
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						(gsize)self->fwstate.offset + 1,
						(gsize)fu_chunk_array_length(chunks));
	}
	fu_progress_step_done(progress);

	/* fw upgrade command */
	if (!fu_pxi_ble_device_fw_upgrade(self, firmware, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* send device reset command */
	if (!fu_pxi_ble_device_reset(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_ble_device_fw_get_info(FuPxiBleDevice *self, GError **error)
{
	guint8 res[FU_PXI_BLE_DEVICE_OTA_BUF_SZ] = {0x0};
	guint8 opcode = 0x0;
	guint16 checksum = 0;
	guint16 hpac_ver = 0;
	g_autofree gchar *version_str = NULL;
	g_autoptr(GByteArray) req = g_byte_array_new();

	fu_byte_array_append_uint8(req, self->feature_report_id);
	fu_byte_array_append_uint8(req, FU_PXI_DEVICE_CMD_FW_GET_INFO);
	if (!fu_pxi_ble_device_set_feature(self, req, error))
		return FALSE;

	/* delay for BLE device read command */
	fu_device_sleep(FU_DEVICE(self), 10); /* ms */

	res[0] = self->feature_report_id;
	res[1] = FU_PXI_DEVICE_CMD_FW_GET_INFO;

	if (!fu_pxi_ble_device_get_feature(self, res, FU_PXI_BLE_DEVICE_FW_INFO_RET_LEN + 3, error))
		return FALSE;
	if (!fu_memread_uint8_safe(res, sizeof(res), 0x4, &opcode, error))
		return FALSE;
	if (opcode != FU_PXI_DEVICE_CMD_FW_GET_INFO) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "FwGetInfo opcode invalid 0x%02x",
			    opcode);
		return FALSE;
	}
	/* set current version */
	if (!fu_device_has_private_flag(FU_DEVICE(self), FU_PXI_DEVICE_FLAG_IS_HPAC)) {
		version_str = g_strndup((gchar *)res + 0x6, 5);
	} else {
		if (!fu_memread_uint16_safe(res,
					    FU_PXI_BLE_DEVICE_OTA_BUF_SZ,
					    9,
					    &hpac_ver,
					    G_BIG_ENDIAN,
					    error))
			return FALSE;

		version_str = fu_pxi_hpac_version_info_parse(hpac_ver);
	}
	fu_device_set_version(FU_DEVICE(self), version_str);

	/* add current checksum */
	if (!fu_memread_uint16_safe(res, sizeof(res), 0xb, &checksum, G_LITTLE_ENDIAN, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_ble_device_get_model_info(FuPxiBleDevice *self, GError **error)
{
	guint8 res[FU_PXI_BLE_DEVICE_OTA_BUF_SZ] = {0x0};
	guint8 opcode = 0x0;
	guint8 model_name[FU_PXI_DEVICE_MODEL_NAME_LEN] = {0x0};
	g_autoptr(GByteArray) req = g_byte_array_new();

	fu_byte_array_append_uint8(req, self->feature_report_id);
	fu_byte_array_append_uint8(req, FU_PXI_DEVICE_CMD_FW_OTA_GET_MODEL);

	if (!fu_pxi_ble_device_set_feature(self, req, error))
		return FALSE;

	/* delay for BLE device read command */
	fu_device_sleep(FU_DEVICE(self), 10); /* ms */

	res[0] = self->feature_report_id;
	if (!fu_pxi_ble_device_get_feature(self, res, sizeof(res), error))
		return FALSE;
	if (!fu_memread_uint8_safe(res, sizeof(res), 0x4, &opcode, error))
		return FALSE;

	/* old firmware */
	if (opcode != FU_PXI_DEVICE_CMD_FW_OTA_GET_MODEL)
		return TRUE;

	/* get model from res */
	if (!fu_memcpy_safe(model_name,
			    sizeof(model_name),
			    0x0, /* dst */
			    (guint8 *)res,
			    sizeof(res),
			    0x6, /* src */
			    sizeof(model_name),
			    error))
		return FALSE;
	g_clear_pointer(&self->model_name, g_free);
	if (model_name[0] != 0x00 && model_name[0] != 0xFF)
		self->model_name = g_strndup((gchar *)model_name, sizeof(model_name));

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_ble_device_probe(FuDevice *device, GError **error)
{
	/* set the logical and physical ID */
	if (!fu_udev_device_set_logical_id(FU_UDEV_DEVICE(device), "hid", error))
		return FALSE;
	return fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "hid", error);
}

static gboolean
fu_pxi_ble_device_setup_guid(FuPxiBleDevice *self, GError **error)
{
#ifdef HAVE_HIDRAW_H
	FuDevice *device = FU_DEVICE(self);
	struct hidraw_devinfo hid_raw_info = {0x0};
	g_autoptr(GString) dev_name = NULL;
	g_autoptr(GString) model_name = NULL;

	/* extra GUID with device name */
	if (!fu_pxi_ble_device_get_raw_info(self, &hid_raw_info, error))
		return FALSE;
	dev_name = g_string_new(fu_device_get_name(device));
	g_string_ascii_up(dev_name);
	g_string_replace(dev_name, " ", "_", 0);

	/* extra GUID with model name*/
	model_name = g_string_new(self->model_name);
	g_string_ascii_up(model_name);
	g_string_replace(model_name, " ", "_", 0);

	/* generate IDs */
	fu_device_add_instance_u16(device, "VEN", hid_raw_info.vendor);
	fu_device_add_instance_u16(device, "DEV", hid_raw_info.product);
	fu_device_add_instance_str(device, "NAME", dev_name->str);
	fu_device_add_instance_str(device, "MODEL", model_name->str);
	if (!fu_device_build_instance_id(device, error, "HIDRAW", "VEN", "DEV", "NAME", NULL))
		return FALSE;
	if (!fu_device_build_instance_id(device, error, "HIDRAW", "VEN", "DEV", "MODEL", NULL))
		return FALSE;
#endif
	return TRUE;
}

static gboolean
fu_pxi_ble_device_setup(FuDevice *device, GError **error)
{
	FuPxiBleDevice *self = FU_PXI_BLE_DEVICE(device);

	if (!fu_pxi_ble_device_check_support_report_id(self, error)) {
		g_prefix_error(error, "failed to check report id: ");
		return FALSE;
	}
	if (!fu_pxi_ble_device_fw_ota_check_retransmit(self, error)) {
		g_prefix_error(error, "failed to OTA check retransmit: ");
		return FALSE;
	}
	if (!fu_pxi_ble_device_fw_ota_init(self, error)) {
		g_prefix_error(error, "failed to OTA init: ");
		return FALSE;
	}
	if (!fu_pxi_ble_device_fw_get_info(self, error)) {
		g_prefix_error(error, "failed to get info: ");
		return FALSE;
	}
	if (!fu_pxi_ble_device_get_model_info(self, error)) {
		g_prefix_error(error, "failed to get model: ");
		return FALSE;
	}
	if (!fu_pxi_ble_device_setup_guid(self, error)) {
		g_prefix_error(error, "failed to setup GUID: ");
		return FALSE;
	}

	return TRUE;
}

static void
fu_pxi_ble_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_pxi_ble_device_init(FuPxiBleDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_vendor_id(FU_DEVICE(self), "USB:0x093A");
	fu_device_add_protocol(FU_DEVICE(self), "com.pixart.rf");
	fu_device_retry_set_delay(FU_DEVICE(self), 50);
	self->retransmit_id = PXI_HID_DEV_OTA_RETRANSMIT_REPORT_ID;
	self->feature_report_id = PXI_HID_DEV_OTA_FEATURE_REPORT_ID;
	self->input_report_id = PXI_HID_DEV_OTA_INPUT_REPORT_ID;
	fu_device_register_private_flag(FU_DEVICE(self), FU_PXI_DEVICE_FLAG_IS_HPAC, "is-hpac");
}

static void
fu_pxi_ble_device_finalize(GObject *object)
{
	FuPxiBleDevice *self = FU_PXI_BLE_DEVICE(object);
	g_free(self->model_name);
	G_OBJECT_CLASS(fu_pxi_ble_device_parent_class)->finalize(object);
}

static void
fu_pxi_ble_device_class_init(FuPxiBleDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_pxi_ble_device_finalize;
	klass_device->probe = fu_pxi_ble_device_probe;
	klass_device->setup = fu_pxi_ble_device_setup;
	klass_device->to_string = fu_pxi_ble_device_to_string;
	klass_device->write_firmware = fu_pxi_ble_device_write_firmware;
	klass_device->prepare_firmware = fu_pxi_ble_device_prepare_firmware;
	klass_device->set_progress = fu_pxi_ble_device_set_progress;
}
