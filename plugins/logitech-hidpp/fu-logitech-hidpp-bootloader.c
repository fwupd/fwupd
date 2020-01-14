/*
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-firmware-common.h"
#include "fu-logitech-hidpp-common.h"
#include "fu-logitech-hidpp-bootloader.h"
#include "fu-logitech-hidpp-hidpp.h"

typedef struct
{
	guint16			 flash_addr_lo;
	guint16			 flash_addr_hi;
	guint16			 flash_blocksize;
} FuLogitechHidPpBootloaderPrivate;

#define FU_UNIFYING_DEVICE_EP1				0x81
#define FU_UNIFYING_DEVICE_EP3				0x83

G_DEFINE_TYPE_WITH_PRIVATE (FuLogitechHidPpBootloader, fu_logitech_hidpp_bootloader, FU_TYPE_USB_DEVICE)

#define GET_PRIVATE(o) (fu_logitech_hidpp_bootloader_get_instance_private (o))

static void
fu_logitech_hidpp_bootloader_to_string (FuDevice *device, guint idt, GString *str)
{
	FuLogitechHidPpBootloader *self = FU_UNIFYING_BOOTLOADER (device);
	FuLogitechHidPpBootloaderPrivate *priv = GET_PRIVATE (self);
	fu_common_string_append_kx (str, idt, "FlashAddrHigh", priv->flash_addr_hi);
	fu_common_string_append_kx (str, idt, "FlashAddrLow", priv->flash_addr_lo);
	fu_common_string_append_kx (str, idt, "FlashBlockSize", priv->flash_blocksize);
}

FuLogitechHidPpBootloaderRequest *
fu_logitech_hidpp_bootloader_request_new (void)
{
	FuLogitechHidPpBootloaderRequest *req = g_new0 (FuLogitechHidPpBootloaderRequest, 1);
	return req;
}

GPtrArray *
fu_logitech_hidpp_bootloader_parse_requests (FuLogitechHidPpBootloader *self, GBytes *fw, GError **error)
{
	const gchar *tmp;
	g_auto(GStrv) lines = NULL;
	g_autoptr(GPtrArray) reqs = NULL;
	guint32 last_addr = 0;

	reqs = g_ptr_array_new_with_free_func (g_free);
	tmp = g_bytes_get_data (fw, NULL);
	lines = g_strsplit_set (tmp, "\n\r", -1);
	for (guint i = 0; lines[i] != NULL; i++) {
		g_autoptr(FuLogitechHidPpBootloaderRequest) payload = NULL;
		guint8 rec_type = 0x00;
		guint16 offset = 0x0000;
		gboolean exit = FALSE;

		/* skip empty lines */
		tmp = lines[i];
		if (strlen (tmp) < 5)
			continue;

		payload = fu_logitech_hidpp_bootloader_request_new ();
		payload->len = fu_logitech_hidpp_buffer_read_uint8 (tmp + 0x01);
		if (payload->len > 28) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "firmware data invalid: too large %u bytes",
				     payload->len);
			return NULL;
		}
		payload->addr = fu_firmware_strparse_uint16 (tmp + 0x03);
		payload->cmd = FU_UNIFYING_BOOTLOADER_CMD_WRITE_RAM_BUFFER;

		rec_type = fu_logitech_hidpp_buffer_read_uint8 (tmp + 0x07);

		switch (rec_type) {
			case 0x00: /* data */
				break;
			case 0x01: /* EOF */
				exit = TRUE;
				break;
			case 0x03: /* start segment address */
				/* this is used to specify the start address,
				it is doesn't matter in this context so we can
				safely ignore it */
				continue;
			case 0x04: /* extended linear address */
				offset = fu_firmware_strparse_uint16 (tmp + 0x09);
				if (offset != 0x0000) {
					g_set_error (error,
						     G_IO_ERROR,
						     G_IO_ERROR_INVALID_DATA,
						     "extended linear addresses with offset different from 0 are not supported");
					return NULL;
				}
				continue;
			case 0x05: /* start linear address */
				/* this is used to specify the start address,
				it is doesn't matter in this context so we can
				safely ignore it */
				continue;
			case 0xFD: /* custom - vendor */
				/* record type of 0xFD indicates signature data */
				payload->cmd = FU_UNIFYING_BOOTLOADER_CMD_WRITE_SIGNATURE;
				break;
			default:
				g_set_error (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "intel hex file record type %02x not supported",
					     rec_type);
				return NULL;
		}

		if (exit)
			break;

		/* read the data, but skip the checksum byte */
		for (guint j = 0; j < payload->len; j++) {
			const gchar *ptr = tmp + 0x09 + (j * 2);
			if (ptr[0] == '\0') {
				g_set_error (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "firmware data invalid: expected %u bytes",
					     payload->len);
				return NULL;
			}
			payload->data[j] = fu_logitech_hidpp_buffer_read_uint8 (ptr);
		}

		/* no need to bound check signature addresses */
		if (payload->cmd == FU_UNIFYING_BOOTLOADER_CMD_WRITE_SIGNATURE) {
			g_ptr_array_add (reqs, g_steal_pointer (&payload));
			continue;
		}

		/* skip the bootloader */
		if (payload->addr > fu_logitech_hidpp_bootloader_get_addr_hi (self)) {
			g_debug ("skipping write @ %04x", payload->addr);
			continue;
		}

		/* skip the header */
		if (payload->addr < fu_logitech_hidpp_bootloader_get_addr_lo (self)) {
			g_debug ("skipping write @ %04x", payload->addr);
			continue;
		}

		/* make sure firmware addresses only go up */
		if (payload->addr < last_addr) {
			g_debug ("skipping write @ %04x", payload->addr);
			continue;
		}
		last_addr = payload->addr;

		/* pending */
		g_ptr_array_add (reqs, g_steal_pointer (&payload));
	}
	if (reqs->len == 0) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "firmware data invalid: no payloads found");
		return NULL;
	}
	return g_steal_pointer (&reqs);
}

guint16
fu_logitech_hidpp_bootloader_get_addr_lo (FuLogitechHidPpBootloader *self)
{
	FuLogitechHidPpBootloaderPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_UNIFYING_BOOTLOADER (self), 0x0000);
	return priv->flash_addr_lo;
}

guint16
fu_logitech_hidpp_bootloader_get_addr_hi (FuLogitechHidPpBootloader *self)
{
	FuLogitechHidPpBootloaderPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_UNIFYING_BOOTLOADER (self), 0x0000);
	return priv->flash_addr_hi;
}

guint16
fu_logitech_hidpp_bootloader_get_blocksize (FuLogitechHidPpBootloader *self)
{
	FuLogitechHidPpBootloaderPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_UNIFYING_BOOTLOADER (self), 0x0000);
	return priv->flash_blocksize;
}

static gboolean
fu_logitech_hidpp_bootloader_attach (FuDevice *device, GError **error)
{
	FuLogitechHidPpBootloader *self = FU_UNIFYING_BOOTLOADER (device);
	g_autoptr(FuLogitechHidPpBootloaderRequest) req = fu_logitech_hidpp_bootloader_request_new ();
	req->cmd = FU_UNIFYING_BOOTLOADER_CMD_REBOOT;
	if (!fu_logitech_hidpp_bootloader_request (self, req, error)) {
		g_prefix_error (error, "failed to attach back to runtime: ");
		return FALSE;
	}
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_logitech_hidpp_bootloader_set_bl_version (FuLogitechHidPpBootloader *self, GError **error)
{
	guint16 build;
	g_autofree gchar *version = NULL;
	g_autoptr(FuLogitechHidPpBootloaderRequest) req = fu_logitech_hidpp_bootloader_request_new ();

	/* call into hardware */
	req->cmd = FU_UNIFYING_BOOTLOADER_CMD_GET_BL_VERSION;
	if (!fu_logitech_hidpp_bootloader_request (self, req, error)) {
		g_prefix_error (error, "failed to get firmware version: ");
		return FALSE;
	}

	/* BOTxx.yy_Bzzzz
	 * 012345678901234 */
	build = (guint16) fu_logitech_hidpp_buffer_read_uint8 ((const gchar *) req->data + 10) << 8;
	build += fu_logitech_hidpp_buffer_read_uint8 ((const gchar *) req->data + 12);
	version = fu_logitech_hidpp_format_version ("BOT",
			fu_logitech_hidpp_buffer_read_uint8 ((const gchar *) req->data + 3),
			fu_logitech_hidpp_buffer_read_uint8 ((const gchar *) req->data + 6),
			build);
	if (version == NULL) {
		g_prefix_error (error, "failed to format firmware version: ");
		return FALSE;
	}
	fu_device_set_version_bootloader (FU_DEVICE (self), version);
	return TRUE;
}

static gboolean
fu_logitech_hidpp_bootloader_open (FuUsbDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);
	const guint idx = 0x00;

	/* claim the only interface */
	if (!g_usb_device_claim_interface (usb_device, idx,
					   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					   error)) {
		g_prefix_error (error, "Failed to claim 0x%02x: ", idx);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_bootloader_setup (FuDevice *device, GError **error)
{
	FuLogitechHidPpBootloaderClass *klass = FU_UNIFYING_BOOTLOADER_GET_CLASS (device);
	FuLogitechHidPpBootloader *self = FU_UNIFYING_BOOTLOADER (device);
	FuLogitechHidPpBootloaderPrivate *priv = GET_PRIVATE (self);
	g_autoptr(FuLogitechHidPpBootloaderRequest) req = fu_logitech_hidpp_bootloader_request_new ();

	/* get memory map */
	req->cmd = FU_UNIFYING_BOOTLOADER_CMD_GET_MEMINFO;
	if (!fu_logitech_hidpp_bootloader_request (self, req, error)) {
		g_prefix_error (error, "failed to get meminfo: ");
		return FALSE;
	}
	if (req->len != 0x06) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to get meminfo: invalid size %02x",
			     req->len);
		return FALSE;
	}

	/* parse values */
	priv->flash_addr_lo = fu_common_read_uint16 (req->data + 0, G_BIG_ENDIAN);
	priv->flash_addr_hi = fu_common_read_uint16 (req->data + 2, G_BIG_ENDIAN);
	priv->flash_blocksize = fu_common_read_uint16 (req->data + 4, G_BIG_ENDIAN);

	/* get bootloader version */
	if (!fu_logitech_hidpp_bootloader_set_bl_version (self, error))
		return FALSE;

	/* subclassed further */
	if (klass->setup != NULL)
		return klass->setup (self, error);

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_bootloader_close (FuUsbDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);
	if (usb_device != NULL) {
		if (!g_usb_device_release_interface (usb_device, 0x00,
						     G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
						     error)) {
			return FALSE;
		}
	}
	return TRUE;
}

gboolean
fu_logitech_hidpp_bootloader_request (FuLogitechHidPpBootloader *self,
				      FuLogitechHidPpBootloaderRequest *req,
				      GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	gsize actual_length = 0;
	guint8 buf_request[32];
	guint8 buf_response[32];

	/* build packet */
	memset (buf_request, 0x00, sizeof (buf_request));
	buf_request[0x00] = req->cmd;
	buf_request[0x01] = req->addr >> 8;
	buf_request[0x02] = req->addr & 0xff;
	buf_request[0x03] = req->len;
	if (!fu_memcpy_safe (buf_request, sizeof(buf_request), 0x04,	/* dst */
			     req->data, sizeof(req->data), 0x0,		/* src */
			     sizeof(req->data), error))
		return FALSE;

	/* send request */
	if (g_getenv ("FWUPD_UNIFYING_VERBOSE") != NULL) {
		fu_common_dump_raw (G_LOG_DOMAIN, "host->device",
				    buf_request, sizeof (buf_request));
	}
	if (usb_device != NULL) {
		if (!g_usb_device_control_transfer (usb_device,
						    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
						    G_USB_DEVICE_REQUEST_TYPE_CLASS,
						    G_USB_DEVICE_RECIPIENT_INTERFACE,
						    FU_HID_REPORT_SET,
						    0x0200, 0x0000,
						    buf_request,
						    sizeof (buf_request),
						    &actual_length,
						    FU_UNIFYING_DEVICE_TIMEOUT_MS,
						    NULL,
						    error)) {
			g_prefix_error (error, "failed to send data: ");
			return FALSE;
		}
	}

	/* no response required when rebooting */
	if (usb_device != NULL &&
	    req->cmd == FU_UNIFYING_BOOTLOADER_CMD_REBOOT) {
		g_autoptr(GError) error_ignore = NULL;
		if (!g_usb_device_interrupt_transfer (usb_device,
						      FU_UNIFYING_DEVICE_EP1,
						      buf_response,
						      sizeof (buf_response),
						      &actual_length,
						      FU_UNIFYING_DEVICE_TIMEOUT_MS,
						      NULL,
						      &error_ignore)) {
			g_debug ("ignoring: %s", error_ignore->message);
		} else {
			if (g_getenv ("FWUPD_UNIFYING_VERBOSE") != NULL) {
				fu_common_dump_raw (G_LOG_DOMAIN, "device->host",
						    buf_response, actual_length);
			}
		}
		return TRUE;
	}

	/* get response */
	memset (buf_response, 0x00, sizeof (buf_response));
	if (usb_device != NULL) {
		if (!g_usb_device_interrupt_transfer (usb_device,
						      FU_UNIFYING_DEVICE_EP1,
						      buf_response,
						      sizeof (buf_response),
						      &actual_length,
						      FU_UNIFYING_DEVICE_TIMEOUT_MS,
						      NULL,
						      error)) {
			g_prefix_error (error, "failed to get data: ");
			return FALSE;
		}
	} else {
		/* emulated */
		buf_response[0] = buf_request[0];
		if (buf_response[0] == FU_UNIFYING_BOOTLOADER_CMD_GET_MEMINFO) {
			buf_response[3] = 0x06; /* len */
			buf_response[4] = 0x40; /* lo MSB */
			buf_response[5] = 0x00; /* lo LSB */
			buf_response[6] = 0x6b; /* hi MSB */
			buf_response[7] = 0xff; /* hi LSB */
			buf_response[8] = 0x00; /* bs MSB */
			buf_response[9] = 0x80; /* bs LSB */
		}
		actual_length = sizeof (buf_response);
	}
	if (g_getenv ("FWUPD_UNIFYING_VERBOSE") != NULL) {
		fu_common_dump_raw (G_LOG_DOMAIN, "device->host",
				    buf_response, actual_length);
	}

	/* parse response */
	if ((buf_response[0x00] & 0xf0) != req->cmd) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "invalid command response of %02x, expected %02x",
			     buf_response[0x00], req->cmd);
		return FALSE;
	}
	req->cmd = buf_response[0x00];
	req->addr = ((guint16) buf_response[0x01] << 8) + buf_response[0x02];
	req->len = buf_response[0x03];
	if (req->len > 28) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "invalid data size of %02x", req->len);
		return FALSE;
	}
	memset (req->data, 0x00, 28);
	if (req->len > 0)
		memcpy (req->data, buf_response + 0x04, req->len);
	return TRUE;
}

static void
fu_logitech_hidpp_bootloader_init (FuLogitechHidPpBootloader *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	fu_device_add_icon (FU_DEVICE (self), "preferences-desktop-keyboard");
	fu_device_set_name (FU_DEVICE (self), "Unifying Receiver");
	fu_device_set_summary (FU_DEVICE (self), "A miniaturised USB wireless receiver (bootloader)");
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_remove_delay (FU_DEVICE (self), FU_UNIFYING_DEVICE_TIMEOUT_MS);
}

static void
fu_logitech_hidpp_bootloader_class_init (FuLogitechHidPpBootloaderClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUsbDeviceClass *klass_usb_device = FU_USB_DEVICE_CLASS (klass);
	klass_device->to_string = fu_logitech_hidpp_bootloader_to_string;
	klass_device->attach = fu_logitech_hidpp_bootloader_attach;
	klass_device->setup = fu_logitech_hidpp_bootloader_setup;
	klass_usb_device->open = fu_logitech_hidpp_bootloader_open;
	klass_usb_device->close = fu_logitech_hidpp_bootloader_close;
}
