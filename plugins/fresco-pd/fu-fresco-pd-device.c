/*
 * Copyright (C) 2020 Fresco Logic
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-fresco-pd-common.h"
#include "fu-fresco-pd-device.h"
#include "fu-fresco-pd-firmware.h"

struct _FuFrescoPdDevice
{
	FuUsbDevice		 parent_instance;
	guint8			 customer_id;
};

G_DEFINE_TYPE (FuFrescoPdDevice, fu_fresco_pd_device, FU_TYPE_USB_DEVICE)

static void
fu_fresco_pd_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuFrescoPdDevice *self = FU_FRESCO_PD_DEVICE (device);
	fu_common_string_append_ku (str, idt, "CustomerID", self->customer_id);
}

static gboolean
fu_fresco_pd_device_transfer_read (FuFrescoPdDevice *self,
				   guint16 offset,
				   guint8 *buf,
				   guint16 bufsz,
				   GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	gsize actual_length = 0;

	g_return_val_if_fail (buf != NULL, FALSE);
	g_return_val_if_fail (bufsz != 0, FALSE);

	/* to device */
	if (g_getenv ("FWUPD_FRESCO_PD_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "read", buf, bufsz);
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0x40, 0x0, offset,
					    buf, bufsz, &actual_length,
					    5000, NULL, error)) {
		g_prefix_error (error, "failed to read from offset 0x%x: ", offset);
		return FALSE;
	}
	if (bufsz != actual_length) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "read 0x%x bytes of 0x%x",
			     (guint) actual_length, bufsz);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_fresco_pd_device_transfer_write (FuFrescoPdDevice *self,
				    guint16 offset,
				    guint8 *buf,
				    guint16 bufsz,
				    GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	gsize actual_length = 0;

	g_return_val_if_fail (buf != NULL, FALSE);
	g_return_val_if_fail (bufsz != 0, FALSE);

	/* to device */
	if (g_getenv ("FWUPD_FRESCO_PD_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "write", buf, bufsz);
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0x41, 0x0, offset,
					    buf, bufsz, &actual_length,
					    5000, NULL, error)) {
		g_prefix_error (error, "failed to write offset 0x%x: ", offset);
		return FALSE;
	}
	if (bufsz != actual_length) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "wrote 0x%x bytes of 0x%x",
			     (guint) actual_length, bufsz);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_fresco_pd_device_read_byte (FuFrescoPdDevice *self,
			       guint16 offset,
			       guint8 *buf,
			       GError **error)
{
	return fu_fresco_pd_device_transfer_read (self, offset, buf, 1, error);
}

static gboolean
fu_fresco_pd_device_write_byte (FuFrescoPdDevice *self,
				guint16 offset,
				guint8 buf,
				GError **error)
{
	return fu_fresco_pd_device_transfer_write (self, offset, &buf, 1, error);
}

static gboolean
fu_fresco_pd_device_set_byte (FuFrescoPdDevice *self,
			      guint16 offset,
			      guint8 val,
			      GError **error)
{
	guint8 buf = 0x0;
	if (!fu_fresco_pd_device_read_byte (self, offset, &buf, error))
		return FALSE;
	if (buf == val)
		return TRUE;
	return fu_fresco_pd_device_write_byte (self, offset, val, error);
}

static gboolean
fu_fresco_pd_device_and_byte (FuFrescoPdDevice *self,
			      guint16 offset,
			      guint8 val,
			      GError **error)
{
	guint8 buf = 0xff;
	if (!fu_fresco_pd_device_read_byte (self, offset, &buf, error))
		return FALSE;
	buf &= val;
	return fu_fresco_pd_device_write_byte (self, offset, buf, error);
}

static gboolean
fu_fresco_pd_device_or_byte (FuFrescoPdDevice *self,
			     guint16 offset,
			     guint8 val,
			     GError **error)
{
	guint8 buf;
	if (!fu_fresco_pd_device_read_byte (self, offset, &buf, error))
		return FALSE;
	buf |= val;
	return fu_fresco_pd_device_write_byte (self, offset, buf, error);
}

static gboolean
fu_fresco_pd_device_setup (FuDevice *device, GError **error)
{
	FuFrescoPdDevice *self = FU_FRESCO_PD_DEVICE (device);
	FuUsbDevice *usb_device = FU_USB_DEVICE (device);
	guint8 ver[4] = { 0x0 };
	g_autofree gchar *instance_id = NULL;
	g_autofree gchar *version = NULL;

	/* read existing device version */
	for (guint i = 0; i < 4; i++) {
		if (!fu_fresco_pd_device_transfer_read (self, 0x3000 + i, &ver[i], 1, error)) {
			g_prefix_error (error, "failed to read device version [%u]: ", i);
			return FALSE;
		}
	}
	version = fu_fresco_pd_version_from_buf (ver);
	fu_device_set_version (FU_DEVICE (self), version, FWUPD_VERSION_FORMAT_QUAD);

	/* get customer ID */
	self->customer_id = ver[1];
	instance_id = g_strdup_printf ("USB\\VID_%04X&PID_%04X&CID_%02X",
				       fu_usb_device_get_vid (usb_device),
				       fu_usb_device_get_pid (usb_device),
				       self->customer_id);
	fu_device_add_instance_id (device, instance_id);

	/* success */
	return TRUE;
}

static FuFirmware *
fu_fresco_pd_device_prepare_firmware (FuDevice *device,
				      GBytes *fw,
				      FwupdInstallFlags flags,
				      GError **error)
{
	FuFrescoPdDevice *self = FU_FRESCO_PD_DEVICE (device);
	guint8 customer_id;
	g_autoptr(FuFirmware) firmware = fu_fresco_pd_firmware_new ();

	/* check size */
	if (g_bytes_get_size (fw) < fu_device_get_firmware_size_min (device)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "firmware too small, got 0x%x, expected >= 0x%x",
			     (guint) g_bytes_get_size (fw),
			     (guint) fu_device_get_firmware_size_min (device));
		return NULL;
	}

	/* check firmware is suitable */
	fu_device_set_status (device, FWUPD_STATUS_DECOMPRESSING);
	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;
	customer_id = fu_fresco_pd_firmware_get_customer_id (FU_FRESCO_PD_FIRMWARE (firmware));
	if (customer_id != self->customer_id) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "device is incompatible with firmware x.%u.x.x",
			     customer_id);
		return NULL;
	}
	return g_steal_pointer (&firmware);
}

static gboolean
fu_fresco_pd_device_panther_reset_device (FuFrescoPdDevice *self, GError **error)
{
	g_autoptr(GError) error_local = NULL;

	g_debug ("resetting target device");
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_RESTART);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	/* ignore when the device reset before completing the transaction */
	if (!fu_fresco_pd_device_or_byte (self, 0xA003, 1 << 3, &error_local)) {
		if (g_error_matches (error_local,
				     G_USB_DEVICE_ERROR,
				     G_USB_DEVICE_ERROR_FAILED)) {
			g_debug ("ignoring %s", error_local->message);
			return TRUE;
		}
		g_propagate_prefixed_error (error, g_steal_pointer (&error_local),
					    "failed to reset device [%i]",
					    error_local->code);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_fresco_pd_device_write_firmware (FuDevice *device,
				    FuFirmware *firmware,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuFrescoPdDevice *self = FU_FRESCO_PD_DEVICE (device);
	const guint8 *buf;
	gsize bufsz = 0x0;
	guint16 begin_addr = 0x6420;
	guint8 config[3] = { 0x0 };
	guint8 start_symbols[2] = { 0x0 };
	g_autoptr(GBytes) fw = NULL;

	/* get default blob, which we know is already bigger than FirmwareMin */
	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;
	buf = g_bytes_get_data (fw, &bufsz);

	/* get start symbols, and be slightly paranoid */
	if (!fu_memcpy_safe (start_symbols, sizeof(start_symbols), 0x0,	/* dst */
			     buf, bufsz, 0x4000,			/* src */
			     sizeof(start_symbols), error))
		return FALSE;

	/* 0xA001<bit 2> = b'0
	 * 0x6C00<bit 1> = b'0
	 * 0x6C04 = 0x08 */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_BUSY);
	g_debug ("disable MCU, and enable mtp write");
	if (!fu_fresco_pd_device_and_byte (self, 0xa001, ~(1 << 2), error)) {
		g_prefix_error (error, "failed to disable MCU bit 2: ");
		return FALSE;
	}
	if (!fu_fresco_pd_device_and_byte (self, 0x6c00, ~(1 << 1), error)) {
		g_prefix_error (error, "failed to disable MCU bit 1: ");
		return FALSE;
	}
	if (!fu_fresco_pd_device_write_byte (self, 0x6c04, 0x08, error)) {
		g_prefix_error (error, "failed to disable MCU: ");
		return FALSE;
	}

	/* fill safe code in the boot code */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	for (guint16 i = 0; i < 0x400; i += 3) {
		for (guint j = 0; j < 3; j++) {
			if (!fu_fresco_pd_device_read_byte (self,
							    begin_addr + i + j,
							    &config[j],
							    error)) {
				g_prefix_error (error, "failed to read config byte %u: ", j);
				return FALSE;
			}
		}
		if (config[0] == start_symbols[0] &&
		    config[1] == start_symbols[1]) {
			begin_addr = 0x6420 + i;
			break;
		}
		if (config[0] == 0 && config[1] == 0 && config[2] == 0)
			break;
	}
	g_debug ("begin_addr: 0x%04x", begin_addr);
	for (guint16 i = begin_addr + 3; i < begin_addr + 0x400; i += 3) {
		for (guint j = 0; j < 3; j++) {
			if (!fu_fresco_pd_device_read_byte (self, i + j, &config[j], error)) {
				g_prefix_error (error, "failed to read config byte %u: ", j);
				return FALSE;
			}
		}
		if (config[0] == 0x74 && config[1] == 0x06 && config[2] != 0x22) {
			if (!fu_fresco_pd_device_write_byte (self, i + 2, 0x22, error))
				return FALSE;
		} else if (config[0] == 0x6c && config[1] == 0x00 && config[2] != 0x01) {
			if (!fu_fresco_pd_device_write_byte (self, i + 2, 0x01, error))
				return FALSE;
		} else if (config[0] == 0x00 && config[1] == 0x00 && config[2] != 0x00)
			break;
	}

	/* copy buf offset [0 - 0x3FFFF] to mmio address [0x2000 - 0x5FFF] */
	g_debug ("fill firmware body");
	for (guint16 byte_index = 0; byte_index < 0x4000; byte_index++) {
		if (!fu_fresco_pd_device_set_byte (self, byte_index + 0x2000, buf[byte_index], error))
			return FALSE;
		fu_device_set_progress_full (device, (gsize) byte_index, 0x4000);
	}

	/* write file buf 0x4200 ~ 0x4205, 6 bytes to internal address 0x6600 ~ 0x6605
	 * write file buf 0x4210 ~ 0x4215, 6 bytes to internal address 0x6610 ~ 0x6615
	 * write file buf 0x4220 ~ 0x4225, 6 bytes to internal address 0x6620 ~ 0x6625
	 * write file buf 0x4230, 1 byte, to internal address 0x6630 */
	g_debug ("update customize data");
	for (guint16 byte_index = 0; byte_index < 6; byte_index++) {
		if (!fu_fresco_pd_device_set_byte (self,
						   0x6600 + byte_index,
						   buf[0x4200 + byte_index],
						   error))
			return FALSE;
		if (!fu_fresco_pd_device_set_byte (self,
						   0x6610 + byte_index,
						   buf[0x4210 + byte_index],
						   error))
			return FALSE;
		if (!fu_fresco_pd_device_set_byte (self,
						   0x6620 + byte_index,
						   buf[0x4220 + byte_index],
						   error))
			return FALSE;
	}
	if (!fu_fresco_pd_device_set_byte (self, 0x6630, buf[0x4230], error))
		return FALSE;

	/* overwrite firmware file's boot code area (0x4020 ~ 0x41ff) to the area on the device marked by begin_addr
	 * example: if the begin_addr = 0x6420, then copy file buf [0x4020 ~ 0x41ff] to device offset[0x6420 ~ 0x65ff] */
	g_debug ("write boot configuration area");
	for (guint16 byte_index = 0; byte_index < 0x1e0; byte_index += 3) {
		if (!fu_fresco_pd_device_set_byte (self,
						   begin_addr + byte_index + 0,
						   buf[0x4020 + byte_index],
						   error))
			return FALSE;
		if (!fu_fresco_pd_device_set_byte (self,
						   begin_addr + byte_index + 1,
						   buf[0x4021 + byte_index],
						   error))
			return FALSE;
		if (!fu_fresco_pd_device_set_byte (self,
						   begin_addr + byte_index + 2,
						   buf[0x4022 + byte_index],
						   error))
			return FALSE;
	}

	/* reset the device */
	return fu_fresco_pd_device_panther_reset_device (self, error);
}

static void
fu_fresco_pd_device_init (FuFrescoPdDevice *self)
{
	fu_device_add_icon (FU_DEVICE (self), "audio-card");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_protocol (FU_DEVICE (self), "com.frescologic.pd");
	fu_device_set_install_duration (FU_DEVICE (self), 15);
	fu_device_set_remove_delay (FU_DEVICE (self), 20000);
	fu_device_set_firmware_size (FU_DEVICE (self), 0x4400);
}

static void
fu_fresco_pd_device_class_init (FuFrescoPdDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->to_string = fu_fresco_pd_device_to_string;
	klass_device->setup = fu_fresco_pd_device_setup;
	klass_device->write_firmware = fu_fresco_pd_device_write_firmware;
	klass_device->prepare_firmware = fu_fresco_pd_device_prepare_firmware;
}
