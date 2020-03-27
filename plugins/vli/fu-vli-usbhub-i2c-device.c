/*
 * Copyright (C) 2017-2019 VIA Corporation
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-firmware-common.h"
#include "fu-ihex-firmware.h"

#include "fu-vli-usbhub-common.h"
#include "fu-vli-usbhub-device.h"
#include "fu-vli-usbhub-i2c-common.h"
#include "fu-vli-usbhub-i2c-device.h"

struct _FuVliUsbhubI2cDevice
{
	FuDevice		 parent_instance;
	FuVliDeviceKind		 device_kind;
};

G_DEFINE_TYPE (FuVliUsbhubI2cDevice, fu_vli_usbhub_i2c_device, FU_TYPE_DEVICE)

static void
fu_vli_usbhub_i2c_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuVliUsbhubI2cDevice *self = FU_VLI_USBHUB_I2C_DEVICE (device);
	fu_common_string_append_kv (str, idt, "DeviceKind",
				    fu_vli_common_device_kind_to_string (self->device_kind));
}

static gboolean
fu_vli_usbhub_i2c_device_setup (FuDevice *device, GError **error)
{
	FuVliUsbhubI2cDevice *self = FU_VLI_USBHUB_I2C_DEVICE (device);
	FuVliUsbhubDevice *parent = FU_VLI_USBHUB_DEVICE (fu_device_get_parent (device));
	guint8 buf[11] = { 0x0 };
	g_autofree gchar *instance_id = NULL;
	g_autofree gchar *version = NULL;

	/* get versions */
	if (!fu_vli_usbhub_device_i2c_read (parent,
					    FU_VLI_USBHUB_I2C_CMD_READ_VERSIONS,
					    buf, sizeof(buf), error)) {
		g_prefix_error (error, "failed to read versions: ");
		return FALSE;
	}
	if ((buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0x00) ||
	    (buf[0] == 0xff && buf[1] == 0xff && buf[2] == 0xff)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "no %s device detected",
			     fu_vli_common_device_kind_to_string (self->device_kind));
		return FALSE;
	}

	/* add instance ID */
	instance_id = g_strdup_printf ("USB\\VID_%04X&PID_%04X&I2C_%s",
				       fu_usb_device_get_vid (FU_USB_DEVICE (parent)),
				       fu_usb_device_get_pid (FU_USB_DEVICE (parent)),
				       fu_vli_common_device_kind_to_string (self->device_kind));
	fu_device_add_instance_id (device, instance_id);

	/* set version */
	version = g_strdup_printf ("%x.%x", buf[0], buf[1]);
	fu_device_set_version (device, version);
	return TRUE;
}

static gboolean
fu_vli_usbhub_i2c_device_detach (FuDevice *device, GError **error)
{
	FuVliUsbhubDevice *parent = FU_VLI_USBHUB_DEVICE (fu_device_get_parent (device));
	FuVliUsbhubI2cStatus status = 0xff;
	g_autoptr(FuDeviceLocker) locker = NULL;
	const guint8 buf[] = {
		FU_VLI_USBHUB_I2C_ADDR_WRITE,
		FU_VLI_USBHUB_I2C_CMD_UPGRADE,
	};

	/* open device */
	locker = fu_device_locker_new (parent, error);
	if (locker == NULL)
		return FALSE;
	if (!fu_vli_usbhub_device_i2c_write_data (parent, 0, 0, buf, sizeof(buf), error))
		return FALSE;

	/* avoid power instability by waiting T1 */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	fu_device_set_progress (device, 0);
	g_usleep (G_USEC_PER_SEC);

	/* check the device came back */
	if (!fu_vli_usbhub_device_i2c_read_status (parent, &status, error)) {
		g_prefix_error (error, "device did not come back after detach");
		return FALSE;
	}
	return fu_vli_usbhub_i2c_check_status (status, error);
}

static FuFirmware *
fu_vli_usbhub_i2c_device_prepare_firmware (FuDevice *device,
					   GBytes *fw,
					   FwupdInstallFlags flags,
					   GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_ihex_firmware_new ();
	fu_device_set_status (device, FWUPD_STATUS_DECOMPRESSING);
	if (!fu_firmware_tokenize (firmware, fw, flags, error))
		return NULL;
	return g_steal_pointer (&firmware);
}

typedef struct {
	guint8		 command;
	guint8		 buf[0x40];
	gsize		 bufsz;
	guint8		 len;
} FuVliUsbhubDeviceRequest;

static gboolean
fu_vli_usbhub_i2c_device_write_firmware_cb (FuDevice *device, gpointer user_data, GError **error)
{
	FuVliUsbhubDeviceRequest *req = (FuVliUsbhubDeviceRequest *) user_data;
	FuVliUsbhubDevice *parent = FU_VLI_USBHUB_DEVICE (fu_device_get_parent (device));
	FuVliUsbhubI2cStatus status = 0xff;

	g_usleep (5 * 1000);
	if (fu_usb_device_get_spec (FU_USB_DEVICE (parent)) >= 0x0300 ||
	    req->bufsz <= 32) {
		if (!fu_vli_usbhub_device_i2c_write_data (parent,
							  0, 0,
							  req->buf,
							  req->bufsz,
							  error))
			return FALSE;
	} else {
		/* for U2, hub data buffer <= 32 bytes */
		if (!fu_vli_usbhub_device_i2c_write_data (parent,
							  0, 1,
							  req->buf,
							  32,
							  error))
			return FALSE;
		if (!fu_vli_usbhub_device_i2c_write_data (parent,
							  1, 0,
							  req->buf + 32,
							  req->bufsz - 32,
							  error))
			return FALSE;
	}

	/* end of file, no need to check status */
	if (req->len == 0 && req->buf[6] == 0x01 && req->buf[7] == 0xFF)
		return TRUE;

	/* read data to check status */
	g_usleep (5 * 1000);
	if (!fu_vli_usbhub_device_i2c_read_status (parent, &status, error))
		return FALSE;
	return fu_vli_usbhub_i2c_check_status (status, error);
}

static gboolean
fu_vli_usbhub_i2c_device_write_firmware (FuDevice *device,
					FuFirmware *firmware,
					FwupdInstallFlags flags,
					GError **error)
{
	FuVliUsbhubDevice *parent = FU_VLI_USBHUB_DEVICE (fu_device_get_parent (device));
	GPtrArray *records = fu_ihex_firmware_get_records (FU_IHEX_FIRMWARE (firmware));
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuDevice) root = NULL;

	/* open device */
	locker = fu_device_locker_new (parent, error);
	if (locker == NULL)
		return FALSE;

	/* transfer by I²C write, and check status by I²C read */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	for (guint j = 0; j < records->len; j++) {
		FuIhexFirmwareRecord *rcd = g_ptr_array_index (records, j);
		FuVliUsbhubDeviceRequest req = { 0x0 };
		const gchar *line = rcd->buf->str;

		/* check there's enough data for the smallest possible record */
		if (rcd->buf->len < 11) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "line %u is incomplete, length %u",
				     rcd->ln, (guint) rcd->buf->len);
			return FALSE;
		}

		/* check starting token */
		if (line[0] != ':') {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "invalid starting token on line %u: %s",
				     rcd->ln, line);
			return FALSE;
		}

		/* length, 16-bit address, type */
		req.len = fu_firmware_strparse_uint8 (line + 1);
		if (req.len >= sizeof(req.buf) - 7) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "line too long; buffer size is 0x%x bytes",
				     (guint) sizeof(req.buf));
			return FALSE;
		}
		if (9 + (guint) req.len * 2 > (guint) rcd->buf->len) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "line %u malformed", rcd->ln);
			return FALSE;
		}

		/* write each record directly to the hardware */
		req.buf[0] = FU_VLI_USBHUB_I2C_ADDR_WRITE;
		req.buf[1] = FU_VLI_USBHUB_I2C_CMD_WRITE;
		req.buf[2] = 0x3a; /* ':' */
		req.buf[3] = req.len;
		req.buf[4] = fu_firmware_strparse_uint8 (line + 3);
		req.buf[5] = fu_firmware_strparse_uint8 (line + 5);
		req.buf[6] = fu_firmware_strparse_uint8 (line + 7);
		for (guint8 i = 0; i < req.len; i++)
			req.buf[7 + i] = fu_firmware_strparse_uint8 (line + 9 + (i * 2));
		req.buf[7 + req.len] = fu_firmware_strparse_uint8 (line + 9+ (req.len * 2));
		req.bufsz = req.len + 8;

		/* retry this if it fails */
		if (!fu_device_retry (device,
				      fu_vli_usbhub_i2c_device_write_firmware_cb,
				      5, &req, error))
			return FALSE;
		fu_device_set_progress_full (device, (gsize) j, (gsize) records->len);
	}

	/* the device automatically reboots */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	fu_device_set_progress (device, 0);

	/* this is unusual, but the MSP device reboot takes down the entire hub
	 * for ~60 seconds and we don't want the parent device removing us */
	root = fu_device_get_root (device);
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	fu_device_set_remove_delay (root, 120000);

	/* success */
	return TRUE;
}

static gboolean
fu_vli_usbhub_i2c_device_probe (FuDevice *device, GError **error)
{
	FuVliUsbhubI2cDevice *self = FU_VLI_USBHUB_I2C_DEVICE (device);
	self->device_kind = FU_VLI_DEVICE_KIND_MSP430;
	fu_device_set_name (device, fu_vli_common_device_kind_to_string (self->device_kind));
	return TRUE;
}

static void
fu_vli_usbhub_i2c_device_init (FuVliUsbhubI2cDevice *self)
{
	fu_device_add_icon (FU_DEVICE (self), "audio-card");
	fu_device_set_protocol (FU_DEVICE (self), "com.vli.i2c");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_PAIR);
	fu_device_set_logical_id (FU_DEVICE (self), "I2C");
	fu_device_set_summary (FU_DEVICE (self), "I²C Dock Management Device");
}

static void
fu_vli_usbhub_i2c_device_class_init (FuVliUsbhubI2cDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->to_string = fu_vli_usbhub_i2c_device_to_string;
	klass_device->probe = fu_vli_usbhub_i2c_device_probe;
	klass_device->setup = fu_vli_usbhub_i2c_device_setup;
	klass_device->detach = fu_vli_usbhub_i2c_device_detach;
	klass_device->write_firmware = fu_vli_usbhub_i2c_device_write_firmware;
	klass_device->prepare_firmware = fu_vli_usbhub_i2c_device_prepare_firmware;
}

FuDevice *
fu_vli_usbhub_i2c_device_new (FuVliUsbhubDevice *parent)
{
	FuVliUsbhubI2cDevice *self = g_object_new (FU_TYPE_VLI_USBHUB_I2C_DEVICE,
						   "parent", parent,
						   NULL);
	return FU_DEVICE (self);
}
