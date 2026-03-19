/*
 * Copyright 2026 Sean Rhodes <sean@starlabs.systems>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <linux/types.h>
#include <linux/usb/video.h>
#include <linux/uvcvideo.h>

#include "fu-sunplus-camera-device.h"
#include "fu-sunplus-camera-struct.h"

#define FU_SUNPLUS_CAMERA_IOCTL_TIMEOUT 5000
#define FU_SUNPLUS_CAMERA_READ_SIZE	64

struct _FuSunplusCameraDevice {
	FuV4lDevice parent_instance;
};

G_DEFINE_TYPE(FuSunplusCameraDevice, fu_sunplus_camera_device, FU_TYPE_V4L_DEVICE)

static gboolean
fu_sunplus_camera_device_ioctl_buffer_cb(FuIoctl *ioctl,
					 gpointer ptr,
					 guint8 *buf,
					 gsize bufsz,
					 GError **error)
{
	struct uvc_xu_control_query *query = (struct uvc_xu_control_query *)ptr;
	query->data = buf;
	query->size = bufsz;
	return TRUE;
}

static gboolean
fu_sunplus_camera_device_xu_query(FuSunplusCameraDevice *self,
				  guint8 selector,
				  guint8 query_code,
				  guint8 *buf,
				  guint16 bufsz,
				  GError **error)
{
	struct uvc_xu_control_query query = {
	    .unit = FU_SUNPLUS_CAMERA_XU_UNIT_ID,
	    .selector = selector,
	    .query = query_code,
	};
	g_autoptr(FuIoctl) ioctl = fu_udev_device_ioctl_new(FU_UDEV_DEVICE(self));

	fu_ioctl_add_key_as_u16(ioctl, "Request", UVCIOC_CTRL_QUERY);
	fu_ioctl_add_key_as_u8(ioctl, "Unit", query.unit);
	fu_ioctl_add_key_as_u8(ioctl, "Selector", query.selector);
	fu_ioctl_add_key_as_u8(ioctl, "Query", query.query);
	fu_ioctl_add_mutable_buffer(ioctl,
				    NULL,
				    buf,
				    bufsz,
				    fu_sunplus_camera_device_ioctl_buffer_cb);
	if (!fu_ioctl_execute(ioctl,
			      UVCIOC_CTRL_QUERY,
			      (guint8 *)&query,
			      sizeof(query),
			      NULL,
			      FU_SUNPLUS_CAMERA_IOCTL_TIMEOUT,
			      FU_IOCTL_FLAG_NONE,
			      error)) {
		g_prefix_error(error,
			       "failed xu query unit=0x%02x selector=0x%02x query=0x%02x: ",
			       query.unit,
			       query.selector,
			       query.query);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_sunplus_camera_device_xu_get_len(FuSunplusCameraDevice *self,
				    guint8 selector,
				    guint16 *len,
				    GError **error)
{
	guint8 buf[2] = {0x0};
	if (!fu_sunplus_camera_device_xu_query(self,
					       selector,
					       UVC_GET_LEN,
					       buf,
					       sizeof(buf),
					       error))
		return FALSE;
	*len = (guint16)(buf[0] | ((guint16)buf[1] << 8));
	return TRUE;
}

static gboolean
fu_sunplus_camera_device_xu_get_cur(FuSunplusCameraDevice *self,
				    guint8 selector,
				    guint8 *buf,
				    guint16 bufsz,
				    GError **error)
{
	return fu_sunplus_camera_device_xu_query(self, selector, UVC_GET_CUR, buf, bufsz, error);
}

static gboolean
fu_sunplus_camera_device_xu_set_cur(FuSunplusCameraDevice *self,
				    guint8 selector,
				    guint8 *buf,
				    guint16 bufsz,
				    GError **error)
{
	return fu_sunplus_camera_device_xu_query(self, selector, UVC_SET_CUR, buf, bufsz, error);
}

static gboolean
fu_sunplus_camera_device_set_enabled(FuSunplusCameraDevice *self, guint8 value, GError **error)
{
	guint8 buf[1] = {value};
	if (!fu_sunplus_camera_device_xu_set_cur(self,
						 FU_SUNPLUS_CAMERA_SELECTOR_ENABLE,
						 buf,
						 sizeof(buf),
						 error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), 200);
	return TRUE;
}

static gboolean
fu_sunplus_camera_device_set_read_addr(FuSunplusCameraDevice *self, guint32 addr, GError **error)
{
	guint8 buf[4] = {
	    (guint8)(addr & 0xff),
	    (guint8)((addr >> 8) & 0xff),
	    (guint8)((addr >> 16) & 0xff),
	    (guint8)((addr >> 24) & 0xff),
	};
	return fu_sunplus_camera_device_xu_set_cur(self,
						   FU_SUNPLUS_CAMERA_SELECTOR_READ_ADDR,
						   buf,
						   sizeof(buf),
						   error);
}

static gboolean
fu_sunplus_camera_device_set_asic_register(FuSunplusCameraDevice *self,
					   guint16 reg,
					   guint8 value,
					   GError **error)
{
	guint8 cmd[2] = {
	    (guint8)(reg & 0xff),
	    (guint8)((reg >> 8) & 0xff),
	};

	if (!fu_sunplus_camera_device_xu_set_cur(self,
						 FU_SUNPLUS_CAMERA_SELECTOR_REG16_CMD,
						 cmd,
						 sizeof(cmd),
						 error))
		return FALSE;
	if (!fu_sunplus_camera_device_xu_set_cur(self,
						 FU_SUNPLUS_CAMERA_SELECTOR_REG8_DATA,
						 &value,
						 sizeof(value),
						 error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_sunplus_camera_device_soft_reset(FuSunplusCameraDevice *self, GError **error)
{
	static const struct {
		guint16 reg;
		guint8 value;
	} steps[] = {
	    {0x2034, 0x00},
	    {0x2030, 0xff},
	    {0x2031, 0x00},
	    {0x2032, 0x00},
	    {0x2033, 0x02},
	    {0x2034, 0x01},
	};

	for (guint i = 0; i < G_N_ELEMENTS(steps); i++) {
		if (!fu_sunplus_camera_device_set_asic_register(self,
								steps[i].reg,
								steps[i].value,
								error)) {
			g_prefix_error(error,
				       "failed soft-reset at reg=0x%04x val=0x%02x: ",
				       steps[i].reg,
				       steps[i].value);
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_sunplus_camera_device_read_chunk(FuSunplusCameraDevice *self,
				    guint32 addr,
				    guint8 *buf,
				    gsize bufsz,
				    GError **error)
{
	if (bufsz != FU_SUNPLUS_CAMERA_READ_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid read size %" G_GSIZE_FORMAT,
			    bufsz);
		return FALSE;
	}
	if (!fu_sunplus_camera_device_set_read_addr(self, addr, error))
		return FALSE;
	if (!fu_sunplus_camera_device_xu_get_cur(self,
						 FU_SUNPLUS_CAMERA_SELECTOR_READ_CHUNK,
						 buf,
						 (guint16)bufsz,
						 error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_sunplus_camera_device_ensure_version(FuSunplusCameraDevice *self, GError **error)
{
	guint64 version_raw = 0;
	g_autofree gchar *id_revision = NULL;
	g_autoptr(FuDevice) usb_device =
	    fu_device_get_backend_parent_with_subsystem(FU_DEVICE(self), "usb:usb_device", NULL);

	if (usb_device != NULL) {
		if (!fu_device_probe(usb_device, error)) {
			g_prefix_error_literal(error, "failed to probe usb_device: ");
			return FALSE;
		}
		version_raw = fu_usb_device_get_release(FU_USB_DEVICE(usb_device));
		if (version_raw != 0) {
			fu_device_set_version_raw(FU_DEVICE(self), version_raw);
			return TRUE;
		}
	}

	id_revision = fu_udev_device_read_property(FU_UDEV_DEVICE(self), "ID_REVISION", NULL);
	if (id_revision != NULL) {
		if (!fu_strtoull(id_revision,
				 &version_raw,
				 0,
				 G_MAXUINT16,
				 FU_INTEGER_BASE_16,
				 error)) {
			g_prefix_error_literal(error, "failed to parse ID_REVISION: ");
			return FALSE;
		}
		fu_device_set_version_raw(FU_DEVICE(self), version_raw);
		return TRUE;
	}

	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to determine device version from usb_device or ID_REVISION");
	return FALSE;
}

static gboolean
fu_sunplus_camera_device_probe(FuDevice *device, GError **error)
{
	if (!FU_DEVICE_CLASS(fu_sunplus_camera_device_parent_class)->probe(device, error))
		return FALSE;

	if (fu_v4l_device_get_index(FU_V4L_DEVICE(device)) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "only the primary video4linux node is supported");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_sunplus_camera_device_setup(FuDevice *device, GError **error)
{
	FuSunplusCameraDevice *self = FU_SUNPLUS_CAMERA_DEVICE(device);

	if (!FU_DEVICE_CLASS(fu_sunplus_camera_device_parent_class)->setup(device, error))
		return FALSE;
	if ((fu_v4l_device_get_caps(FU_V4L_DEVICE(device)) & FU_V4L_CAP_VIDEO_CAPTURE) == 0) {
		g_autofree gchar *caps =
		    fu_v4l_cap_to_string(fu_v4l_device_get_caps(FU_V4L_DEVICE(device)));
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "only video capture devices are supported, got %s",
			    caps);
		return FALSE;
	}
	return fu_sunplus_camera_device_ensure_version(self, error);
}

static gboolean
fu_sunplus_camera_device_verify(FuSunplusCameraDevice *self,
				FuChunkArray *chunks,
				FuProgress *progress,
				GError **error)
{
	guint8 buf[FU_SUNPLUS_CAMERA_READ_SIZE] = {0};

	fu_progress_set_id(progress, G_STRLOC);
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_sunplus_camera_device_read_chunk(self,
							 (guint32)fu_chunk_get_address(chk),
							 buf,
							 sizeof(buf),
							 error))
			return FALSE;
		if (memcmp(buf, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk)) != 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "verify mismatch at 0x%04x",
				    (guint)fu_chunk_get_address(chk));
			return FALSE;
		}
		fu_progress_set_percentage_full(progress, i + 1, fu_chunk_array_length(chunks));
	}
	return TRUE;
}

static gboolean
fu_sunplus_camera_device_disable_after_failure(FuSunplusCameraDevice *self, gboolean enabled)
{
	if (enabled) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_sunplus_camera_device_set_enabled(self, 0x00, &error_local) &&
		    error_local != NULL) {
			g_debug("disable after failure did not succeed: %s", error_local->message);
		}
	}
	return FALSE;
}

static gboolean
fu_sunplus_camera_device_write_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	FuSunplusCameraDevice *self = FU_SUNPLUS_CAMERA_DEVICE(device);
	gsize payload_sz = 0;
	guint16 chunk_len = 0;
	guint8 checksum = 0;
	guint8 checksum_dev = 0;
	guint8 finish = 0x01;
	gboolean enabled = FALSE;
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autofree guint8 *buf = NULL;

	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;
	payload_sz = fu_firmware_get_size(firmware);
	if (payload_sz == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "firmware payload was empty");
		return FALSE;
	}

	if (!fu_sunplus_camera_device_xu_get_len(self,
						 FU_SUNPLUS_CAMERA_SELECTOR_ACCESS,
						 &chunk_len,
						 error))
		return FALSE;
	if (chunk_len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "selector 10 reported zero length");
		return FALSE;
	}
	chunks = fu_chunk_array_new_from_stream(stream,
						FU_CHUNK_ADDR_OFFSET_NONE,
						FU_CHUNK_PAGESZ_NONE,
						chunk_len,
						error);
	if (chunks == NULL)
		return FALSE;
	buf = g_malloc(chunk_len);

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, "enable");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 70, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, "finish");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 20, "verify");

	if (!fu_sunplus_camera_device_set_enabled(self, 0x01, error))
		return FALSE;
	enabled = TRUE;
	fu_progress_step_done(progress);

	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = fu_chunk_array_index(chunks, i, error);
		gsize chunk_sz = 0;
		if (chk == NULL)
			return fu_sunplus_camera_device_disable_after_failure(self, enabled);
		chunk_sz = fu_chunk_get_data_sz(chk);

		memset(buf, 0xff, chunk_len);
		if (!fu_memcpy_safe(buf,
				    chunk_len,
				    0x0,
				    fu_chunk_get_data(chk),
				    chunk_sz,
				    0x0,
				    chunk_sz,
				    error))
			return fu_sunplus_camera_device_disable_after_failure(self, enabled);
		for (guint16 j = 0; j < chunk_len; j++)
			checksum ^= buf[j];
		if (!fu_sunplus_camera_device_xu_set_cur(self,
							 FU_SUNPLUS_CAMERA_SELECTOR_ACCESS,
							 buf,
							 chunk_len,
							 error))
			return fu_sunplus_camera_device_disable_after_failure(self, enabled);
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						fu_chunk_get_address(chk) + chunk_sz,
						payload_sz);
		fu_device_sleep(device, 10);
	}
	fu_progress_step_done(progress);

	fu_device_sleep(device, 1000);
	if (!fu_sunplus_camera_device_xu_get_cur(self,
						 FU_SUNPLUS_CAMERA_SELECTOR_CHECKSUM,
						 &checksum_dev,
						 sizeof(checksum_dev),
						 error))
		return fu_sunplus_camera_device_disable_after_failure(self, enabled);
	if (checksum_dev != checksum) {
		g_set_error(error, /* nocheck:error-false-return */
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "device checksum mismatch 0x%02x != 0x%02x",
			    checksum_dev,
			    checksum);
		return fu_sunplus_camera_device_disable_after_failure(self, enabled);
	}
	if (!fu_sunplus_camera_device_xu_set_cur(self,
						 FU_SUNPLUS_CAMERA_SELECTOR_FINISH,
						 &finish,
						 sizeof(finish),
						 error))
		return fu_sunplus_camera_device_disable_after_failure(self, enabled);
	fu_progress_step_done(progress);

	if (!fu_sunplus_camera_device_verify(self, chunks, fu_progress_get_child(progress), error))
		return fu_sunplus_camera_device_disable_after_failure(self, enabled);
	fu_progress_step_done(progress);
	return TRUE;
}

static void
fu_sunplus_camera_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-firmware");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "reload");
}

static gboolean
fu_sunplus_camera_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuSunplusCameraDevice *self = FU_SUNPLUS_CAMERA_DEVICE(device);

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_RESTART);
	if (!fu_sunplus_camera_device_soft_reset(self, error))
		return FALSE;
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gchar *
fu_sunplus_camera_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint16((guint16)version_raw, fu_device_get_version_format(device));
}

static void
fu_sunplus_camera_device_init(FuSunplusCameraDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.sunplus.camera");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_BCD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_device_set_remove_delay(FU_DEVICE(self), 30 * 1000);
	fu_device_set_install_duration(FU_DEVICE(self), 60);
}

static void
fu_sunplus_camera_device_class_init(FuSunplusCameraDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_sunplus_camera_device_probe;
	device_class->setup = fu_sunplus_camera_device_setup;
	device_class->set_progress = fu_sunplus_camera_device_set_progress;
	device_class->attach = fu_sunplus_camera_device_attach;
	device_class->convert_version = fu_sunplus_camera_device_convert_version;
	device_class->write_firmware = fu_sunplus_camera_device_write_firmware;
}
