/*
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>
#include <appstream-glib.h>

#include "fu-ebitdo-common.h"
#include "fu-ebitdo-device.h"

struct _FuEbitdoDevice {
	FuUsbDevice		 parent_instance;
	guint32			 serial[9];
};

G_DEFINE_TYPE (FuEbitdoDevice, fu_ebitdo_device, FU_TYPE_USB_DEVICE)

static gboolean
fu_ebitdo_device_send (FuEbitdoDevice *self,
		       FuEbitdoPktType type,
		       FuEbitdoPktCmd subtype,
		       FuEbitdoPktCmd cmd,
		       const guint8 *in,
		       gsize in_len,
		       GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	guint8 packet[FU_EBITDO_USB_EP_SIZE] = {0};
	gsize actual_length;
	guint8 ep_out = FU_EBITDO_USB_RUNTIME_EP_OUT;
	g_autoptr(GError) error_local = NULL;
	FuEbitdoPkt *hdr = (FuEbitdoPkt *) packet;

	/* different */
	if (fu_device_has_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		ep_out = FU_EBITDO_USB_BOOTLOADER_EP_OUT;

	/* check size */
	if (in_len > 64 - 8) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "input buffer too large");
		return FALSE;
	}

	/* packet[0] is the total length of the packet */
	hdr->type = type;
	hdr->subtype = subtype;

	/* do we have a payload */
	if (in_len > 0) {
		hdr->cmd_len = GUINT16_TO_LE (in_len + 3);
		hdr->cmd = cmd;
		hdr->payload_len = GUINT16_TO_LE (in_len);
		memcpy (packet + 0x08, in, in_len);
		hdr->pkt_len = (guint8) (in_len + 7);
	} else {
		hdr->cmd_len = GUINT16_TO_LE (in_len + 1);
		hdr->cmd = cmd;
		hdr->pkt_len = 5;
	}

	/* debug */
	if (g_getenv ("FWUPD_EBITDO_VERBOSE") != NULL) {
		fu_ebitdo_dump_raw ("->DEVICE", packet, (gsize) hdr->pkt_len + 1);
		fu_ebitdo_dump_pkt (hdr);
	}

	/* get data from device */
	if (!g_usb_device_interrupt_transfer (usb_device,
					      ep_out,
					      packet,
					      FU_EBITDO_USB_EP_SIZE,
					      &actual_length,
					      FU_EBITDO_USB_TIMEOUT,
					      NULL, /* cancellable */
					      &error_local)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "failed to send to device on ep 0x%02x: %s",
			     (guint) FU_EBITDO_USB_BOOTLOADER_EP_OUT,
			     error_local->message);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ebitdo_device_receive (FuEbitdoDevice *self,
		       guint8 *out,
		       gsize out_len,
		       GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	guint8 packet[FU_EBITDO_USB_EP_SIZE] = {0};
	gsize actual_length;
	guint8 ep_in = FU_EBITDO_USB_RUNTIME_EP_IN;
	g_autoptr(GError) error_local = NULL;
	FuEbitdoPkt *hdr = (FuEbitdoPkt *) packet;

	/* different */
	if (fu_device_has_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		ep_in = FU_EBITDO_USB_BOOTLOADER_EP_IN;

	/* get data from device */
	if (!g_usb_device_interrupt_transfer (usb_device,
					      ep_in,
					      packet,
					      FU_EBITDO_USB_EP_SIZE,
					      &actual_length,
					      FU_EBITDO_USB_TIMEOUT,
					      NULL, /* cancellable */
					      &error_local)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "failed to retrieve from device on ep 0x%02x: %s",
			     (guint) FU_EBITDO_USB_BOOTLOADER_EP_IN,
			     error_local->message);
		return FALSE;
	}

	/* debug */
	if (g_getenv ("FWUPD_EBITDO_VERBOSE") != NULL) {
		fu_ebitdo_dump_raw ("<-DEVICE", packet, actual_length);
		fu_ebitdo_dump_pkt (hdr);
	}

	/* get-version (booloader) */
	if (hdr->type == FU_EBITDO_PKT_TYPE_USER_CMD &&
	    hdr->subtype == FU_EBITDO_PKT_CMD_UPDATE_FIRMWARE_DATA &&
	    hdr->cmd == FU_EBITDO_PKT_CMD_FW_GET_VERSION) {
		if (out != NULL) {
			if (hdr->payload_len != out_len) {
				g_set_error (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "outbuf size wrong, expected %" G_GSIZE_FORMAT " got %u",
					     out_len,
					     hdr->payload_len);
				return FALSE;
			}
			memcpy (out,
				packet + sizeof(FuEbitdoPkt),
				hdr->payload_len);
		}
		return TRUE;
	}

	/* get-version (firmware) -- not a packet, just raw data! */
	if (hdr->pkt_len == FU_EBITDO_PKT_CMD_GET_VERSION_RESPONSE) {
		if (out != NULL) {
			if (out_len != 4) {
				g_set_error (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "outbuf size wrong, expected 4 got %" G_GSIZE_FORMAT,
					     out_len);
				return FALSE;
			}
			memcpy (out, packet + 1, 4);
		}
		return TRUE;
	}

	/* verification-id response */
	if (hdr->type == FU_EBITDO_PKT_TYPE_USER_CMD &&
	    hdr->subtype == FU_EBITDO_PKT_CMD_VERIFICATION_ID) {
		if (out != NULL) {
			if (hdr->cmd_len != out_len) {
				g_set_error (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "outbuf size wrong, expected %" G_GSIZE_FORMAT " got %i",
					     out_len,
					     hdr->cmd_len);
				return FALSE;
			}
			memcpy (out,
				packet + sizeof(FuEbitdoPkt) - 3,
				hdr->cmd_len);
		}
		return TRUE;
	}

	/* update-firmware-data */
	if (hdr->type == FU_EBITDO_PKT_TYPE_USER_CMD &&
	    hdr->subtype == FU_EBITDO_PKT_CMD_UPDATE_FIRMWARE_DATA &&
	    hdr->payload_len == 0x00) {
		if (hdr->cmd != FU_EBITDO_PKT_CMD_ACK) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "write failed, got %s",
				     fu_ebitdo_pkt_cmd_to_string (hdr->cmd));
			return FALSE;
		}
		return TRUE;
	}

	/* unhandled */
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "unexpected device response");
	return FALSE;
}

static void
fu_ebitdo_device_set_version (FuEbitdoDevice *self, guint32 version)
{
	g_autofree gchar *tmp = NULL;
	tmp = g_strdup_printf ("%.2f", version / 100.f);
	fu_device_set_version (FU_DEVICE (self), tmp);
}

static gboolean
fu_ebitdo_device_validate (FuEbitdoDevice *self, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	guint8 idx;
	g_autofree gchar *ven = NULL;
	const gchar *whitelist[] = {
		"8Bitdo",
		"SFC30",
		NULL };

	/* this is a new, always valid, VID */
	if (g_usb_device_get_vid (usb_device) == 0x2dc8)
		return TRUE;

	/* verify the vendor prefix against a whitelist */
	idx = g_usb_device_get_manufacturer_index (usb_device);
	ven = g_usb_device_get_string_descriptor (usb_device, idx, error);
	if (ven == NULL) {
		g_prefix_error (error, "could not check vendor descriptor: ");
		return FALSE;
	}
	for (guint i = 0; whitelist[i] != NULL; i++) {
		if (g_str_has_prefix (ven, whitelist[i]))
			return TRUE;
	}
	g_set_error (error,
		     G_IO_ERROR,
		     G_IO_ERROR_INVALID_DATA,
		     "vendor '%s' did not match whitelist, "
		     "probably not a 8Bitdo device…", ven);
	return FALSE;
}

static gboolean
fu_ebitdo_device_open (FuUsbDevice *device, GError **error)
{
	FuEbitdoDevice *self = FU_EBITDO_DEVICE (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);
	gdouble tmp;
	guint32 version_tmp = 0;
	guint32 serial_tmp[9] = {0};

	/* open, then ensure this is actually 8Bitdo hardware */
	if (!fu_ebitdo_device_validate (self, error))
		return FALSE;
	if (!g_usb_device_claim_interface (usb_device, 0, /* 0 = idx? */
					   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					   error)) {
		return FALSE;
	}

	/* in firmware mode */
	if (!fu_device_has_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		if (!fu_ebitdo_device_send (self,
					 FU_EBITDO_PKT_TYPE_USER_CMD,
					 FU_EBITDO_PKT_CMD_GET_VERSION,
					 0,
					 NULL, 0, /* in */
					 error)) {
			return FALSE;
		}
		if (!fu_ebitdo_device_receive (self,
					    (guint8 *) &version_tmp,
					    sizeof(version_tmp),
					    error)) {
			return FALSE;
		}
		tmp = (gdouble) GUINT32_FROM_LE (version_tmp);
		fu_ebitdo_device_set_version (self, tmp);
		return TRUE;
	}

	/* get version */
	if (!fu_ebitdo_device_send (self,
				 FU_EBITDO_PKT_TYPE_USER_CMD,
				 FU_EBITDO_PKT_CMD_UPDATE_FIRMWARE_DATA,
				 FU_EBITDO_PKT_CMD_FW_GET_VERSION,
				 NULL, 0, /* in */
				 error)) {
		return FALSE;
	}
	if (!fu_ebitdo_device_receive (self,
				    (guint8 *) &version_tmp,
				    sizeof(version_tmp),
				    error)) {
		return FALSE;
	}
	tmp = (gdouble) GUINT32_FROM_LE (version_tmp);
	fu_ebitdo_device_set_version (self, tmp);

	/* get verification ID */
	if (!fu_ebitdo_device_send (self,
				 FU_EBITDO_PKT_TYPE_USER_CMD,
				 FU_EBITDO_PKT_CMD_GET_VERIFICATION_ID,
				 0x00, /* cmd */
				 NULL, 0,
				 error)) {
		return FALSE;
	}
	if (!fu_ebitdo_device_receive (self,
				    (guint8 *) &serial_tmp, sizeof(serial_tmp),
				    error)) {
		return FALSE;
	}
	for (guint i = 0; i < 9; i++)
		self->serial[i] = GUINT32_FROM_LE (serial_tmp[i]);

	/* success */
	return TRUE;
}

const guint32 *
fu_ebitdo_device_get_serial (FuEbitdoDevice *self)
{
	return self->serial;
}

static gboolean
fu_ebitdo_device_write_firmware (FuDevice *device, GBytes *fw, GError **error)
{
	FuEbitdoDevice *self = FU_EBITDO_DEVICE (device);
	FuEbitdoFirmwareHeader *hdr;
	const guint8 *payload_data;
	const guint chunk_sz = 32;
	guint32 payload_len;
	guint32 serial_new[3];
	g_autoptr(GError) error_local = NULL;
	const guint32 app_key_index[16] = {
		0x186976e5, 0xcac67acd, 0x38f27fee, 0x0a4948f1,
		0xb75b7753, 0x1f8ffa5c, 0xbff8cf43, 0xc4936167,
		0x92bd03f0, 0x5573c6ed, 0x57d8845b, 0x827197ac,
		0xb91901c9, 0x3917edfe, 0xbcd6344f, 0xcf9e23b5
	};

	/* not in bootloader mode, so print what to do */
	if (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER)) {
		GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
		g_autoptr(GString) msg = g_string_new ("Not in bootloader mode: ");
		g_string_append (msg, "Disconnect the controller, ");
		g_print ("1. \n");
		switch (g_usb_device_get_pid (usb_device)) {
		case 0xab11: /* FC30 */
		case 0xab12: /* NES30 */
		case 0xab21: /* SFC30 */
		case 0xab20: /* SNES30 */
			g_string_append (msg, "hold down L+R+START for 3 seconds until "
					      "both LED lights flashing, ");
			break;
		case 0x9000: /* FC30PRO */
		case 0x9001: /* NES30PRO */
			g_string_append (msg, "hold down RETURN+POWER for 3 seconds until "
					      "both LED lights flashing, ");
			break;
		case 0x1002: /* FC30-ARCADE */
			g_string_append (msg, "hold down L1+R1+HOME for 3 seconds until "
					      "both blue LED and green LED blink, ");
			break;
		case 0x6000: /* SF30 pro: Dinput mode */
		case 0x6001: /* SN30 pro: Dinput mode */
		case 0x028e: /* SF30/SN30 pro: Xinput mode */
			g_string_append (msg, "press and hold L1+R1+START for 3 seconds "
					      "until the LED on top blinks red, ");
			break;
		default:
			g_string_append (msg, "do what it says in the manual, ");
			break;
		}
		g_string_append (msg, "then re-connect controller");
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     msg->str);
		return FALSE;
	}

	/* corrupt */
	if (g_bytes_get_size (fw) < sizeof (FuEbitdoFirmwareHeader)) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "firmware too small for header");
		return FALSE;
	}

	/* print details about the firmware */
	hdr = (FuEbitdoFirmwareHeader *) g_bytes_get_data (fw, NULL);
	fu_ebitdo_dump_firmware_header (hdr);

	/* check the file size */
	payload_len = (guint32) (g_bytes_get_size (fw) - sizeof (FuEbitdoFirmwareHeader));
	if (payload_len != GUINT32_FROM_LE (hdr->destination_len)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "file size incorrect, expected 0x%04x got 0x%04x",
			     (guint) GUINT32_FROM_LE (hdr->destination_len),
			     (guint) payload_len);
		return FALSE;
	}

	/* check if this is firmware */
	for (guint i = 0; i < 4; i++) {
		if (hdr->reserved[i] != 0x0) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "data invalid, reserved[%u] = 0x%04x",
				     i, hdr->reserved[i]);
			return FALSE;
		}
	}

	/* set up the firmware header */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_ebitdo_device_send (self,
				 FU_EBITDO_PKT_TYPE_USER_CMD,
				 FU_EBITDO_PKT_CMD_UPDATE_FIRMWARE_DATA,
				 FU_EBITDO_PKT_CMD_FW_UPDATE_HEADER,
				 (const guint8 *) hdr, sizeof(FuEbitdoFirmwareHeader),
				 &error_local)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "failed to set up firmware header: %s",
			     error_local->message);
		return FALSE;
	}
	if (!fu_ebitdo_device_receive (self, NULL, 0, &error_local)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "failed to get ACK for fw update header: %s",
			     error_local->message);
		return FALSE;
	}

	/* flash the firmware in 32 byte blocks */
	payload_data = g_bytes_get_data (fw, NULL);
	payload_data += sizeof(FuEbitdoFirmwareHeader);
	for (guint32 offset = 0; offset < payload_len; offset += chunk_sz) {
		if (g_getenv ("FWUPD_EBITDO_VERBOSE") != NULL) {
			g_debug ("writing %u bytes to 0x%04x of 0x%04x",
				 chunk_sz, offset, payload_len);
		}
		fu_device_set_progress_full (device, offset, payload_len);
		if (!fu_ebitdo_device_send (self,
					 FU_EBITDO_PKT_TYPE_USER_CMD,
					 FU_EBITDO_PKT_CMD_UPDATE_FIRMWARE_DATA,
					 FU_EBITDO_PKT_CMD_FW_UPDATE_DATA,
					 payload_data + offset, chunk_sz,
					 &error_local)) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "Failed to write firmware @0x%04x: %s",
				     offset, error_local->message);
			return FALSE;
		}
		if (!fu_ebitdo_device_receive (self, NULL, 0, &error_local)) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "Failed to get ACK for write firmware @0x%04x: %s",
				     offset, error_local->message);
			return FALSE;
		}
	}

	/* mark as complete */
	fu_device_set_progress_full (device, payload_len, payload_len);

	/* set the "encode id" which is likely a checksum, bluetooth pairing
	 * or maybe just security-through-obscurity -- also note:
	 * SET_ENCODE_ID enforces no read for success?! */
	serial_new[0] = self->serial[0] ^ app_key_index[self->serial[0] & 0x0f];
	serial_new[1] = self->serial[1] ^ app_key_index[self->serial[1] & 0x0f];
	serial_new[2] = self->serial[2] ^ app_key_index[self->serial[2] & 0x0f];
	if (!fu_ebitdo_device_send (self,
				 FU_EBITDO_PKT_TYPE_USER_CMD,
				 FU_EBITDO_PKT_CMD_UPDATE_FIRMWARE_DATA,
				 FU_EBITDO_PKT_CMD_FW_SET_ENCODE_ID,
				 (guint8 *) serial_new,
				 sizeof(serial_new),
				 &error_local)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "failed to set encoding ID: %s",
			     error_local->message);
		return FALSE;
	}

	/* mark flash as successful */
	if (!fu_ebitdo_device_send (self,
				 FU_EBITDO_PKT_TYPE_USER_CMD,
				 FU_EBITDO_PKT_CMD_UPDATE_FIRMWARE_DATA,
				 FU_EBITDO_PKT_CMD_FW_UPDATE_OK,
				 NULL, 0,
				 &error_local)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "failed to mark firmware as successful: %s",
			     error_local->message);
		return FALSE;
	}
	if (!fu_ebitdo_device_receive (self, NULL, 0, &error_local)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "failed to get ACK for mark firmware as successful: %s",
			     error_local->message);
		return FALSE;
	}

	/* success! */
	fu_device_set_status (device, FWUPD_STATUS_IDLE);
	return TRUE;
}

static gboolean
fu_ebitdo_device_probe (FuUsbDevice *device, GError **error)
{
	FuEbitdoDevice *self = FU_EBITDO_DEVICE (device);

	/* allowed, but requires manual bootloader step */
	fu_device_add_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_remove_delay (FU_DEVICE (device), FU_DEVICE_REMOVE_DELAY_USER_REPLUG);

	/* set name and vendor */
	fu_device_set_summary (FU_DEVICE (device),
			       "A redesigned classic game controller");
	fu_device_set_vendor (FU_DEVICE (device), "8Bitdo");

	/* add a hardcoded icon name */
	fu_device_add_icon (FU_DEVICE (device), "input-gaming");

	/* only the bootloader can do the update */
	if (!fu_device_has_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		fu_device_add_guid (FU_DEVICE (device), "USB\\VID_0483&PID_5750");
		fu_device_add_guid (FU_DEVICE (device), "USB\\VID_2DC8&PID_5750");
		fu_device_add_flag (FU_DEVICE (device),
				    FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER);
	}

	/* success */
	return TRUE;
}

static void
fu_ebitdo_device_init (FuEbitdoDevice *self)
{
}

static void
fu_ebitdo_device_class_init (FuEbitdoDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUsbDeviceClass *klass_usb_device = FU_USB_DEVICE_CLASS (klass);
	klass_device->write_firmware = fu_ebitdo_device_write_firmware;
	klass_usb_device->open = fu_ebitdo_device_open;
	klass_usb_device->probe = fu_ebitdo_device_probe;
}

/**
 * fu_ebitdo_device_new:
 *
 * Creates a new #FuEbitdoDevice.
 *
 * Returns: (transfer full): a #FuEbitdoDevice, or %NULL if not a game pad
 *
 * Since: 0.1.0
 **/
FuEbitdoDevice *
fu_ebitdo_device_new (GUsbDevice *usb_device)
{
	FuEbitdoDevice *self;
	self = g_object_new (FU_TYPE_EBITDO_DEVICE,
			     "usb-device", usb_device,
			     NULL);
	return FU_EBITDO_DEVICE (self);
}
