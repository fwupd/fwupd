/*
 * Copyright 2024 Logitech, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <linux/types.h>
#include <linux/usb/video.h>
#include <linux/uvcvideo.h>
#ifdef HAVE_IOCTL_H
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#endif

#include <string.h>

#include "fu-logitech-tap-struct.h"
#include "fu-logitech-tap-touch-common.h"
#include "fu-logitech-tap-touch-device.h"
#include "fu-logitech-tap-touch-firmware.h"

#define FU_LOGITECH_TAP_TOUCH_IOCTL_TIMEOUT 5000 /* ms */

#define FU_LOGITECH_TAP_TOUCH_HID_SET_DATA_LEN 64
#define FU_LOGITECH_TAP_TOUCH_HID_GET_DATA_LEN 64

#define FU_LOGITECH_TAP_TOUCH_HID_RESPONSE_OFFSET                                                  \
	4 /* Skip first 4 header bytes from response buffer */

#define FU_LOGITECH_TAP_TOUCH_HID_REPORT_ID 0x03

#define FU_LOGITECH_TAP_TOUCH_TRANSFER_BLOCK_SIZE 32

#define FU_LOGITECH_TAP_TOUCH_AP_MODE 0x5A /* device in Application mode */
#define FU_LOGITECH_TAP_TOUCH_BL_MODE 0x55 /* device in Bootloader mode */

#define FU_LOGITECH_TAP_TOUCH_MAX_GET_RETRY_COUNT	 50
#define FU_LOGITECH_TAP_TOUCH_MAX_BUSY_CHECK_RETRY_COUNT 50
#define FU_LOGITECH_TAP_TOUCH_MAX_FW_WRITE_RETRIES	 3

#define FU_LOGITECH_TAP_TOUCH_SYSTEM_READY 0x50 /* wait and retry if device not ready */

/* usb bus type */
#define FU_LOGITECH_TAP_TOUCH_DEVICE_INFO_BUS_TYPE 0x03

struct _FuLogitechTapTouchDevice {
	FuHidrawDevice parent_instance;
};

G_DEFINE_TYPE(FuLogitechTapTouchDevice, fu_logitech_tap_touch_device, FU_TYPE_HIDRAW_DEVICE)

static gboolean
fu_logitech_tap_touch_device_get_feature_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuLogitechTapTouchDevice *self = FU_LOGITECH_TAP_TOUCH_DEVICE(device);
	GByteArray *outbuffer = (GByteArray *)user_data;
	gboolean ret;
	guint8 report_id = 0;
	g_autoptr(GError) error_local = NULL;

	ret = fu_udev_device_pread(FU_UDEV_DEVICE(self),
				   0x0,
				   (guint8 *)outbuffer->data,
				   outbuffer->len,
				   &error_local);
	if (!fu_memread_uint8_safe((guint8 *)outbuffer->data,
				   outbuffer->len,
				   0x00U,
				   &report_id,
				   error)) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "failed to read report id: ");
		return FALSE;
	}
	if (!ret) {
		if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_INTERNAL) ||
		    report_id != FU_LOGITECH_TAP_TOUCH_HID_REPORT_ID) {
			g_propagate_prefixed_error(error,
						   g_steal_pointer(&error_local),
						   "failed to read report: ");
			return FALSE;
		}
		g_propagate_prefixed_error(error,
					   g_steal_pointer(&error_local),
					   "failed to read report: ");
		return FALSE;
	}
	fu_dump_raw(G_LOG_DOMAIN, "HidGetFeatureResponse", outbuffer->data, outbuffer->len);

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_tap_touch_device_hid_transfer(FuLogitechTapTouchDevice *self,
					  GByteArray *st_req,
					  guint delay,
					  GByteArray *buf_res,
					  GError **error)
{
	fu_byte_array_set_size(st_req, FU_LOGITECH_TAP_TOUCH_HID_SET_DATA_LEN, 0x0);
	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  st_req->data,
					  st_req->len,
					  FU_UDEV_DEVICE_IOCTL_FLAG_RETRY,
					  error)) {
		g_prefix_error(error, "failed to send packet to touch panel: ");
		return FALSE;
	}
	/* check if there is a corresponding get report request.
	 * If so, wait for specified duration before submitting get report */
	if (buf_res != NULL) {
		fu_byte_array_set_size(buf_res, FU_LOGITECH_TAP_TOUCH_HID_GET_DATA_LEN, 0x0);
		fu_device_sleep(FU_DEVICE(self), delay);
		if (!fu_device_retry_full(FU_DEVICE(self),
					  fu_logitech_tap_touch_device_get_feature_cb,
					  FU_LOGITECH_TAP_TOUCH_MAX_GET_RETRY_COUNT,
					  delay,
					  buf_res,
					  error)) {
			g_prefix_error(error, "failed to receive packet from touch panel: ");
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_tap_touch_device_enable_tde(FuDevice *device, GError **error)
{
	FuLogitechTapTouchDevice *self = FU_LOGITECH_TAP_TOUCH_DEVICE(device);
	g_autoptr(FuStructLogitechTapTouchHidReq) st = fu_struct_logitech_tap_touch_hid_req_new();

	/* hid report to put device into suspend mode */
	fu_struct_logitech_tap_touch_hid_req_set_payload_len(st, 0x02);
	fu_struct_logitech_tap_touch_hid_req_set_response_len(st, 0x00);
	fu_struct_logitech_tap_touch_hid_req_set_cmd(
	    st,
	    FU_STRUCT_LOGITECH_TAP_TOUCH_HID_CMD_SET_TDE_TEST_MODE);
	fu_byte_array_append_uint8(st, 0x01);
	return fu_logitech_tap_touch_device_hid_transfer(self, st, 0, NULL, error);
}

static gboolean
fu_logitech_tap_touch_device_disable_tde(FuDevice *device, GError **error)
{
	FuLogitechTapTouchDevice *self = FU_LOGITECH_TAP_TOUCH_DEVICE(device);
	g_autoptr(FuStructLogitechTapTouchHidReq) st = fu_struct_logitech_tap_touch_hid_req_new();

	fu_struct_logitech_tap_touch_hid_req_set_payload_len(st, 0x02);
	fu_struct_logitech_tap_touch_hid_req_set_response_len(st, 0x0);
	fu_struct_logitech_tap_touch_hid_req_set_cmd(
	    st,
	    FU_STRUCT_LOGITECH_TAP_TOUCH_HID_CMD_SET_TDE_TEST_MODE);
	fu_byte_array_append_uint8(st, 0x00);
	return fu_logitech_tap_touch_device_hid_transfer(self, st, 0, NULL, error);
}

static gboolean
fu_logitech_tap_touch_device_write_enable(FuLogitechTapTouchDevice *self,
					  gboolean in_ap,
					  gboolean write_ap,
					  guint32 end,
					  guint32 checksum,
					  GError **error)
{
	guint8 delay;
	g_autoptr(FuStructLogitechTapTouchHidReq) st = fu_struct_logitech_tap_touch_hid_req_new();

	if (in_ap) {
		fu_struct_logitech_tap_touch_hid_req_set_payload_len(st, 0x03);
		delay = 100;
	} else {
		fu_struct_logitech_tap_touch_hid_req_set_payload_len(st, 0x0A);
		delay = 10;
	}
	fu_struct_logitech_tap_touch_hid_req_set_response_len(st, 0x0);
	fu_struct_logitech_tap_touch_hid_req_set_cmd(
	    st,
	    FU_STRUCT_LOGITECH_TAP_TOUCH_HID_CMD_WRITE_ENABLE);
	fu_byte_array_append_uint8(st, 0x5A);
	fu_byte_array_append_uint8(st, 0xA5);
	if (end > 0) {
		fu_byte_array_append_uint8(st, write_ap ? 0x00 : 0x01);
		fu_byte_array_append_uint24(st, end, G_BIG_ENDIAN);
		fu_byte_array_append_uint24(st, checksum, G_BIG_ENDIAN);
	}

	/* hid report to enable writing */
	if (!fu_logitech_tap_touch_device_hid_transfer(self, st, 0, NULL, error))
		return FALSE;

	/* mode switch delay for application/bootloader */
	fu_device_sleep(FU_DEVICE(self), delay);

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_tap_touch_device_check_busy_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuLogitechTapTouchDevice *self = FU_LOGITECH_TAP_TOUCH_DEVICE(device);
	guint8 hid_response = 0;
	g_autoptr(GByteArray) buf_res = g_byte_array_new();
	g_autoptr(FuStructLogitechTapTouchHidReq) st = fu_struct_logitech_tap_touch_hid_req_new();

	/* hid report to query device busy or idle status */
	fu_struct_logitech_tap_touch_hid_req_set_payload_len(st, 0x01);
	fu_struct_logitech_tap_touch_hid_req_set_response_len(st, 0x01);
	fu_struct_logitech_tap_touch_hid_req_set_cmd(
	    st,
	    FU_STRUCT_LOGITECH_TAP_TOUCH_HID_CMD_GET_SYS_BUSY_STATUS);
	if (!fu_logitech_tap_touch_device_hid_transfer(self, st, 5, buf_res, error))
		return FALSE;
	if (!fu_memread_uint8_safe(buf_res->data,
				   buf_res->len,
				   FU_LOGITECH_TAP_TOUCH_HID_RESPONSE_OFFSET,
				   &hid_response,
				   error))
		return FALSE;
	if (hid_response != FU_LOGITECH_TAP_TOUCH_SYSTEM_READY) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "device busy check failed, got:  0x%02x, expected: %i",
			    hid_response,
			    FU_LOGITECH_TAP_TOUCH_SYSTEM_READY);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_tap_touch_device_get_crc(FuLogitechTapTouchDevice *self,
				     guint16 *crc,
				     guint8 datasz,
				     GError **error)
{
	g_autoptr(FuStructLogitechTapTouchHidReq) st = fu_struct_logitech_tap_touch_hid_req_new();
	fu_struct_logitech_tap_touch_hid_req_set_payload_len(st, 0x01);

	if (crc != NULL)
		fu_struct_logitech_tap_touch_hid_req_set_response_len(st, datasz);
	else
		fu_struct_logitech_tap_touch_hid_req_set_response_len(st, 0x0);
	fu_struct_logitech_tap_touch_hid_req_set_cmd(
	    st,
	    FU_STRUCT_LOGITECH_TAP_TOUCH_HID_CMD_GET_AP_CRC);

	/* hid report to query crc info of dataflash/pflash (DF/AP) block */
	if (crc != NULL) {
		g_autoptr(GByteArray) buf_res = g_byte_array_new();
		if (!fu_logitech_tap_touch_device_hid_transfer(self, st, 100, buf_res, error))
			return FALSE;
		if (!fu_memread_uint16_safe(buf_res->data,
					    buf_res->len,
					    FU_LOGITECH_TAP_TOUCH_HID_RESPONSE_OFFSET,
					    crc,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
	} else {
		if (!fu_logitech_tap_touch_device_hid_transfer(self, st, 0, NULL, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_tap_touch_device_get_mcu_mode(FuLogitechTapTouchDevice *self,
					  guint8 *mcu_mode,
					  GError **error)
{
	g_autoptr(GByteArray) buf_res = g_byte_array_new();
	g_autoptr(FuStructLogitechTapTouchHidReq) st = fu_struct_logitech_tap_touch_hid_req_new();

	fu_struct_logitech_tap_touch_hid_req_set_payload_len(st, 0x01);
	fu_struct_logitech_tap_touch_hid_req_set_response_len(st, 0x2);
	fu_struct_logitech_tap_touch_hid_req_set_cmd(
	    st,
	    FU_STRUCT_LOGITECH_TAP_TOUCH_HID_CMD_GET_MCU_MODE);

	/* hid report to query current mode, application (AP) or bootloader (BL) mode */
	if (!fu_logitech_tap_touch_device_hid_transfer(self, st, 100, buf_res, error))
		return FALSE;

	return fu_memread_uint8_safe(buf_res->data,
				     buf_res->len,
				     FU_LOGITECH_TAP_TOUCH_HID_RESPONSE_OFFSET,
				     mcu_mode,
				     error);
}

static gboolean
fu_logitech_tap_touch_device_check_ic_name(FuLogitechTapTouchDevice *self, GError **error)
{
	guint16 ic_name = 0;
	g_autoptr(FuStructLogitechTapTouchHidReq) st = fu_struct_logitech_tap_touch_hid_req_new();
	g_autoptr(GByteArray) buf_res = g_byte_array_new();

	fu_struct_logitech_tap_touch_hid_req_set_payload_len(st, 0x01);
	fu_struct_logitech_tap_touch_hid_req_set_response_len(st, 0x20);
	fu_struct_logitech_tap_touch_hid_req_set_cmd(
	    st,
	    FU_STRUCT_LOGITECH_TAP_TOUCH_HID_CMD_GET_MCU_VERSION);
	if (!fu_logitech_tap_touch_device_hid_transfer(self, st, 100, buf_res, error))
		return FALSE;
	if (!fu_memread_uint16_safe(buf_res->data,
				    buf_res->len,
				    FU_LOGITECH_TAP_TOUCH_HID_RESPONSE_OFFSET,
				    &ic_name,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (ic_name != FU_LOGITECH_TAP_TOUCH_IC_NAME) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to get supported ic: %x",
			    ic_name);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_tap_touch_device_ensure_version(FuLogitechTapTouchDevice *self, GError **error)
{
	guint64 version_raw = 0;
	g_autoptr(FuStructLogitechTapTouchHidReq) st = fu_struct_logitech_tap_touch_hid_req_new();
	g_autoptr(GByteArray) buf_res = g_byte_array_new();

	/*
	 * hid report to query version info
	 * Firmware updater available/supported from only 2 display panel vendors.
	 * All vendors use same VID/PID, only way to determine supported vendor is to analyze
	 * version. Version is 8 bytes, and fifth byte determines supported or not.
	 *
	 * Currently only supported values are: 0x03 or 0x04.
	 * Create unique GUID for each supported vendor to match 'provides' value in metainfo.
	 */
	fu_struct_logitech_tap_touch_hid_req_set_payload_len(st, 0x01);
	fu_struct_logitech_tap_touch_hid_req_set_response_len(st, 0x08);
	fu_struct_logitech_tap_touch_hid_req_set_cmd(
	    st,
	    FU_STRUCT_LOGITECH_TAP_TOUCH_HID_CMD_GET_FIRMWARE_VERSION);
	if (!fu_logitech_tap_touch_device_hid_transfer(self, st, 100, buf_res, error))
		return FALSE;
	if (!fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		guint8 version_2511 = 0;
		if (!fu_memread_uint8_safe(buf_res->data, buf_res->len, 8, &version_2511, error))
			return FALSE;
		if (version_2511 == 0x03) {
			fu_device_add_instance_str(FU_DEVICE(self), "2511", "TM");
		} else if (version_2511 == 0x04) {
			fu_device_add_instance_str(FU_DEVICE(self), "2511", "SW");
		} else {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "failed to get supported vendor: %x",
				    version_2511);
			return FALSE;
		}
		if (!fu_device_build_instance_id(FU_DEVICE(self),
						 error,
						 "HIDRAW",
						 "VEN",
						 "DEV",
						 "2511",
						 NULL))
			return FALSE;
	}
	if (!fu_memread_uint64_safe(buf_res->data,
				    buf_res->len,
				    FU_LOGITECH_TAP_TOUCH_HID_RESPONSE_OFFSET,
				    &version_raw,
				    G_BIG_ENDIAN,
				    error))
		return FALSE;
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		fu_device_set_version_bootloader_raw(FU_DEVICE(self), version_raw);
	} else {
		fu_device_set_version_raw(FU_DEVICE(self), version_raw);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_tap_touch_device_check_protocol(FuLogitechTapTouchDevice *self, GError **error)
{
	guint8 protocol_version[3] = {0};
	g_autoptr(FuStructLogitechTapTouchHidReq) st = fu_struct_logitech_tap_touch_hid_req_new();
	g_autoptr(GByteArray) buf_res = g_byte_array_new();

	/*
	 * hid report to query device protocol info
	 * in application mode only V3 (3.1.0) supported
	 * in bootloader mode only 1.7.ff supported
	 */
	fu_struct_logitech_tap_touch_hid_req_set_payload_len(st, 0x01);
	fu_struct_logitech_tap_touch_hid_req_set_response_len(st, 0x03);
	fu_struct_logitech_tap_touch_hid_req_set_cmd(
	    st,
	    FU_STRUCT_LOGITECH_TAP_TOUCH_HID_CMD_GET_PROTOCOL_VERSION);
	if (!fu_logitech_tap_touch_device_hid_transfer(self, st, 100, buf_res, error))
		return FALSE;
	if (!fu_memcpy_safe(protocol_version,
			    sizeof(protocol_version),
			    0x0, /* dst */
			    buf_res->data,
			    buf_res->len,
			    FU_LOGITECH_TAP_TOUCH_HID_RESPONSE_OFFSET, /* src */
			    sizeof(protocol_version),
			    error))
		return FALSE;
	if ((protocol_version[0] != FU_LOGITECH_TAP_TOUCH_SUPPORTED_PROTOCOL_VERSION) &&
	    (!fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER))) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to get supported protocol ver: %x",
			    protocol_version[0]);
		return FALSE;
	}
	g_debug("touch panel protocol version: %x.%x.%x",
		protocol_version[0],
		protocol_version[1],
		protocol_version[2]);

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_tap_touch_device_setup(FuDevice *device, GError **error)
{
	FuLogitechTapTouchDevice *self = FU_LOGITECH_TAP_TOUCH_DEVICE(device);
	struct hidraw_devinfo hid_raw_info = {0x0};
	g_autoptr(FuDeviceLocker) locker = NULL;

	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		g_debug("entering in BL MODE");
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  HIDIOCGRAWINFO,
				  (guint8 *)&hid_raw_info,
				  sizeof(hid_raw_info),
				  NULL,
				  FU_LOGITECH_TAP_TOUCH_IOCTL_TIMEOUT,
				  FU_UDEV_DEVICE_IOCTL_FLAG_NONE,
				  error))
		return FALSE;
	if (hid_raw_info.bustype != FU_LOGITECH_TAP_TOUCH_DEVICE_INFO_BUS_TYPE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "incorrect bustype=0x%x, expected usb",
			    hid_raw_info.bustype);
		return FALSE;
	}

	/* enable/disable TDE mode */
	locker =
	    fu_device_locker_new_full(FU_DEVICE(self),
				      (FuDeviceLockerFunc)fu_logitech_tap_touch_device_enable_tde,
				      (FuDeviceLockerFunc)fu_logitech_tap_touch_device_disable_tde,
				      error);
	if (locker == NULL)
		return FALSE;

	/* wait 1 sec for suspend mode */
	fu_device_sleep(FU_DEVICE(self), 1000);

	/* hid report to query MCU info, only FU_LOGITECH_TAP_TOUCH_IC_NAME supported */
	if (!fu_logitech_tap_touch_device_check_protocol(self, error))
		return FALSE;
	if (!fu_logitech_tap_touch_device_check_ic_name(self, error))
		return FALSE;

	/* get version */
	return fu_logitech_tap_touch_device_ensure_version(self, error);
}

static gboolean
fu_logitech_tap_touch_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuLogitechTapTouchDevice *self = FU_LOGITECH_TAP_TOUCH_DEVICE(device);
	guint8 mcu_mode = 0;

	/* sanity check */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in bootloader mode, skipping");
		return TRUE;
	}

	/* cannot use locker, device goes into bootloader mode here, looses connectivity */
	if (!fu_logitech_tap_touch_device_enable_tde(device, error))
		return FALSE;

	if (!fu_logitech_tap_touch_device_get_mcu_mode(self, &mcu_mode, error))
		return FALSE;

	/* hid report to enable write and switch to bootloader (BL) mode */
	if (mcu_mode == FU_LOGITECH_TAP_TOUCH_AP_MODE) {
		g_autoptr(FuStructLogitechTapTouchHidReq) st =
		    fu_struct_logitech_tap_touch_hid_req_new();
		if (!fu_logitech_tap_touch_device_write_enable(self, TRUE, FALSE, 0, 0, error))
			return FALSE;
		fu_struct_logitech_tap_touch_hid_req_set_payload_len(st, 0x01);
		fu_struct_logitech_tap_touch_hid_req_set_response_len(st, 0x0);
		fu_struct_logitech_tap_touch_hid_req_set_cmd(
		    st,
		    FU_STRUCT_LOGITECH_TAP_TOUCH_HID_CMD_SET_BL_MODE);
		if (!fu_logitech_tap_touch_device_hid_transfer(self, st, 0, NULL, error))
			return FALSE;
	}

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_tap_touch_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuLogitechTapTouchDevice *self = FU_LOGITECH_TAP_TOUCH_DEVICE(device);
	g_autoptr(FuStructLogitechTapTouchHidReq) st = fu_struct_logitech_tap_touch_hid_req_new();

	/* sanity check */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in runtime mode, skipping");
		return TRUE;
	}

	/* hid report to enable write and switch to application (AP) mode */
	if (!fu_logitech_tap_touch_device_write_enable(self, FALSE, FALSE, 0, 0, error))
		return FALSE;
	fu_struct_logitech_tap_touch_hid_req_set_payload_len(st, 0x01);
	fu_struct_logitech_tap_touch_hid_req_set_response_len(st, 0x0);
	fu_struct_logitech_tap_touch_hid_req_set_cmd(
	    st,
	    FU_STRUCT_LOGITECH_TAP_TOUCH_HID_CMD_SET_AP_MODE);
	if (!fu_logitech_tap_touch_device_hid_transfer(self, st, 0, NULL, error))
		return FALSE;

	/* mode switch delay for application/bootloader */
	fu_device_sleep(FU_DEVICE(self), 100);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_tap_touch_device_probe(FuDevice *device, GError **error)
{
	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS(fu_logitech_tap_touch_device_parent_class)->probe(device, error))
		return FALSE;

	/* ignore unsupported subsystems */
	if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), "hidraw") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "is not correct subsystem=%s, expected hidraw",
			    fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)));
		return FALSE;
	}

	/* set the physical ID */
	return fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "hid", error);
}

static gboolean
fu_logitech_tap_touch_device_write_blocks(FuLogitechTapTouchDevice *self,
					  FuFirmware *img,
					  guint16 firmware_checksum,
					  gboolean in_ap,
					  FuProgress *progress,
					  GError **error)
{
	guint16 device_checksum = 0;
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GInputStream) stream = NULL;

	stream = fu_firmware_get_stream(img, error);
	if (stream == NULL)
		return FALSE;
	chunks = fu_chunk_array_new_from_stream(stream,
						0x0,
						FU_LOGITECH_TAP_TOUCH_TRANSFER_BLOCK_SIZE,
						error);
	if (chunks == NULL)
		return FALSE;

	/* progress */
	g_debug("updating %s block. end:0x%x, checksum:0x%x",
		in_ap ? "AP" : "DF",
		(guint)fu_firmware_get_offset(img),
		firmware_checksum);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));

	/* hid report to enable write */
	if (!fu_logitech_tap_touch_device_write_enable(self,
						       FALSE,
						       in_ap ? TRUE : FALSE,
						       fu_firmware_get_offset(img),
						       firmware_checksum,
						       error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), 10);

	for (guint32 i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuStructLogitechTapTouchHidReq) st =
		    fu_struct_logitech_tap_touch_hid_req_new();
		g_autoptr(GByteArray) chunk_buf = g_byte_array_new();
		g_autoptr(FuChunk) chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		/* ensure each packet is FU_LOGITECH_TAP_TOUCH_TRANSFER_BLOCK_SIZE bytes */
		g_byte_array_append(chunk_buf, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));

		/* write packet */
		fu_struct_logitech_tap_touch_hid_req_set_payload_len(st, 1 + chunk_buf->len);
		fu_struct_logitech_tap_touch_hid_req_set_response_len(st, 0x00);
		fu_struct_logitech_tap_touch_hid_req_set_cmd(
		    st,
		    FU_STRUCT_LOGITECH_TAP_TOUCH_HID_CMD_WRITE_DATA);
		g_byte_array_append(st, chunk_buf->data, chunk_buf->len);
		/* resize the last packet */
		if ((i == (fu_chunk_array_length(chunks) - 1)) &&
		    (fu_chunk_get_data_sz(chk) < FU_LOGITECH_TAP_TOUCH_TRANSFER_BLOCK_SIZE))
			fu_byte_array_set_size(st, 37, in_ap ? 0xFF : 0x0);
		if (!fu_logitech_tap_touch_device_hid_transfer(self, st, 0, NULL, error))
			return FALSE;
		fu_device_sleep(FU_DEVICE(self), 2);

		if (!fu_device_retry_full(FU_DEVICE(self),
					  fu_logitech_tap_touch_device_check_busy_cb,
					  FU_LOGITECH_TAP_TOUCH_MAX_BUSY_CHECK_RETRY_COUNT,
					  5,
					  NULL,
					  error)) {
			g_prefix_error(error,
				       "failed to get idle state for %s: ",
				       in_ap ? "AP" : "DF");
			return FALSE;
		}
		fu_progress_step_done(progress);
	}
	/* done with writing dataflash/pflash (DF/AP) block */
	fu_device_sleep(FU_DEVICE(self), 50);

	/* verify crc */
	if (!fu_logitech_tap_touch_device_get_crc(self, NULL, 0, error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_logitech_tap_touch_device_check_busy_cb,
				  FU_LOGITECH_TAP_TOUCH_MAX_BUSY_CHECK_RETRY_COUNT,
				  5,
				  NULL,
				  error)) {
		g_prefix_error(error, "failed to crc for %s, device busy", in_ap ? "AP" : "DF");
		return FALSE;
	}
	if (!fu_logitech_tap_touch_device_get_crc(self, &device_checksum, 4, error))
		return FALSE;
	if (device_checksum != firmware_checksum) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "crc check failed for %s, expected 0x%04x and got 0x%04x",
			    in_ap ? "AP" : "DF",
			    firmware_checksum,
			    device_checksum);
		return FALSE;
	}
	g_info("device checksum for %s. checksum:0x%x", in_ap ? "AP" : "DF", device_checksum);

	/* success */
	return TRUE;
}

typedef struct {
	FuFirmware *firmware; /* noref */
	FuProgress *progress; /* noref */
} FuLogitechTapTouchWriteHelper;

static gboolean
fu_logitech_tap_touch_device_write_chunks_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuLogitechTapTouchDevice *self = FU_LOGITECH_TAP_TOUCH_DEVICE(device);
	FuLogitechTapTouchWriteHelper *helper = (FuLogitechTapTouchWriteHelper *)user_data;
	guint16 ap_checksum;
	guint16 df_checksum;
	g_autoptr(FuStructLogitechTapTouchHidReq) st = fu_struct_logitech_tap_touch_hid_req_new();
	g_autoptr(FuFirmware) ap_img = NULL;
	g_autoptr(FuFirmware) df_img = NULL;

	/* progress */
	fu_progress_set_id(helper->progress, G_STRLOC);
	fu_progress_add_step(helper->progress, FWUPD_STATUS_DEVICE_ERASE, 3, "erase");
	fu_progress_add_step(helper->progress, FWUPD_STATUS_DEVICE_WRITE, 3, "write-df-blocks");
	fu_progress_add_step(helper->progress, FWUPD_STATUS_DEVICE_WRITE, 94, "write-ap-blocks");

	ap_checksum = fu_logitech_tap_touch_firmware_get_ap_checksum(
	    FU_LOGITECH_TAP_TOUCH_FIRMWARE(helper->firmware));
	df_checksum = fu_logitech_tap_touch_firmware_get_df_checksum(
	    FU_LOGITECH_TAP_TOUCH_FIRMWARE(helper->firmware));

	/* get images */
	ap_img = fu_firmware_get_image_by_id(helper->firmware, "ap", error);
	if (ap_img == NULL)
		return FALSE;
	df_img = fu_firmware_get_image_by_id(helper->firmware, "df", error);
	if (df_img == NULL)
		return FALSE;

	/* hid report to enable write */
	if (!fu_logitech_tap_touch_device_write_enable(self, FALSE, FALSE, 0xF01F, 0, error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), 5);

	/* write_data */
	fu_struct_logitech_tap_touch_hid_req_set_payload_len(st, 0x21);
	fu_struct_logitech_tap_touch_hid_req_set_response_len(st, 0x0);
	fu_struct_logitech_tap_touch_hid_req_set_cmd(
	    st,
	    FU_STRUCT_LOGITECH_TAP_TOUCH_HID_CMD_WRITE_DATA);
	fu_byte_array_set_size(st,
			       37,
			       0xFF); /* 4 (req header) + 1 (cmd) +
					 FU_LOGITECH_TAP_TOUCH_TRANSFER_BLOCK_SIZE (data buffer) */
	if (!fu_logitech_tap_touch_device_hid_transfer(self, st, 0, NULL, error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), 500);

	/* erase flash */
	fu_progress_step_done(helper->progress);

	/* write firmware to data flash (DF) block */
	if (!fu_logitech_tap_touch_device_write_blocks(self,
						       df_img,
						       df_checksum,
						       FALSE,
						       fu_progress_get_child(helper->progress),
						       error))
		return FALSE;
	fu_progress_step_done(helper->progress);

	/* write firmware to pflash (AP) block */
	if (!fu_logitech_tap_touch_device_write_blocks(self,
						       ap_img,
						       ap_checksum,
						       TRUE,
						       fu_progress_get_child(helper->progress),
						       error))
		return FALSE;
	fu_progress_step_done(helper->progress);

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_tap_touch_device_write_firmware(FuDevice *device,
					    FuFirmware *firmware,
					    FuProgress *progress,
					    FwupdInstallFlags flags,
					    GError **error)
{
	FuLogitechTapTouchDevice *self = FU_LOGITECH_TAP_TOUCH_DEVICE(device);
	FuLogitechTapTouchWriteHelper helper = {.firmware = firmware, .progress = progress};
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* enable/disable TDE mode */
	locker =
	    fu_device_locker_new_full(FU_DEVICE(self),
				      (FuDeviceLockerFunc)fu_logitech_tap_touch_device_enable_tde,
				      (FuDeviceLockerFunc)fu_logitech_tap_touch_device_disable_tde,
				      error);
	if (locker == NULL)
		return FALSE;

	/* vendor recommendation is to retry few time */
	return fu_device_retry_full(device,
				    fu_logitech_tap_touch_device_write_chunks_cb,
				    FU_LOGITECH_TAP_TOUCH_MAX_FW_WRITE_RETRIES,
				    100,
				    &helper,
				    error);
}

static void
fu_logitech_tap_touch_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 10, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 80, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 10, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static gchar *
fu_logitech_tap_touch_device_convert_version(FuDevice *device, guint64 version_raw)
{
	/* convert 8 byte version in to human readable format. e.g. convert 0x0600000003000004 into
	 * 6000.3004*/
	return g_strdup_printf("%01x%01x%01x%01x.%01x%01x%01x%01x",
			       (guint)((version_raw >> 56) & 0xFF),
			       (guint)((version_raw >> 48) & 0xFF),
			       (guint)((version_raw >> 40) & 0xFF),
			       (guint)((version_raw >> 32) & 0xFF),
			       (guint)((version_raw >> 24) & 0xFF),
			       (guint)((version_raw >> 16) & 0xFF),
			       (guint)((version_raw >> 8) & 0xFF),
			       (guint)((version_raw >> 0) & 0xFF));
}

static void
fu_logitech_tap_touch_device_init(FuLogitechTapTouchDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.logitech.hardware.tap");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ADD_COUNTERPART_GUIDS);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_set_firmware_size_min(FU_DEVICE(self), FU_LOGITECH_TAP_TOUCH_MIN_FW_FILE_SIZE);
	fu_device_set_firmware_size_max(FU_DEVICE(self), FU_LOGITECH_TAP_TOUCH_MAX_FW_FILE_SIZE);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_LOGITECH_TAP_TOUCH_FIRMWARE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_NONBLOCK);
}

static void
fu_logitech_tap_touch_device_class_init(FuLogitechTapTouchDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_logitech_tap_touch_device_probe;
	device_class->setup = fu_logitech_tap_touch_device_setup;
	device_class->set_progress = fu_logitech_tap_touch_device_set_progress;
	device_class->convert_version = fu_logitech_tap_touch_device_convert_version;
	device_class->detach = fu_logitech_tap_touch_device_detach;
	device_class->write_firmware = fu_logitech_tap_touch_device_write_firmware;
	device_class->attach = fu_logitech_tap_touch_device_attach;
}
