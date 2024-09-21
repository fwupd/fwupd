/*
 * Copyright 2017 VIA Corporation
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-vli-struct.h"
#include "fu-vli-usbhub-common.h"
#include "fu-vli-usbhub-i2c-common.h"
#include "fu-vli-usbhub-msp430-device.h"

struct _FuVliUsbhubMsp430Device {
	FuDevice parent_instance;
};

G_DEFINE_TYPE(FuVliUsbhubMsp430Device, fu_vli_usbhub_msp430_device, FU_TYPE_DEVICE)

/* Texas Instruments BSL */
#define I2C_ADDR_WRITE 0x18
#define I2C_ADDR_READ  0x19

#define I2C_CMD_WRITE	      0x32
#define I2C_CMD_READ_STATUS   0x33
#define I2C_CMD_UPGRADE	      0x34
#define I2C_CMD_READ_VERSIONS 0x40

#define I2C_R_VDR 0xa0 /* read vendor command */
#define I2C_W_VDR 0xb0 /* write vendor command */

static gboolean
fu_vli_usbhub_msp430_device_i2c_read(FuVliUsbhubDevice *self,
				     guint8 cmd,
				     guint8 *buf,
				     gsize bufsz,
				     GError **error)
{
	guint16 value = ((guint16)I2C_ADDR_WRITE << 8) | cmd;
	guint16 index = (guint16)I2C_ADDR_READ << 8;
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_DEVICE_TO_HOST,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    I2C_R_VDR,
					    value,
					    index,
					    buf,
					    bufsz,
					    NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL,
					    error)) {
		g_prefix_error(error, "failed to read I2C: ");
		return FALSE;
	}
	fu_dump_raw(G_LOG_DOMAIN, "I2cReadData", buf, bufsz);
	return TRUE;
}

static gboolean
fu_vli_usbhub_msp430_device_i2c_read_status(FuVliUsbhubDevice *self,
					    FuVliUsbhubI2cStatus *status,
					    GError **error)
{
	guint8 buf[1] = {0xff};
	if (!fu_vli_usbhub_msp430_device_i2c_read(self,
						  I2C_CMD_READ_STATUS,
						  buf,
						  sizeof(buf),
						  error))
		return FALSE;
	if (status != NULL)
		*status = buf[0];
	return TRUE;
}

static gboolean
fu_vli_usbhub_msp430_device_i2c_write_data(FuVliUsbhubDevice *self,
					   guint8 disable_start_bit,
					   guint8 disable_end_bit,
					   const guint8 *buf,
					   gsize bufsz,
					   GError **error)
{
	guint16 value = (((guint16)disable_start_bit) << 8) | disable_end_bit;
	g_autofree guint8 *buf_mut = NULL;

	fu_dump_raw(G_LOG_DOMAIN, "I2cWriteData", buf, bufsz);
	buf_mut = fu_memdup_safe(buf, bufsz, error);
	if (buf_mut == NULL)
		return FALSE;
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    I2C_W_VDR,
					    value,
					    0x0,
					    buf_mut,
					    bufsz,
					    NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL,
					    error)) {
		g_prefix_error(error, "failed to write I2C @0x%x: ", value);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_usbhub_msp430_device_setup(FuDevice *device, GError **error)
{
	FuVliUsbhubDevice *parent = FU_VLI_USBHUB_DEVICE(fu_device_get_parent(device));
	guint8 buf[11] = {0x0};
	g_autofree gchar *version = NULL;

	/* get versions */
	if (!fu_vli_usbhub_msp430_device_i2c_read(parent,
						  I2C_CMD_READ_VERSIONS,
						  buf,
						  sizeof(buf),
						  error)) {
		g_prefix_error(error, "failed to read versions: ");
		return FALSE;
	}
	if ((buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0x00) ||
	    (buf[0] == 0xff && buf[1] == 0xff && buf[2] == 0xff)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "no MSP430 device detected");
		return FALSE;
	}

	/* set version */
	version = g_strdup_printf("%x.%x", buf[0], buf[1]);
	fu_device_set_version(device, version);
	return TRUE;
}

static gboolean
fu_vli_usbhub_msp430_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuVliUsbhubDevice *parent = FU_VLI_USBHUB_DEVICE(fu_device_get_parent(device));
	FuVliUsbhubI2cStatus status = 0xff;
	g_autoptr(FuDeviceLocker) locker = NULL;
	const guint8 buf[] = {
	    I2C_ADDR_WRITE,
	    I2C_CMD_UPGRADE,
	};

	/* open device */
	locker = fu_device_locker_new(parent, error);
	if (locker == NULL)
		return FALSE;
	if (!fu_vli_usbhub_msp430_device_i2c_write_data(parent, 0, 0, buf, sizeof(buf), error))
		return FALSE;

	/* avoid power instability by waiting T1 */
	fu_device_sleep_full(device, 1000, progress); /* ms */

	/* check the device came back */
	if (!fu_vli_usbhub_msp430_device_i2c_read_status(parent, &status, error)) {
		g_prefix_error(error, "device did not come back after detach: ");
		return FALSE;
	}
	return fu_vli_usbhub_i2c_check_status(status, error);
}

typedef struct {
	guint8 command;
	guint8 buf[0x40];
	gsize bufsz;
	guint8 len;
} FuVliUsbhubDeviceRequest;

static gboolean
fu_vli_usbhub_msp430_device_write_firmware_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuVliUsbhubDeviceRequest *req = (FuVliUsbhubDeviceRequest *)user_data;
	FuVliUsbhubDevice *parent = FU_VLI_USBHUB_DEVICE(fu_device_get_parent(device));
	FuVliUsbhubI2cStatus status = 0xff;

	fu_device_sleep(device, 5); /* ms */
	if (fu_usb_device_get_spec(FU_USB_DEVICE(parent)) >= 0x0300 || req->bufsz <= 32) {
		if (!fu_vli_usbhub_msp430_device_i2c_write_data(parent,
								0,
								0,
								req->buf,
								req->bufsz,
								error))
			return FALSE;
	} else {
		/* for U2, hub data buffer <= 32 bytes */
		if (!fu_vli_usbhub_msp430_device_i2c_write_data(parent, 0, 1, req->buf, 32, error))
			return FALSE;
		if (!fu_vli_usbhub_msp430_device_i2c_write_data(parent,
								1,
								0,
								req->buf + 32,
								req->bufsz - 32,
								error))
			return FALSE;
	}

	/* end of file, no need to check status */
	if (req->len == 0 && req->buf[6] == 0x01 && req->buf[7] == 0xFF)
		return TRUE;

	/* read data to check status */
	fu_device_sleep(device, 5); /* ms */
	if (!fu_vli_usbhub_msp430_device_i2c_read_status(parent, &status, error))
		return FALSE;
	return fu_vli_usbhub_i2c_check_status(status, error);
}

static gboolean
fu_vli_usbhub_msp430_device_write_firmware(FuDevice *device,
					   FuFirmware *firmware,
					   FuProgress *progress,
					   FwupdInstallFlags flags,
					   GError **error)
{
	FuVliUsbhubDevice *parent = FU_VLI_USBHUB_DEVICE(fu_device_get_parent(device));
	GPtrArray *records = fu_ihex_firmware_get_records(FU_IHEX_FIRMWARE(firmware));
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open device */
	locker = fu_device_locker_new(parent, error);
	if (locker == NULL)
		return FALSE;

	/* transfer by I²C write, and check status by I²C read */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	fu_progress_set_steps(progress, records->len);
	for (guint j = 0; j < records->len; j++) {
		FuIhexFirmwareRecord *rcd = g_ptr_array_index(records, j);
		FuVliUsbhubDeviceRequest req = {0x0};
		const gchar *line = rcd->buf->str;

		/* length, 16-bit address, type */
		req.len = rcd->byte_cnt;
		if (req.len >= sizeof(req.buf) - 7) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "line too long; buffer size is 0x%x bytes",
				    (guint)sizeof(req.buf));
			return FALSE;
		}

		/* write each record directly to the hardware */
		req.buf[0] = I2C_ADDR_WRITE;
		req.buf[1] = I2C_CMD_WRITE;
		req.buf[2] = 0x3a; /* ':' */
		req.buf[3] = req.len;
		if (!fu_firmware_strparse_uint8_safe(line, rcd->buf->len, 3, &req.buf[4], error))
			return FALSE;
		if (!fu_firmware_strparse_uint8_safe(line, rcd->buf->len, 5, &req.buf[5], error))
			return FALSE;
		if (!fu_firmware_strparse_uint8_safe(line, rcd->buf->len, 7, &req.buf[6], error))
			return FALSE;
		for (guint8 i = 0; i < req.len; i++) {
			if (!fu_firmware_strparse_uint8_safe(line,
							     rcd->buf->len,
							     9 + (i * 2),
							     &req.buf[7 + i],
							     error))
				return FALSE;
		}
		if (!fu_firmware_strparse_uint8_safe(line,
						     rcd->buf->len,
						     9 + (req.len * 2),
						     &req.buf[7 + req.len],
						     error))
			return FALSE;
		req.bufsz = req.len + 8;

		/* retry this if it fails */
		if (!fu_device_retry(device,
				     fu_vli_usbhub_msp430_device_write_firmware_cb,
				     5,
				     &req,
				     error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* the device automatically reboots */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	/* success */
	return TRUE;
}

static gboolean
fu_vli_usbhub_msp430_device_probe(FuDevice *device, GError **error)
{
	FuVliDeviceKind device_kind = FU_VLI_DEVICE_KIND_MSP430;
	FuVliUsbhubDevice *parent = FU_VLI_USBHUB_DEVICE(fu_device_get_parent(device));

	fu_device_set_name(device, fu_vli_device_kind_to_string(device_kind));
	fu_device_incorporate(device, FU_DEVICE(parent), FU_DEVICE_INCORPORATE_FLAG_PHYSICAL_ID);

	/* add instance ID */
	fu_device_add_instance_str(device, "I2C", fu_vli_device_kind_to_string(device_kind));
	return fu_device_build_instance_id(device, error, "USB", "VID", "PID", NULL);
}

static void
fu_vli_usbhub_msp430_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 13, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 85, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_vli_usbhub_msp430_device_init(FuVliUsbhubMsp430Device *self)
{
	fu_device_add_icon(FU_DEVICE(self), "usb-hub");
	fu_device_add_protocol(FU_DEVICE(self), "com.vli.i2c");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
	fu_device_set_logical_id(FU_DEVICE(self), "I2C");
	fu_device_set_summary(FU_DEVICE(self), "I²C dock management device");
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_IHEX_FIRMWARE);

	/* the MSP device reboot takes down the entire hub for ~60 seconds */
	fu_device_set_remove_delay(FU_DEVICE(self), 120 * 1000);
}

static void
fu_vli_usbhub_msp430_device_class_init(FuVliUsbhubMsp430DeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_vli_usbhub_msp430_device_probe;
	device_class->setup = fu_vli_usbhub_msp430_device_setup;
	device_class->detach = fu_vli_usbhub_msp430_device_detach;
	device_class->write_firmware = fu_vli_usbhub_msp430_device_write_firmware;
	device_class->set_progress = fu_vli_usbhub_msp430_device_set_progress;
}

FuDevice *
fu_vli_usbhub_msp430_device_new(FuVliUsbhubDevice *parent)
{
	FuVliUsbhubMsp430Device *self =
	    g_object_new(FU_TYPE_VLI_USBHUB_MSP430_DEVICE, "parent", parent, NULL);
	return FU_DEVICE(self);
}
