/*
 * Copyright 1999-2023 Logitech, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <linux/types.h>
#include <linux/usb/video.h>
#include <linux/uvcvideo.h>
#ifdef HAVE_IOCTL_H
#include <sys/ioctl.h>
#endif

#include <string.h>

#include "fu-logitech-tap-hdmi-device.h"

#define FU_LOGITECH_TAP_HDMI_DEVICE_IOCTL_TIMEOUT 5000 /* ms */
#define XU_INPUT_DATA_LEN			  8

/* 2 byte for get len query */
#define kDefaultUvcGetLenQueryControlSize 2

const guint8 kLogiTapCameraVersionSelector = 1;
const guint8 kLogiTapUvcXuAitCustomCsGetMmpResult = 5;

const guint8 kLogiTapHdmiVerSetData = 0x0B;

const guint8 kLogiUnitIdVidCapExtension = 0x06;
const guint8 kLogiHdmiVerGetSelector = 2;

const guint8 kLogiTapAitSetMmpCmdFwBurning = 0x01;
const guint8 kLogiTapVideoAitInitiateSetMMPData = 1;
const guint kLogiDefaultImageBlockSize = 32;
const guint8 kLogiUvcXuAitCustomCsSetFwData = 0x03;

const guint8 kLogiTapUvcXuAitCustomCsSetMmp = 4;
const guint kLogiDefaultAitSleepIntervalMs = 1000;

/* when finalize Ait, max polling duration is 120sec */
const guint kLogiDefaultAitFinalizeMaxPollingDurationMs = 120000;
const guint8 kLogiDefaultAitSuccessValue = 0x00;
const guint8 kLogiDefaultAitFailureValue = 0x82;

struct _FuLogitechTapHdmiDevice {
	FuV4lDevice parent_instance;
};

G_DEFINE_TYPE(FuLogitechTapHdmiDevice, fu_logitech_tap_hdmi_device, FU_TYPE_V4L_DEVICE)

static gboolean
fu_logitech_tap_hdmi_device_query_data_size(FuLogitechTapHdmiDevice *self,
					    guint8 unit_id,
					    guint8 control_selector,
					    guint16 *data_size,
					    GError **error)
{
	guint8 size_data[kDefaultUvcGetLenQueryControlSize] = {0x0};
	struct uvc_xu_control_query size_query = {.unit = unit_id,
						  .selector = control_selector,
						  .query = UVC_GET_LEN,
						  .size = kDefaultUvcGetLenQueryControlSize,
						  .data = size_data};

	g_debug("data size query request, unit: 0x%x selector: 0x%x", unit_id, control_selector);

	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  UVCIOC_CTRL_QUERY,
				  (guint8 *)&size_query,
				  sizeof(size_query),
				  NULL,
				  FU_LOGITECH_TAP_HDMI_DEVICE_IOCTL_TIMEOUT,
				  FU_UDEV_DEVICE_IOCTL_FLAG_RETRY,
				  error))
		return FALSE;

	/* convert the data byte to int */
	if (!fu_memread_uint16_safe(size_data,
				    sizeof(size_data),
				    0x0,
				    data_size,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	g_debug("data size query response, size: %u unit: 0x%x selector: 0x%x",
		*data_size,
		unit_id,
		control_selector);
	fu_dump_raw(G_LOG_DOMAIN,
		    "UVC_GET_LENRes",
		    size_query.data,
		    kDefaultUvcGetLenQueryControlSize);

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_tap_hdmi_device_get_xu_control(FuLogitechTapHdmiDevice *self,
					   guint8 unit_id,
					   guint8 control_selector,
					   guint16 data_size,
					   guint8 *data,
					   GError **error)
{
	struct uvc_xu_control_query control_query = {.unit = unit_id,
						     .selector = control_selector,
						     .query = UVC_GET_CUR,
						     .size = data_size,
						     .data = data};
	g_debug("get xu control request, size: %" G_GUINT16_FORMAT " unit: 0x%x selector: 0x%x",
		data_size,
		unit_id,
		control_selector);

	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  UVCIOC_CTRL_QUERY,
				  (guint8 *)&control_query,
				  sizeof(control_query),
				  NULL,
				  FU_LOGITECH_TAP_HDMI_DEVICE_IOCTL_TIMEOUT,
				  FU_UDEV_DEVICE_IOCTL_FLAG_RETRY,
				  error))
		return FALSE;

	g_debug("received get xu control response, size: %u unit: 0x%x selector: 0x%x",
		control_query.size,
		unit_id,
		control_selector);
	fu_dump_raw(G_LOG_DOMAIN, "UVC_GET_CURRes", control_query.data, control_query.size);

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_tap_hdmi_device_set_xu_control(FuLogitechTapHdmiDevice *self,
					   guint8 unit_id,
					   guint8 control_selector,
					   guint16 data_size,
					   guint8 *data,
					   GError **error)
{
	struct uvc_xu_control_query control_query = {.unit = unit_id,
						     .selector = control_selector,
						     .query = UVC_SET_CUR,
						     .size = data_size,
						     .data = data};

	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  UVCIOC_CTRL_QUERY,
				  (guint8 *)&control_query,
				  sizeof(control_query),
				  NULL,
				  FU_LOGITECH_TAP_HDMI_DEVICE_IOCTL_TIMEOUT,
				  FU_UDEV_DEVICE_IOCTL_FLAG_RETRY,
				  error))
		return FALSE;

	g_debug("received set xu control response, size: %u unit: 0x%x selector: 0x%x",
		data_size,
		unit_id,
		control_selector);

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_tap_hdmi_device_ait_initiate_update(FuLogitechTapHdmiDevice *self, GError **error)
{
	guint16 data_len = 0;
	g_autofree guint8 *mmp_get_data = NULL;
	guint8 ait_initiate_update[XU_INPUT_DATA_LEN] =
	    {kLogiTapAitSetMmpCmdFwBurning, 0, 0, kLogiTapVideoAitInitiateSetMMPData, 0, 0, 0, 0};

	if (!fu_logitech_tap_hdmi_device_set_xu_control(self,
							kLogiUnitIdVidCapExtension,
							kLogiTapUvcXuAitCustomCsSetMmp,
							XU_INPUT_DATA_LEN,
							(guint8 *)&ait_initiate_update,
							error))
		return FALSE;

	if (!fu_logitech_tap_hdmi_device_query_data_size(self,
							 kLogiUnitIdVidCapExtension,
							 kLogiTapUvcXuAitCustomCsGetMmpResult,
							 &data_len,
							 error))
		return FALSE;
	if (data_len > XU_INPUT_DATA_LEN) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "initiate query packet was too large at 0x%x bytes: ",
			    data_len);
		return FALSE;
	}

	mmp_get_data = g_malloc0(data_len);
	if (!fu_logitech_tap_hdmi_device_get_xu_control(self,
							kLogiUnitIdVidCapExtension,
							kLogiTapUvcXuAitCustomCsGetMmpResult,
							data_len,
							(guint8 *)mmp_get_data,
							error))
		return FALSE;
	if (mmp_get_data[0] != kLogiDefaultAitSuccessValue) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to initialize AIT update, invalid result data: 0x%x",
			    mmp_get_data[0]);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_tap_hdmi_device_ait_finalize_update(FuLogitechTapHdmiDevice *self, GError **error)
{
	guint duration_ms = 0;
	guint8 ait_finalize_update[XU_INPUT_DATA_LEN] =
	    {kLogiTapAitSetMmpCmdFwBurning, kLogiTapVideoAitInitiateSetMMPData, 0, 0, 0, 0, 0, 0};

	fu_device_sleep(FU_DEVICE(self), 4 * kLogiDefaultAitSleepIntervalMs); /* 4 sec */
	if (!fu_logitech_tap_hdmi_device_set_xu_control(self,
							kLogiUnitIdVidCapExtension,
							kLogiTapUvcXuAitCustomCsSetMmp,
							XU_INPUT_DATA_LEN,
							(guint8 *)&ait_finalize_update,
							error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), kLogiDefaultAitSleepIntervalMs); /* 1 sec */
	/* poll for burning fw result or return failure if it hits max polling */
	for (int pass = 0;; pass++) {
		g_autofree guint8 *mmp_get_data = NULL;
		guint16 data_len = 0;

		fu_device_sleep(FU_DEVICE(self), kLogiDefaultAitSleepIntervalMs); /* 1 sec */
		duration_ms = duration_ms + kLogiDefaultAitSleepIntervalMs;
		if (!fu_logitech_tap_hdmi_device_query_data_size(
			self,
			kLogiUnitIdVidCapExtension,
			kLogiTapUvcXuAitCustomCsGetMmpResult,
			&data_len,
			error))
			return FALSE;
		mmp_get_data = g_malloc0(data_len);
		if (!fu_logitech_tap_hdmi_device_get_xu_control(
			self,
			kLogiUnitIdVidCapExtension,
			kLogiTapUvcXuAitCustomCsGetMmpResult,
			data_len,
			(guint8 *)mmp_get_data,
			error))
			return FALSE;
		if (mmp_get_data[0] == kLogiDefaultAitSuccessValue) {
			if (pass == 0)
				fu_device_sleep(FU_DEVICE(self), 8 * 1000);
			break;
		} else if (mmp_get_data[0] == kLogiDefaultAitFailureValue) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to finalize image burning, invalid result data: 0x%x",
				    mmp_get_data[0]);
			return FALSE;
		}
		if (duration_ms > kLogiDefaultAitFinalizeMaxPollingDurationMs) {
			/* if device never returns 0x82 or 0x00, bail out */
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to finalize image burning, duration_ms: %u",
				    duration_ms);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_tap_hdmi_device_write_fw(FuLogitechTapHdmiDevice *self,
				     FuChunkArray *chunks,
				     FuProgress *progress,
				     GError **error)
{
	/* init */
	if (!fu_logitech_tap_hdmi_device_ait_initiate_update(self, error))
		return FALSE;

	/* write */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		/* if needed, pad the last block to kLogiDefaultImageBlockSize size,
		 * so that device always gets each block of kLogiDefaultImageBlockSize */
		g_autofree guint8 *data_pkt = g_malloc0(kLogiDefaultImageBlockSize);

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_memcpy_safe(data_pkt,
				    kLogiDefaultImageBlockSize,
				    0x0,
				    fu_chunk_get_data(chk),
				    fu_chunk_get_data_sz(chk),
				    0x0,
				    fu_chunk_get_data_sz(chk),
				    error))
			return FALSE;
		if (!fu_logitech_tap_hdmi_device_set_xu_control(self,
								kLogiUnitIdVidCapExtension,
								kLogiUvcXuAitCustomCsSetFwData,
								kLogiDefaultImageBlockSize,
								(guint8 *)data_pkt,
								error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* uninit */
	if (!fu_logitech_tap_hdmi_device_ait_finalize_update(self, error))
		return FALSE;

	/* signal for sensor device to trigger composite device reboot */
	fu_device_add_private_flag(FU_DEVICE(self),
				   FU_LOGITECH_TAP_HDMI_DEVICE_FLAG_SENSOR_NEEDS_REBOOT);
	return TRUE;
}

static gboolean
fu_logitech_tap_hdmi_device_write_firmware(FuDevice *device,
					   FuFirmware *firmware,
					   FuProgress *progress,
					   FwupdInstallFlags flags,
					   GError **error)
{
	FuLogitechTapHdmiDevice *self = FU_LOGITECH_TAP_HDMI_DEVICE(device);
	g_autofree gchar *old_firmware_version = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* for troubleshooting purpose */
	old_firmware_version = g_strdup(fu_device_get_version(device));
	g_debug("update %s firmware", old_firmware_version);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");

	/* get image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;

	/* write */
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	chunks = fu_chunk_array_new_from_stream(stream, 0x0, kLogiDefaultImageBlockSize, error);
	if (chunks == NULL)
		return FALSE;
	if (!fu_logitech_tap_hdmi_device_write_fw(self,
						  chunks,
						  fu_progress_get_child(progress),
						  error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_tap_hdmi_device_ensure_version(FuLogitechTapHdmiDevice *self, GError **error)
{
	guint16 bufsz = 0;
	guint8 set_data[XU_INPUT_DATA_LEN] = {kLogiTapHdmiVerSetData, 0, 0, 0, 0, 0, 0, 0};
	guint16 build = 0;
	guint16 minor = 0;
	guint16 major = 0;
	g_autofree guint8 *buf = NULL;

	if (!fu_logitech_tap_hdmi_device_set_xu_control(self,
							kLogiUnitIdVidCapExtension,
							kLogiTapCameraVersionSelector,
							XU_INPUT_DATA_LEN,
							(guint8 *)set_data,
							error))
		return FALSE;

	/* query current device version */
	if (!fu_logitech_tap_hdmi_device_query_data_size(self,
							 kLogiUnitIdVidCapExtension,
							 kLogiHdmiVerGetSelector,
							 &bufsz,
							 error))
		return FALSE;
	if (bufsz > XU_INPUT_DATA_LEN) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "version query packet was too large at 0x%x bytes: ",
			    bufsz);
		return FALSE;
	}

	buf = g_malloc0(bufsz);
	if (!fu_logitech_tap_hdmi_device_get_xu_control(self,
							kLogiUnitIdVidCapExtension,
							kLogiHdmiVerGetSelector,
							bufsz,
							(guint8 *)buf,
							error))
		return FALSE;

	/* MajorVersion bytes 3&2, MinorVersion bytes 5&4, BuildVersion bytes 7&6 */
	if (!fu_memread_uint16_safe(buf, bufsz, 0x2, &major, G_BIG_ENDIAN, error))
		return FALSE;
	if (!fu_memread_uint16_safe(buf, bufsz, 0x4, &minor, G_BIG_ENDIAN, error))
		return FALSE;
	if (!fu_memread_uint16_safe(buf, bufsz, 0x6, &build, G_BIG_ENDIAN, error))
		return FALSE;
	fu_device_set_version(FU_DEVICE(self), g_strdup_printf("%i.%i.%i", major, minor, build));

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_tap_hdmi_device_setup(FuDevice *device, GError **error)
{
	FuLogitechTapHdmiDevice *self = FU_LOGITECH_TAP_HDMI_DEVICE(device);

	/* FuV4lDevice->setup */
	if (!FU_DEVICE_CLASS(fu_logitech_tap_hdmi_device_parent_class)->setup(device, error))
		return FALSE;

	/* only interested in video capture device */
	if ((fu_v4l_device_get_caps(FU_V4L_DEVICE(self)) & FU_V4L_CAP_VIDEO_CAPTURE) == 0) {
		g_autofree gchar *caps =
		    fu_v4l_cap_to_string(fu_v4l_device_get_caps(FU_V4L_DEVICE(self)));
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "only video capture device are supported, got %s",
			    caps);
		return FALSE;
	}
	return fu_logitech_tap_hdmi_device_ensure_version(self, error);
}

static gboolean
fu_logitech_tap_hdmi_device_probe(FuDevice *device, GError **error)
{
	/* interested in lowest index only e,g, video0, ignore low format siblings like
	 * video1/video2/video3 etc */
	if (fu_v4l_device_get_index(FU_V4L_DEVICE(device)) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "only device with lower index supported");
		return FALSE;
	};

	/* success */
	return TRUE;
}

static void
fu_logitech_tap_hdmi_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_logitech_tap_hdmi_device_init(FuLogitechTapHdmiDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.logitech.hardware.tap");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_retry_set_delay(FU_DEVICE(self), 1000);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_NONBLOCK);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_LOGITECH_TAP_HDMI_DEVICE_FLAG_SENSOR_NEEDS_REBOOT);
}

static void
fu_logitech_tap_hdmi_device_class_init(FuLogitechTapHdmiDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_logitech_tap_hdmi_device_probe;
	device_class->setup = fu_logitech_tap_hdmi_device_setup;
	device_class->set_progress = fu_logitech_tap_hdmi_device_set_progress;
	device_class->write_firmware = fu_logitech_tap_hdmi_device_write_firmware;
}
