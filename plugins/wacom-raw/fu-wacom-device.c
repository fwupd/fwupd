/*
 * Copyright (C) 2018-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <linux/hidraw.h>
#include <sys/ioctl.h>

#include "fu-chunk.h"
#include "fu-ihex-firmware.h"
#include "fu-wacom-common.h"
#include "fu-wacom-device.h"

typedef struct
{
	guint			 flash_block_size;
	guint32			 flash_base_addr;
	guint32			 flash_size;
} FuWacomDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuWacomDevice, fu_wacom_device, FU_TYPE_UDEV_DEVICE)

#define GET_PRIVATE(o) (fu_wacom_device_get_instance_private (o))

static void
fu_wacom_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuWacomDevice *self = FU_WACOM_DEVICE (device);
	FuWacomDevicePrivate *priv = GET_PRIVATE (self);
	fu_common_string_append_kx (str, idt, "FlashBlockSize", priv->flash_block_size);
	fu_common_string_append_kx (str, idt, "FlashBaseAddr", priv->flash_base_addr);
	fu_common_string_append_kx (str, idt, "FlashSize", priv->flash_size);
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

static FuFirmware *
fu_wacom_device_prepare_firmware (FuDevice *device,
				  GBytes *fw,
				  FwupdInstallFlags flags,
				  GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_ihex_firmware_new ();
	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;
	return g_steal_pointer (&firmware);
}

static gboolean
fu_wacom_device_write_firmware (FuDevice *device,
				FuFirmware *firmware,
				FwupdInstallFlags flags,
				GError **error)
{
	FuWacomDevice *self = FU_WACOM_DEVICE (device);
	FuWacomDevicePrivate *priv = GET_PRIVATE (self);
	FuWacomDeviceClass *klass = FU_WACOM_DEVICE_GET_CLASS (device);
	g_autoptr(FuFirmwareImage) img = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* use the correct image from the firmware */
	img = fu_firmware_get_image_default (firmware, error);
	if (img == NULL)
		return FALSE;
	g_debug ("using element at addr 0x%0x",
		 (guint) fu_firmware_image_get_addr (img));

	/* check start address and size */
	if (fu_firmware_image_get_addr (img) != priv->flash_base_addr) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "base addr invalid: 0x%05x",
			     (guint) fu_firmware_image_get_addr (img));
		return FALSE;
	}
	fw = fu_firmware_image_write (img, error);
	if (fw == NULL)
		return FALSE;
	if (g_bytes_get_size (fw) > priv->flash_size) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "size is invalid: 0x%05x",
			     (guint) g_bytes_get_size (fw));
		return FALSE;
	}

	/* we're in bootloader mode now */
	if (!fu_wacom_device_check_mode (self, error))
		return FALSE;
	if (!fu_wacom_device_set_version_bootloader (self, error))
		return FALSE;

	/* flash chunks */
	chunks = fu_chunk_array_new_from_bytes (fw, priv->flash_base_addr,
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
	fu_common_dump_raw (G_LOG_DOMAIN, "SetFeature", data, datasz);
	return fu_udev_device_ioctl (FU_UDEV_DEVICE (self),
				     HIDIOCSFEATURE(datasz), (guint8 *) data,
				     NULL, error);
}

gboolean
fu_wacom_device_get_feature (FuWacomDevice *self,
			     guint8 *data,
			     guint datasz,
			     GError **error)
{
	if (!fu_udev_device_ioctl (FU_UDEV_DEVICE (self),
				   HIDIOCGFEATURE(datasz), data,
				   NULL, error))
		return FALSE;
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
	fu_device_set_protocol (FU_DEVICE (self), "com.wacom.raw");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_INTERNAL);
}

static void
fu_wacom_device_class_init (FuWacomDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUdevDeviceClass *klass_device_udev = FU_UDEV_DEVICE_CLASS (klass);
	klass_device->to_string = fu_wacom_device_to_string;
	klass_device->prepare_firmware = fu_wacom_device_prepare_firmware;
	klass_device->write_firmware = fu_wacom_device_write_firmware;
	klass_device->attach = fu_wacom_device_attach;
	klass_device->detach = fu_wacom_device_detach;
	klass_device->set_quirk_kv = fu_wacom_device_set_quirk_kv;
	klass_device_udev->probe = fu_wacom_device_probe;
}
