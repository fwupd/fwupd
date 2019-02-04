/*
 * Copyright (C) 2018-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fcntl.h>
#include <string.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>

#include <glib/gstdio.h>

#include "fu-chunk.h"
#include "fu-wacom-common.h"
#include "fu-wacom-device.h"
#include "dfu-firmware.h"

typedef struct
{
	gint			 fd;
	guint			 flash_block_size;
	guint32			 flash_base_addr;
	guint32			 flash_size;
} FuWacomDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuWacomDevice, fu_wacom_device, FU_TYPE_UDEV_DEVICE)

#define GET_PRIVATE(o) (fu_wacom_device_get_instance_private (o))

static void
fu_wacom_device_to_string (FuDevice *device, GString *str)
{
	FuWacomDevice *self = FU_WACOM_DEVICE (device);
	FuWacomDevicePrivate *priv = GET_PRIVATE (self);
	g_string_append (str, "  FuWacomDevice:\n");
	g_string_append_printf (str, "    fd:\t\t\t%i\n", priv->fd);
	g_string_append_printf (str, "    flash-block-size:\t0x%04x\n", priv->flash_block_size);
	g_string_append_printf (str, "    flash-base-addr:\t0x%04x\n", priv->flash_base_addr);
	g_string_append_printf (str, "    flash-size:\t\t0x%04x\n", priv->flash_size);
}

guint
fu_wacom_device_get_block_sz (FuWacomDevice *self)
{
	FuWacomDevicePrivate *priv = GET_PRIVATE (self);
	return priv->flash_block_size;
}

guint
fu_wacom_device_get_base_addr (FuWacomDevice *self)
{
	FuWacomDevicePrivate *priv = GET_PRIVATE (self);
	return priv->flash_base_addr;
}

gboolean
fu_wacom_device_check_mpu (FuWacomDevice *self, GError **error)
{
	FuWacomRawRequest req = {
		.cmd = FU_WACOM_RAW_BL_CMD_GET_MPUTYPE,
		.echo = FU_WACOM_RAW_ECHO_DEFAULT,
		0x00
	};
	FuWacomRawResponse rsp = { 0x00 };
	if (!fu_wacom_device_cmd (FU_WACOM_DEVICE (self), &req, &rsp, 0,
				  FU_WACOM_DEVICE_CMD_FLAG_NO_ERROR_CHECK, error)) {
		g_prefix_error (error, "failed to get MPU type: ");
		return FALSE;
	}

	/* W9013 */
	if (rsp.resp == 0x2e) {
		fu_device_add_instance_id (FU_DEVICE (self), "WacomEMR_W9013");
		return TRUE;
	}

	/* W9021 */
	if (rsp.resp == 0x45) {
		fu_device_add_instance_id (FU_DEVICE (self), "WacomEMR_W9021");
		return TRUE;
	}

	/* unsupported */
	g_set_error (error,
		     G_IO_ERROR,
		     G_IO_ERROR_FAILED,
		     "MPU is not W9013 or W9021: 0x%x",
		     rsp.resp);
	return FALSE;
}

static gboolean
fu_wacom_device_open (FuDevice *device, GError **error)
{
	FuWacomDevice *self = FU_WACOM_DEVICE (device);
	FuWacomDevicePrivate *priv = GET_PRIVATE (self);
	GUdevDevice *udev_device = fu_udev_device_get_dev (FU_UDEV_DEVICE (device));

	/* open device */
	priv->fd = g_open (g_udev_device_get_device_file (udev_device), O_RDWR);
	if (priv->fd < 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to open %s",
			     g_udev_device_get_device_file (udev_device));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_wacom_device_close (FuDevice *device, GError **error)
{
	FuWacomDevice *self = FU_WACOM_DEVICE (device);
	FuWacomDevicePrivate *priv = GET_PRIVATE (self);
	if (!g_close (priv->fd, error))
		return FALSE;
	priv->fd = 0;
	return TRUE;
}

static gboolean
fu_wacom_device_probe (FuUdevDevice *device, GError **error)
{
	/* set the physical ID */
	if (!fu_udev_device_set_physical_id (device, "hid", error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_wacom_device_detach (FuDevice *device, GError **error)
{
	FuWacomDevice *self = FU_WACOM_DEVICE (device);
	guint8 buf[FU_WACOM_RAW_FW_REPORT_SZ] = {
		FU_WACOM_RAW_FW_REPORT_ID,
		FU_WACOM_RAW_FW_CMD_DETACH,
	};
	if (!fu_wacom_device_set_feature (self, buf, sizeof(buf), error)) {
		g_prefix_error (error, "failed to switch to bootloader mode: ");
		return FALSE;
	}
	g_usleep (300 * 1000);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	return TRUE;
}

static gboolean
fu_wacom_device_attach (FuDevice *device, GError **error)
{
	FuWacomDevice *self = FU_WACOM_DEVICE (device);
	FuWacomRawRequest req = {
		.report_id = FU_WACOM_RAW_BL_REPORT_ID_SET,
		.cmd = FU_WACOM_RAW_BL_CMD_ATTACH,
		.echo = FU_WACOM_RAW_ECHO_DEFAULT,
		0x00
	};
	if (!fu_wacom_device_set_feature (self, (const guint8 *) &req, sizeof(req), error)) {
		g_prefix_error (error, "failed to switch to runtime mode: ");
		return FALSE;
	}
	/* only required on AES, but harmless for EMR */
	g_usleep (300 * 1000);
	fu_device_remove_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	return TRUE;
}

static gboolean
fu_wacom_device_check_mode (FuWacomDevice *self, GError **error)
{
	FuWacomRawRequest req = {
		.cmd = FU_WACOM_RAW_BL_CMD_CHECK_MODE,
		.echo = FU_WACOM_RAW_ECHO_DEFAULT,
		0x00
	};
	FuWacomRawResponse rsp = { 0x00 };
	if (!fu_wacom_device_cmd (self, &req, &rsp, 0,
				  FU_WACOM_DEVICE_CMD_FLAG_NO_ERROR_CHECK, error)) {
		g_prefix_error (error, "failed to check mode: ");
		return FALSE;
	}
	if (rsp.resp != 0x06) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "check mode failed, mode=0x%02x",
			     rsp.resp);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_wacom_device_set_version_bootloader (FuWacomDevice *self, GError **error)
{
	FuWacomRawRequest req = {
		.cmd = FU_WACOM_RAW_BL_CMD_GET_BLVER,
		.echo = FU_WACOM_RAW_ECHO_DEFAULT,
		0x00
	};
	FuWacomRawResponse rsp = { 0x00 };
	g_autofree gchar *version = NULL;
	if (!fu_wacom_device_cmd (self, &req, &rsp, 0,
				  FU_WACOM_DEVICE_CMD_FLAG_NO_ERROR_CHECK, error)) {
		g_prefix_error (error, "failed to get bootloader version: ");
		return FALSE;
	}
	version = g_strdup_printf ("%u", rsp.resp);
	fu_device_set_version_bootloader (FU_DEVICE (self), version);
	return TRUE;
}

static gboolean
fu_wacom_device_write_firmware (FuDevice *device, GBytes *fw, GError **error)
{
	FuWacomDevice *self = FU_WACOM_DEVICE (device);
	FuWacomDevicePrivate *priv = GET_PRIVATE (self);
	FuWacomDeviceClass *klass = FU_WACOM_DEVICE_GET_CLASS (device);
	DfuElement *element;
	DfuImage *image;
	GBytes *fw_new;
	g_autoptr(DfuFirmware) firmware = dfu_firmware_new ();
	g_autoptr(GPtrArray) chunks = NULL;

	/* parse hex file */
	if (!dfu_firmware_parse_data (firmware, fw, DFU_FIRMWARE_PARSE_FLAG_NONE, error))
		return FALSE;
	if (dfu_firmware_get_format (firmware) != DFU_FIRMWARE_FORMAT_INTEL_HEX) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "expected firmware format is 'ihex', got '%s'",
			     dfu_firmware_format_to_string (dfu_firmware_get_format (firmware)));
		return FALSE;
	}

	/* use the correct image from the firmware */
	image = dfu_firmware_get_image_default (firmware);
	if (image == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "no firmware image");
		return FALSE;
	}
	element = dfu_image_get_element_default (image);
	if (element == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "no element in image");
		return FALSE;
	}
	g_debug ("using element at addr 0x%0x",
		 (guint) dfu_element_get_address (element));

	/* check start address and size */
	if (dfu_element_get_address (element) != priv->flash_base_addr) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "base addr invalid: 0x%05x",
			     (guint) dfu_element_get_address (element));
		return FALSE;
	}
	fw_new = dfu_element_get_contents (element);
	if (g_bytes_get_size (fw_new) > priv->flash_size) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "size is invalid: 0x%05x",
			     (guint) g_bytes_get_size (fw_new));
		return FALSE;
	}

	/* we're in bootloader mode now */
	if (!fu_wacom_device_check_mode (self, error))
		return FALSE;
	if (!fu_wacom_device_set_version_bootloader (self, error))
		return FALSE;

	/* flash chunks */
	chunks = fu_chunk_array_new_from_bytes (fw_new, priv->flash_base_addr,
						0x00,	/* page_sz */
						priv->flash_block_size);
	return klass->write_firmware (device, chunks, error);
}

gboolean
fu_wacom_device_set_feature (FuWacomDevice *self,
			     const guint8 *data,
			     guint datasz,
			     GError **error)
{
	FuWacomDevicePrivate *priv = GET_PRIVATE (self);

	/* Set Feature */
	fu_common_dump_raw (G_LOG_DOMAIN, "SetFeature", data, datasz);
	if (ioctl (priv->fd, HIDIOCSFEATURE(datasz), data) < 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "failed to SetFeature");
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_wacom_device_get_feature (FuWacomDevice *self,
			     guint8 *data,
			     guint datasz,
			     GError **error)
{
	FuWacomDevicePrivate *priv = GET_PRIVATE (self);
	if (ioctl (priv->fd, HIDIOCGFEATURE(datasz), data) < 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "failed to GetFeature");
		return FALSE;
	}
	fu_common_dump_raw (G_LOG_DOMAIN, "GetFeature", data, datasz);
	return TRUE;
}

gboolean
fu_wacom_device_cmd (FuWacomDevice *self,
		     FuWacomRawRequest *req, FuWacomRawResponse *rsp,
		     gulong delay_us, FuWacomDeviceCmdFlags flags,
		     GError **error)
{
	req->report_id = FU_WACOM_RAW_BL_REPORT_ID_SET;
	if (!fu_wacom_device_set_feature (self, (const guint8 *)req, sizeof(*req), error)) {
		g_prefix_error (error, "failed to send: ");
		return FALSE;
	}
	if (delay_us > 0)
		g_usleep (delay_us);
	rsp->report_id = FU_WACOM_RAW_BL_REPORT_ID_GET;
	if (!fu_wacom_device_get_feature (self, (guint8 *)rsp, sizeof(*rsp), error)) {
		g_prefix_error (error, "failed to receive: ");
		return FALSE;
	}
	if (flags & FU_WACOM_DEVICE_CMD_FLAG_NO_ERROR_CHECK)
		return TRUE;
	if (!fu_wacom_common_check_reply (req, rsp, error))
		return FALSE;

	/* wait for the command to complete */
	if (flags & FU_WACOM_DEVICE_CMD_FLAG_POLL_ON_WAITING &&
	    rsp->resp != FU_WACOM_RAW_RC_OK) {
		for (guint i = 0; i < FU_WACOM_RAW_CMD_RETRIES; i++) {
			if (delay_us > 0)
				g_usleep (delay_us);
			if (!fu_wacom_device_get_feature (self, (guint8 *)rsp, sizeof(*rsp), error))
				return FALSE;
			if (!fu_wacom_common_check_reply (req, rsp, error))
				return FALSE;
			if (rsp->resp != FU_WACOM_RAW_RC_IN_PROGRESS &&
			    rsp->resp != FU_WACOM_RAW_RC_BUSY)
				break;
		}
	}
	return fu_wacom_common_rc_set_error (rsp, error);
}

static gboolean
fu_wacom_device_set_quirk_kv (FuDevice *device,
			      const gchar *key,
			      const gchar *value,
			      GError **error)
{
	FuWacomDevice *self = FU_WACOM_DEVICE (device);
	FuWacomDevicePrivate *priv = GET_PRIVATE (self);
	if (g_strcmp0 (key, "WacomI2cFlashBlockSize") == 0) {
		priv->flash_block_size = fu_common_strtoull (value);
		return TRUE;
	}
	if (g_strcmp0 (key, "WacomI2cFlashBaseAddr") == 0) {
		priv->flash_base_addr = fu_common_strtoull (value);
		return TRUE;
	}
	if (g_strcmp0 (key, "WacomI2cFlashSize") == 0) {
		priv->flash_size = fu_common_strtoull (value);
		return TRUE;
	}
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "quirk key not supported");
	return FALSE;
}

static void
fu_wacom_device_init (FuWacomDevice *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
}

static void
fu_wacom_device_class_init (FuWacomDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUdevDeviceClass *klass_device_udev = FU_UDEV_DEVICE_CLASS (klass);
	klass_device->to_string = fu_wacom_device_to_string;
	klass_device->open = fu_wacom_device_open;
	klass_device->close = fu_wacom_device_close;
	klass_device->write_firmware = fu_wacom_device_write_firmware;
	klass_device->attach = fu_wacom_device_attach;
	klass_device->detach = fu_wacom_device_detach;
	klass_device->set_quirk_kv = fu_wacom_device_set_quirk_kv;
	klass_device_udev->probe = fu_wacom_device_probe;
}
