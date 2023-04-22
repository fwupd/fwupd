/*
 * Copyright (c) 1999-2023 Logitech, Inc.
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <linux/types.h>
#include <linux/usb/video.h>
#include <linux/uvcvideo.h>
#ifdef HAVE_IOCTL_H
#include <sys/ioctl.h>
#endif

#include <string.h>

#include "fu-logitech-tap-common.h"
#include "fu-logitech-tap-hdmi-device.h"

#define FU_LOGITECH_TAP_HDMI_DEVICE_IOCTL_TIMEOUT 5000 /* ms */
#define XU_INPUT_DATA_LEN 8

/* 2 byte for get len query */
#define kDefaultUvcGetLenQueryControlSize 2

const guchar kLogiTapCameraVersionSelector = 1;
const guchar kLogiTapUvcXuAitCustomCsGetMmpResult = 5;

const guchar kLogiTapHdmiVerSetData = 0x0B;


const guchar kLogiUnitIdVidCapExtension = 0x06;
const guchar kLogiHdmiVerGetSelector = 2;

const guchar kLogiTapAitSetMmpCmdFwBurning = 0x01;
const guchar kLogiTapVideoAitInitiateSetMMPData = 1;
const guint kLogiDefaultImageBlockSize = 32;
const guchar kLogiUvcXuAitCustomCsSetFwData = 0x03;

const guchar kLogiTapUvcXuAitCustomCsSetMmp = 4;
const guint kLogiDefaultAitSleepIntervalMs = 1000;

/* when finalize Ait, max polling duration is 120sec */
const guint kLogiDefaultAitFinalizeMaxPollingDurationMs = 120000;
const guchar kLogiDefaultAitSuccessValue = 0x00;
const guchar kLogiDefaultAitFailureValue = 0x82;

struct _FuLogitechTapHdmiDevice {
	FuLogitechTapDevice parent_instance;
};

G_DEFINE_TYPE(FuLogitechTapHdmiDevice, fu_logitech_tap_hdmi_device, FU_TYPE_LOGITECH_TAP_DEVICE)

static gboolean
fu_logitech_tap_hdmi_device_query_data_size(FuLogitechTapHdmiDevice *self,
					  guchar unit_id,
					  guchar control_selector,
					  guint16 *data_size,
					  GError **error)
{
	guint8 size_data[kDefaultUvcGetLenQueryControlSize] = {0x0};
	struct uvc_xu_control_query size_query;
	size_query.unit = unit_id;
	size_query.selector = control_selector;
	size_query.query = UVC_GET_LEN;
	size_query.size = kDefaultUvcGetLenQueryControlSize;
	size_query.data = size_data;

	g_debug("data size query request, unit: 0x%x selector: 0x%x",
			(guchar)unit_id,
			(guchar)control_selector);

	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  UVCIOC_CTRL_QUERY,
				  (guint8 *)&size_query,
				  NULL,
				  FU_LOGITECH_TAP_HDMI_DEVICE_IOCTL_TIMEOUT,
				  error))
		return FALSE;

	/* convert the data byte to int */
	*data_size = size_data[1] << 8 | size_data[0];
		g_debug("data size query response, size: %u unit: 0x%x selector: 0x%x",
			*data_size,
			(guchar)unit_id,
			(guchar)control_selector);
		fu_dump_raw(G_LOG_DOMAIN,
			    "UVC_GET_LENRes",
			    size_query.data,
			    kDefaultUvcGetLenQueryControlSize);

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_tap_hdmi_device_get_xu_control(FuLogitechTapHdmiDevice *self,
					 guchar unit_id,
					 guchar control_selector,
					 guint16 data_size,
					 guchar *data,
					 GError **error)
{
	struct uvc_xu_control_query control_query;

	g_debug("get xu control request, size: %" G_GUINT16_FORMAT
			" unit: 0x%x selector: 0x%x",
			data_size,
			(guchar)unit_id,
			(guchar)control_selector);

	control_query.unit = unit_id;
	control_query.selector = control_selector;
	control_query.query = UVC_GET_CUR;
	control_query.size = data_size;
	control_query.data = data;
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  UVCIOC_CTRL_QUERY,
				  (guint8 *)&control_query,
				  NULL,
				  FU_LOGITECH_TAP_HDMI_DEVICE_IOCTL_TIMEOUT,
				  error))
		return FALSE;

	g_debug("received get xu control response, size: %u unit: 0x%x selector: 0x%x",
			control_query.size,
			(guchar)unit_id,
			(guchar)control_selector);
	fu_dump_raw(G_LOG_DOMAIN, "UVC_GET_CURRes", control_query.data, control_query.size);

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_tap_hdmi_device_set_xu_control(FuLogitechTapHdmiDevice *self,
					 guchar unit_id,
					 guchar control_selector,
					 guint16 data_size,
					 guchar *data,
					 GError **error)
{
	struct uvc_xu_control_query control_query;

	control_query.unit = unit_id;
	control_query.selector = control_selector;
	control_query.query = UVC_SET_CUR;
	control_query.size = data_size;
	control_query.data = data;

	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  UVCIOC_CTRL_QUERY,
				  (guint8 *)&control_query,
				  NULL,
				  FU_LOGITECH_TAP_HDMI_DEVICE_IOCTL_TIMEOUT,
				  error))
		return FALSE;

	g_debug("received set xu control response, size: %u unit: 0x%x selector: 0x%x",
			data_size,
			(guchar)unit_id,
			(guchar)control_selector);

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_tap_hdmi_device_ait_initiate_update(FuLogitechTapHdmiDevice *self,
						GError **error)
{

	guint16 data_len = 0;
	g_autofree guint8 *mmp_get_data = NULL;
	guint8 ait_initiate_update[XU_INPUT_DATA_LEN] = {kLogiTapAitSetMmpCmdFwBurning,
                                     0,
                                     0,
                                     kLogiTapVideoAitInitiateSetMMPData,
                                     0,
                                     0,
                                     0,
                                     0};

	g_debug("Ait initiate update request");

	if (!fu_logitech_tap_hdmi_device_set_xu_control(self,
			kLogiUnitIdVidCapExtension,
			kLogiTapUvcXuAitCustomCsSetMmp,
			XU_INPUT_DATA_LEN,
			(guchar *)&ait_initiate_update,
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
			G_IO_ERROR,
			G_IO_ERROR_FAILED,
			"initiate query packet was too large at 0x%x bytes: ",
			data_len);
		return FALSE;
	}

	mmp_get_data = g_malloc0(data_len);
	if (!fu_logitech_tap_hdmi_device_get_xu_control(self,
			kLogiUnitIdVidCapExtension,
			kLogiTapUvcXuAitCustomCsGetMmpResult,
			data_len,
			(guchar *)mmp_get_data,
			error))
    	return FALSE;
	if (mmp_get_data[0] != kLogiDefaultAitSuccessValue) {
		g_set_error(error,
			G_IO_ERROR,
			G_IO_ERROR_FAILED,
			"failed to initialize AIT update, invalid result data: 0x%x",
			(guchar)mmp_get_data[0]);
    	return FALSE;
	}

	/* success */
	return TRUE;
  }

static gboolean
fu_logitech_tap_hdmi_device_ait_finalize_update(FuLogitechTapHdmiDevice *self,
						GError **error)
{

	guint duration_ms = 0;
	guint8 ait_finalize_update[XU_INPUT_DATA_LEN] = {kLogiTapAitSetMmpCmdFwBurning,
									kLogiTapVideoAitInitiateSetMMPData,
                                    0,
                                	0,
                                    0,
                                    0,
                                    0,
                                    0};

	g_debug("Ait finalize update request");

  fu_device_sleep(FU_DEVICE(self), 2*kLogiDefaultAitSleepIntervalMs); /* 2 sec */
   fu_device_sleep(FU_DEVICE(self), 2*kLogiDefaultAitSleepIntervalMs); /* 2 sec */
	if (!fu_logitech_tap_hdmi_device_set_xu_control(self,
			kLogiUnitIdVidCapExtension,
			kLogiTapUvcXuAitCustomCsSetMmp,
			XU_INPUT_DATA_LEN,
			(guchar *)&ait_finalize_update,
			error))
    	return FALSE;

  fu_device_sleep(FU_DEVICE(self), kLogiDefaultAitSleepIntervalMs); /* 1 sec */
 /* poll for burning fw result or return failure if it hits max polling */
 for (int pass = 0;; pass++) {
  g_autofree guint8 *mmp_get_data = NULL;
  guint16 data_len = 0;

  fu_device_sleep(FU_DEVICE(self), kLogiDefaultAitSleepIntervalMs); /* 1 sec */
  duration_ms = duration_ms + kLogiDefaultAitSleepIntervalMs;
	if (!fu_logitech_tap_hdmi_device_query_data_size(self,
			kLogiUnitIdVidCapExtension,
			kLogiTapUvcXuAitCustomCsGetMmpResult,
			&data_len,
			error))
		return FALSE;
	mmp_get_data = g_malloc0(data_len);
	if (!fu_logitech_tap_hdmi_device_get_xu_control(self,
			kLogiUnitIdVidCapExtension,
			kLogiTapUvcXuAitCustomCsGetMmpResult,
			data_len,
			(guchar *)mmp_get_data,
			error))
    	return FALSE;
	if (mmp_get_data[0] == kLogiDefaultAitSuccessValue) {
		if (pass == 0)
		  g_usleep(8 * G_USEC_PER_SEC);
		break;
	}
   else if (mmp_get_data[0] == kLogiDefaultAitFailureValue) {
	g_set_error(error,
			G_IO_ERROR,
			G_IO_ERROR_FAILED,
			"failed to finalize image burning, invalid result data: 0x%x",
			(guchar)mmp_get_data[0]);
    return FALSE;
   }
  if (duration_ms > kLogiDefaultAitFinalizeMaxPollingDurationMs) {
   /* if device never returns 0x82 or 0x00, bail out */
   	g_set_error(error,
			G_IO_ERROR,
			G_IO_ERROR_FAILED,
			"failed to finalize image burning, duration_ms: %u",
			duration_ms);
   return FALSE;
  }
 }

	/* success */
	return TRUE;
  }

static gboolean
fu_logitech_tap_hdmi_device_write_firmware(FuDevice *device,
				   GPtrArray *chunks,
				   FuProgress *progress,
				   GError **error)
{
    FuLogitechTapHdmiDevice *self = FU_LOGITECH_TAP_HDMI_DEVICE(device);

	g_debug("write HDMI firmware");
	/* flushes image */
	
    /* init */
	if (!fu_logitech_tap_hdmi_device_ait_initiate_update(self,
			error))
	return FALSE;

	/* write */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		g_autoptr(GByteArray) data_pkt = g_byte_array_new();
		/* device expect fixed size buffer, so cannot leverage actual size here: fu_chunk_get_data_sz(chk) */ 
		g_byte_array_append(data_pkt, fu_chunk_get_data(chk), kLogiDefaultImageBlockSize);
		if (!fu_logitech_tap_hdmi_device_set_xu_control(self,
			kLogiUnitIdVidCapExtension,
			kLogiUvcXuAitCustomCsSetFwData,
			data_pkt->len,
			(guchar *)data_pkt->data,
			error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* uninit */ 
	if (!fu_logitech_tap_hdmi_device_ait_finalize_update(self,
			error))
		return FALSE;

	/* signal for sensor device to trigger composite device reboot */
	fu_device_add_private_flag(device, FU_LOGITECH_TAP_HDMI_DEVICE_FLAG_NEEDS_REBOOT);
	return TRUE;
}

static gboolean
fu_logitech_tap_hdmi_device_set_version(FuDevice *device, GError **error)
{
	FuLogitechTapHdmiDevice *self = FU_LOGITECH_TAP_HDMI_DEVICE(device);
	guint16 data_len = 0;
	g_autofree guint8 *query_data = NULL;
    g_autofree gchar *fwversion_str = NULL;
    guint8 set_data[XU_INPUT_DATA_LEN] = {kLogiTapHdmiVerSetData,0,0,0,0,0,0,0};

	g_debug("get HDMI firmware version");

	if (!fu_logitech_tap_hdmi_device_set_xu_control(self,
						      kLogiUnitIdVidCapExtension,
						      kLogiTapCameraVersionSelector,
						      XU_INPUT_DATA_LEN,
						      (guchar *)set_data,
						      error))
		return FALSE;

	data_len = 0;
	/* query current device version */
	if (!fu_logitech_tap_hdmi_device_query_data_size(self,
						       kLogiUnitIdVidCapExtension,
						       kLogiHdmiVerGetSelector,
						       &data_len,
						       error))
		return FALSE;
	if (data_len > XU_INPUT_DATA_LEN) {
				g_set_error(error,
			G_IO_ERROR,
			G_IO_ERROR_FAILED,
			"version query packet was too large at 0x%x bytes: ",
			data_len);
		return FALSE;
	}

	query_data = g_malloc0(data_len);
	if (!fu_logitech_tap_hdmi_device_get_xu_control(self,
						      kLogiUnitIdVidCapExtension,
						      kLogiHdmiVerGetSelector,
						      data_len,
						      (guchar *)query_data,
						      error))
		return FALSE;

	/*  little-endian data. MajorVersion byte 3&2, MinorVersion byte 5&4, BuildVersion byte 7&6 */
	fwversion_str = g_strdup_printf("%i.%i.%i", query_data[3] + query_data[2] * 100, query_data[5] + query_data[4] * 100, query_data[7] + query_data[6] * 100);
	fu_device_set_version(device, fwversion_str);

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_tap_hdmi_device_setup(FuDevice *device, GError **error)
{
	/* setup device identifier so plugin can disntiguish device during composite_cleaup */
	fu_device_add_private_flag(device, FU_LOGITECH_TAP_DEVICE_TYPE_HDMI);
	return fu_logitech_tap_hdmi_device_set_version(device, error);
}

static gboolean
fu_logitech_tap_hdmi_device_probe(FuDevice *device, GError **error)
{
#if G_USB_CHECK_VERSION(0, 3, 3)
	const gchar *id_v4l_capabilities;
	const gchar *index;
    GUdevDevice *udev_device = fu_udev_device_get_dev(FU_UDEV_DEVICE(device));

	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS(fu_logitech_tap_hdmi_device_parent_class)->probe(device, error)) {
		return FALSE;
	}

	/* ignore unsupported susbystems */
	if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), "video4linux") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "is not correct subsystem=%s, expected video4linux",
			    fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)));
		return FALSE;
	}

	/* only interested in video capture device */
	id_v4l_capabilities = g_udev_device_get_property(udev_device, "ID_V4L_CAPABILITIES");
	if (g_strcmp0(id_v4l_capabilities, ":capture:") != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "only video capture device are supported");
		return FALSE;
	}

	/* interested in lowest index only e,g, video0, ignore low format siblings like video1/video2/video3 etc */
	index = g_udev_device_get_sysfs_attr(udev_device, "index");
    if (g_strcmp0(index, "0") != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "only device with lower index supported");
		return FALSE;
	};

	/* set the physical ID */
	return fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "video4linux", error);
#else
    /* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "this version of GUsb is not supported");
	return FALSE;
#endif
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
	fu_device_retry_set_delay(FU_DEVICE(self), 1000);
	fu_udev_device_set_flags(FU_UDEV_DEVICE(self),
				 FU_UDEV_DEVICE_FLAG_OPEN_READ | FU_UDEV_DEVICE_FLAG_OPEN_WRITE |
				     FU_UDEV_DEVICE_FLAG_OPEN_NONBLOCK | FU_UDEV_DEVICE_FLAG_IOCTL_RETRY);
}

static void
fu_logitech_tap_hdmi_device_finalize(GObject *object)
{
	G_OBJECT_CLASS(fu_logitech_tap_hdmi_device_parent_class)->finalize(object);
}

static void
fu_logitech_tap_hdmi_device_class_init(FuLogitechTapHdmiDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	FuLogitechTapDeviceClass *klass_logitech_tap_device = FU_LOGITECH_TAP_DEVICE_CLASS(klass);
	object_class->finalize = fu_logitech_tap_hdmi_device_finalize;
	klass_device->probe = fu_logitech_tap_hdmi_device_probe;
	klass_device->setup = fu_logitech_tap_hdmi_device_setup;
	klass_device->set_progress = fu_logitech_tap_hdmi_device_set_progress;
	klass_logitech_tap_device->write_firmware = fu_logitech_tap_hdmi_device_write_firmware;
}
