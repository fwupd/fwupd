/*
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-chunk.h"
#include "fu-colorhug-common.h"
#include "fu-colorhug-device.h"

/**
 * FU_COLORHUG_DEVICE_FLAG_HALFSIZE:
 *
 * Some devices have a compact memory layout and the application code starts
 * earlier.
 *
 * Since: 1.0.3
 */
#define FU_COLORHUG_DEVICE_FLAG_HALFSIZE	"halfsize"

struct _FuColorhugDevice {
	FuUsbDevice		 parent_instance;
	guint16			 start_addr;
};

G_DEFINE_TYPE (FuColorhugDevice, fu_colorhug_device, FU_TYPE_USB_DEVICE)

#define CH_CMD_GET_FIRMWARE_VERSION		0x07
#define CH_CMD_RESET				0x24
#define CH_CMD_READ_FLASH			0x25
#define CH_CMD_WRITE_FLASH			0x26
#define CH_CMD_BOOT_FLASH			0x27
#define CH_CMD_SET_FLASH_SUCCESS		0x28
#define CH_CMD_ERASE_FLASH			0x29

#define CH_USB_HID_EP				0x0001
#define CH_USB_HID_EP_IN			(CH_USB_HID_EP | 0x80)
#define CH_USB_HID_EP_OUT			(CH_USB_HID_EP | 0x00)
#define CH_USB_HID_EP_SIZE			64
#define CH_USB_CONFIG				0x0001
#define CH_USB_INTERFACE			0x0000
#define CH_EEPROM_ADDR_RUNCODE			0x4000
#define CH_EEPROM_ADDR_RUNCODE_ALS		0x2000

#define CH_DEVICE_USB_TIMEOUT			5000 /* ms */
#define CH_FLASH_TRANSFER_BLOCK_SIZE		0x020	/* 32 */

static gboolean
fu_colorhug_device_msg (FuColorhugDevice *self, guint8 cmd,
			guint8 *ibuf, gsize ibufsz,
			guint8 *obuf, gsize obufsz,
			GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	guint8 buf[] = { [0] = cmd, [1 ... CH_USB_HID_EP_SIZE - 1] = 0x00 };
	gsize actual_length = 0;

	/* check size */
	if (ibufsz > sizeof(buf) - 1) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "cannot process chunk of size %" G_GSIZE_FORMAT,
			     ibufsz);
		return FALSE;
	}
	if (obufsz > sizeof(buf) - 2) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "cannot process chunk of size %" G_GSIZE_FORMAT,
			     ibufsz);
		return FALSE;
	}

	/* optionally copy in data */
	if (ibuf != NULL) {
		if (!fu_memcpy_safe (buf, sizeof(buf), 0x1,	/* dst */
				     ibuf, ibufsz, 0x0,		/* src */
				     ibufsz, error))
			return FALSE;
	}

	/* request */
	if (g_getenv ("FWUPD_COLORHUG_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "REQ", buf, ibufsz + 1);
	if (!g_usb_device_interrupt_transfer (usb_device,
					      CH_USB_HID_EP_OUT,
					      buf,
					      sizeof(buf),
					      &actual_length,
					      CH_DEVICE_USB_TIMEOUT,
					      NULL, /* cancellable */
					      error)) {
		g_prefix_error (error, "failed to send request: ");
		return FALSE;
	}
	if (actual_length != CH_USB_HID_EP_SIZE) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "request not all sent, got %" G_GSIZE_FORMAT,
			     actual_length);
		return FALSE;
	}

	/* read reply */
	if (!g_usb_device_interrupt_transfer (usb_device,
					      CH_USB_HID_EP_IN,
					      buf,
					      sizeof(buf),
					      &actual_length,
					      CH_DEVICE_USB_TIMEOUT,
					      NULL, /* cancellable */
					      error)) {
		g_prefix_error (error, "failed to get reply: ");
		return FALSE;
	}
	if (g_getenv ("FWUPD_COLORHUG_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "RES", buf, actual_length);

	/* old bootloaders do not return the full block */
	if (actual_length != CH_USB_HID_EP_SIZE &&
	    actual_length != 2 &&
	    actual_length != obufsz + 2) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "request not all received, got %" G_GSIZE_FORMAT,
			     actual_length);
		return FALSE;
	}

	/* check error code */
	if (buf[0] != CH_ERROR_NONE) {
		const gchar *msg = ch_strerror (buf[0]);
		if (msg == NULL)
			msg = "unknown error";
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     msg);
		return FALSE;
	}

	/* check cmd matches */
	if (buf[1] != cmd) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "cmd incorrect, expected %u, got %u",
			     cmd, buf[1]);
		return FALSE;
	}

	/* copy back optional buf */
	if (obuf != NULL) {
		if (!fu_memcpy_safe (obuf, obufsz, 0x0,		/* dst */
				     buf, sizeof(buf), 0x2,	/* src */
				     obufsz, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_colorhug_device_detach (FuDevice *device, GError **error)
{
	FuColorhugDevice *self = FU_COLORHUG_DEVICE (device);
	g_autoptr(GError) error_local = NULL;

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	if (!fu_colorhug_device_msg (self, CH_CMD_RESET,
				     NULL, 0, /* in */
				     NULL, 0, /* out */
				     &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "failed to reset device: %s",
			     error_local->message);
		return FALSE;
	}
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_colorhug_device_attach (FuDevice *device, GError **error)
{
	FuColorhugDevice *self = FU_COLORHUG_DEVICE (device);
	g_autoptr(GError) error_local = NULL;

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	if (!fu_colorhug_device_msg (self, CH_CMD_BOOT_FLASH,
				     NULL, 0, /* in */
				     NULL, 0, /* out */
				     &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "failed to boot to runtime: %s",
			     error_local->message);
		return FALSE;
	}
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_colorhug_device_set_flash_success (FuColorhugDevice *self,
				      gboolean val,
				      GError **error)
{
	guint8 buf[] = { [0] = val ? 0x01 : 0x00 };
	g_autoptr(GError) error_local = NULL;

	g_debug ("setting flash success");
	if (!fu_colorhug_device_msg (self, CH_CMD_SET_FLASH_SUCCESS,
				     buf, sizeof(buf), /* in */
				     NULL, 0, /* out */
				     &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "failed to set flash success: %s",
			     error_local->message);
		return FALSE;
	}
	return TRUE;
}


static gboolean
fu_colorhug_device_reload (FuDevice *device, GError **error)
{
	FuColorhugDevice *self = FU_COLORHUG_DEVICE (device);
	return fu_colorhug_device_set_flash_success (self, TRUE, error);
}

static gboolean
fu_colorhug_device_erase (FuColorhugDevice *self, guint16 addr, gsize sz, GError **error)
{
	guint8 buf[4];
	g_autoptr(GError) error_local = NULL;

	fu_common_write_uint16 (buf + 0, addr, G_LITTLE_ENDIAN);
	fu_common_write_uint16 (buf + 2, sz, G_LITTLE_ENDIAN);
	if (!fu_colorhug_device_msg (self, CH_CMD_ERASE_FLASH,
				     buf, sizeof(buf), /* in */
				     NULL, 0, /* out */
				     &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "failed to erase device: %s",
			     error_local->message);
		return FALSE;
	}
	return TRUE;
}

static gchar *
fu_colorhug_device_get_version (FuColorhugDevice *self, GError **error)
{
	guint8 buf[6];
	if (!fu_colorhug_device_msg (self, CH_CMD_GET_FIRMWARE_VERSION,
				     NULL, 0, /* in */
				     buf, sizeof(buf), /* out */
				     error)) {
		return NULL;
	}
	return g_strdup_printf ("%i.%i.%i",
				fu_common_read_uint16 (buf + 0, G_LITTLE_ENDIAN),
				fu_common_read_uint16 (buf + 2, G_LITTLE_ENDIAN),
				fu_common_read_uint16 (buf + 4, G_LITTLE_ENDIAN));
}

static gboolean
fu_colorhug_device_probe (FuUsbDevice *device, GError **error)
{
	FuColorhugDevice *self = FU_COLORHUG_DEVICE (device);

	/* compact memory layout */
	if (fu_device_has_custom_flag (FU_DEVICE (device),
				       FU_COLORHUG_DEVICE_FLAG_HALFSIZE))
		self->start_addr = CH_EEPROM_ADDR_RUNCODE_ALS;

	/* add hardcoded bits */
	fu_device_add_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_UPDATABLE);

	/* success */
	return TRUE;
}

static gboolean
fu_colorhug_device_open (FuUsbDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);

	/* got the version using the HID API */
	if (!g_usb_device_set_configuration (usb_device, CH_USB_CONFIG, error))
		return FALSE;
	if (!g_usb_device_claim_interface (usb_device, CH_USB_INTERFACE,
					   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					   error)) {
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_colorhug_device_setup (FuDevice *device, GError **error)
{
	FuColorhugDevice *self = FU_COLORHUG_DEVICE (device);

	if (fu_device_get_version (FU_DEVICE (device)) == NULL) {
		g_autofree gchar *version = NULL;
		g_autoptr(GError) error_local = NULL;
		version = fu_colorhug_device_get_version (self, &error_local);
		if (version != NULL) {
			g_debug ("obtained fwver using API '%s'", version);
			fu_device_set_version (device, version, FWUPD_VERSION_FORMAT_TRIPLET);
		} else {
			g_warning ("failed to get firmware version: %s",
				   error_local->message);
		}
	}

	/* success */
	return TRUE;
}

static guint8
ch_colorhug_device_calculate_checksum (const guint8 *data, guint32 len)
{
	guint8 checksum = 0xff;
	for (guint32 i = 0; i < len; i++)
		checksum ^= data[i];
	return checksum;
}

static gboolean
fu_colorhug_device_write_firmware (FuDevice *device,
				   FuFirmware *firmware,
				   FwupdInstallFlags flags,
				   GError **error)
{
	FuColorhugDevice *self = FU_COLORHUG_DEVICE (device);
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* get default image */
	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	/* build packets */
	chunks = fu_chunk_array_new_from_bytes (fw,
						self->start_addr,
						0x00,	/* page_sz */
						CH_FLASH_TRANSFER_BLOCK_SIZE);

	/* don't auto-boot firmware */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_colorhug_device_set_flash_success (self, FALSE, error))
		return FALSE;

	/* erase flash */
	if (!fu_colorhug_device_erase (self, self->start_addr, g_bytes_get_size (fw), error))
		return FALSE;

	/* write each block */
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		guint8 buf[CH_FLASH_TRANSFER_BLOCK_SIZE+4];
		g_autoptr(GError) error_local = NULL;

		/* set address, length, checksum, data */
		fu_common_write_uint16 (buf + 0, chk->address, G_LITTLE_ENDIAN);
		buf[2] = chk->data_sz;
		buf[3] = ch_colorhug_device_calculate_checksum (chk->data, chk->data_sz);
		if (!fu_memcpy_safe (buf, sizeof(buf), 0x4,		/* dst */
				     chk->data, chk->data_sz, 0x0,	/* src */
				     chk->data_sz, error))
			return FALSE;
		if (!fu_colorhug_device_msg (self, CH_CMD_WRITE_FLASH,
					     buf, sizeof(buf), /* in */
					     NULL, 0, /* out */
					     &error_local)) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_WRITE,
				     "failed to write: %s",
				     error_local->message);
			return FALSE;
		}

		/* update progress */
		fu_device_set_progress_full (device, (gsize) i, (gsize) chunks->len * 2);
	}

	/* verify each block */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_VERIFY);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		guint8 buf[3];
		guint8 buf_out[CH_FLASH_TRANSFER_BLOCK_SIZE+1];
		g_autoptr(GError) error_local = NULL;

		/* set address */
		fu_common_write_uint16 (buf + 0, chk->address, G_LITTLE_ENDIAN);
		buf[2] = chk->data_sz;
		if (!fu_colorhug_device_msg (self, CH_CMD_READ_FLASH,
					     buf, sizeof(buf), /* in */
					     buf_out, sizeof(buf_out), /* out */
					     &error_local)) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_READ,
				     "failed to read: %s",
				     error_local->message);
			return FALSE;
		}

		/* verify */
		if (memcmp (buf_out + 1, chk->data, chk->data_sz) != 0) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_WRITE,
				     "failed to verify firmware for chunk %u, "
				     "address 0x%0x, length 0x%0x",
				     i, (guint) chk->address, chk->data_sz);
			return FALSE;
		}

		/* update progress */
		fu_device_set_progress_full (device,
					     (gsize) chunks->len + i,
					     (gsize) chunks->len * 2);
	}

	/* success! */
	return TRUE;
}

static void
fu_colorhug_device_init (FuColorhugDevice *self)
{
	/* this is the application code */
	self->start_addr = CH_EEPROM_ADDR_RUNCODE;
	fu_device_set_protocol (FU_DEVICE (self), "com.hughski.colorhug");
	fu_device_set_remove_delay (FU_DEVICE (self),
				    FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
}

static void
fu_colorhug_device_class_init (FuColorhugDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUsbDeviceClass *klass_usb_device = FU_USB_DEVICE_CLASS (klass);
	klass_device->write_firmware = fu_colorhug_device_write_firmware;
	klass_device->attach = fu_colorhug_device_attach;
	klass_device->detach = fu_colorhug_device_detach;
	klass_device->reload = fu_colorhug_device_reload;
	klass_device->setup = fu_colorhug_device_setup;
	klass_usb_device->open = fu_colorhug_device_open;
	klass_usb_device->probe = fu_colorhug_device_probe;
}
