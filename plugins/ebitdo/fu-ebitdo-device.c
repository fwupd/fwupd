/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "config.h"

#include <string.h>
#include <appstream-glib.h>

#include "fu-ebitdo-common.h"
#include "fu-ebitdo-device.h"

typedef struct
{
	FuEbitdoDeviceKind	 kind;
	guint32			 serial[9];
} FuEbitdoDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuEbitdoDevice, fu_ebitdo_device, FU_TYPE_USB_DEVICE)

#define GET_PRIVATE(o) (fu_ebitdo_device_get_instance_private (o))

/**
 * fu_ebitdo_device_kind_from_string:
 * @kind: the string.
 *
 * Converts the text representation to an enumerated value.
 *
 * Returns: (transfer full): a #FuEbitdoDeviceKind, or %FU_EBITDO_DEVICE_KIND_UNKNOWN for unknown.
 *
 * Since: 0.1.0
 **/
FuEbitdoDeviceKind
fu_ebitdo_device_kind_from_string (const gchar *kind)
{
	if (g_strcmp0 (kind, "BOOTLOADER") == 0)
		return FU_EBITDO_DEVICE_KIND_BOOTLOADER;
	if (g_strcmp0 (kind, "FC30") == 0)
		return FU_EBITDO_DEVICE_KIND_FC30;
	if (g_strcmp0 (kind, "NES30") == 0)
		return FU_EBITDO_DEVICE_KIND_NES30;
	if (g_strcmp0 (kind, "SFC30") == 0)
		return FU_EBITDO_DEVICE_KIND_SFC30;
	if (g_strcmp0 (kind, "SNES30") == 0)
		return FU_EBITDO_DEVICE_KIND_SNES30;
	if (g_strcmp0 (kind, "FC30PRO") == 0)
		return FU_EBITDO_DEVICE_KIND_FC30PRO;
	if (g_strcmp0 (kind, "NES30PRO") == 0)
		return FU_EBITDO_DEVICE_KIND_NES30PRO;
	if (g_strcmp0 (kind, "FC30_ARCADE") == 0)
		return FU_EBITDO_DEVICE_KIND_FC30_ARCADE;
	return FU_EBITDO_DEVICE_KIND_UNKNOWN;
}

/**
 * fu_ebitdo_device_kind_to_string:
 * @kind: the #FuEbitdoDeviceKind.
 *
 * Converts the enumerated value to an text representation.
 *
 * Returns: string version of @kind
 *
 * Since: 0.1.0
 **/
const gchar *
fu_ebitdo_device_kind_to_string (FuEbitdoDeviceKind kind)
{
	if (kind == FU_EBITDO_DEVICE_KIND_BOOTLOADER)
		return "BOOTLOADER";
	if (kind == FU_EBITDO_DEVICE_KIND_FC30)
		return "FC30";
	if (kind == FU_EBITDO_DEVICE_KIND_NES30)
		return "NES30";
	if (kind == FU_EBITDO_DEVICE_KIND_SFC30)
		return "SFC30";
	if (kind == FU_EBITDO_DEVICE_KIND_SNES30)
		return "SNES30";
	if (kind == FU_EBITDO_DEVICE_KIND_FC30PRO)
		return "FC30PRO";
	if (kind == FU_EBITDO_DEVICE_KIND_NES30PRO)
		return "NES30PRO";
	if (kind == FU_EBITDO_DEVICE_KIND_FC30_ARCADE)
		return "FC30_ARCADE";
	return NULL;
}

FuEbitdoDeviceKind
fu_ebitdo_device_get_kind (FuEbitdoDevice *device)
{
	FuEbitdoDevicePrivate *priv = GET_PRIVATE (device);
	return priv->kind;
}

static gboolean
fu_ebitdo_device_send (FuEbitdoDevice *device,
		       FuEbitdoPktType type,
		       FuEbitdoPktCmd subtype,
		       FuEbitdoPktCmd cmd,
		       const guint8 *in,
		       gsize in_len,
		       GError **error)
{
	FuEbitdoDevicePrivate *priv = GET_PRIVATE (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	guint8 packet[FU_EBITDO_USB_EP_SIZE];
	gsize actual_length;
	guint8 ep_out = FU_EBITDO_USB_RUNTIME_EP_OUT;
	g_autoptr(GError) error_local = NULL;
	FuEbitdoPkt *hdr = (FuEbitdoPkt *) packet;

	/* different */
	if (priv->kind == FU_EBITDO_DEVICE_KIND_BOOTLOADER)
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
	memset (packet, 0x00, sizeof(packet));
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
fu_ebitdo_device_receive (FuEbitdoDevice *device,
		       guint8 *out,
		       gsize out_len,
		       GError **error)
{
	FuEbitdoDevicePrivate *priv = GET_PRIVATE (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	guint8 packet[FU_EBITDO_USB_EP_SIZE];
	gsize actual_length;
	guint8 ep_in = FU_EBITDO_USB_RUNTIME_EP_IN;
	g_autoptr(GError) error_local = NULL;
	FuEbitdoPkt *hdr = (FuEbitdoPkt *) packet;

	/* different */
	if (priv->kind == FU_EBITDO_DEVICE_KIND_BOOTLOADER)
		ep_in = FU_EBITDO_USB_BOOTLOADER_EP_IN;

	/* get data from device */
	memset (packet, 0x0, sizeof(packet));
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
fu_ebitdo_device_set_version (FuEbitdoDevice *device, guint32 version)
{
	g_autofree gchar *tmp = NULL;
	tmp = g_strdup_printf ("%.2f", version / 100.f);
	fu_device_set_version (FU_DEVICE (device), tmp);
}

static gboolean
fu_ebitdo_device_validate (FuEbitdoDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
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
	FuEbitdoDevicePrivate *priv = GET_PRIVATE (self);
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);
	gdouble tmp;
	guint32 version_tmp = 0;
	guint32 serial_tmp[9];

	/* open, then ensure this is actually 8Bitdo hardware */
	g_debug ("opening %s", fu_ebitdo_device_kind_to_string (priv->kind));
	if (!fu_ebitdo_device_validate (self, error))
		return FALSE;
	if (!g_usb_device_claim_interface (usb_device, 0, /* 0 = idx? */
					   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					   error)) {
		return FALSE;
	}

	/* in firmware mode */
	if (priv->kind != FU_EBITDO_DEVICE_KIND_BOOTLOADER) {
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
	memset (serial_tmp, 0x00, sizeof (serial_tmp));
	if (!fu_ebitdo_device_receive (self,
				    (guint8 *) &serial_tmp, sizeof(serial_tmp),
				    error)) {
		return FALSE;
	}
	for (guint i = 0; i < 9; i++)
		priv->serial[i] = GUINT32_FROM_LE (serial_tmp[i]);

	/* success */
	return TRUE;
}

const guint32 *
fu_ebitdo_device_get_serial (FuEbitdoDevice *device)
{
	FuEbitdoDevicePrivate *priv = GET_PRIVATE (device);
	return priv->serial;
}

static void
fu_ebitdo_device_set_progress (FuEbitdoDevice *device, guint32 current, guint32 total)
{
	gdouble percentage = -1.f;
	if (total > 0)
		percentage = (100.f * (gdouble) current) / (gdouble) total;
	if (g_getenv ("FWUPD_EBITDO_VERBOSE") != NULL) {
		g_debug ("written %" G_GUINT32_FORMAT "/%" G_GUINT32_FORMAT " bytes [%.1f%%]",
			 current, total, percentage);
	}
	fu_device_set_progress (FU_DEVICE (device), (guint) percentage);
}

gboolean
fu_ebitdo_device_write_firmware (FuEbitdoDevice *device, GBytes *fw, GError **error)
{
	FuEbitdoDevicePrivate *priv = GET_PRIVATE (device);
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
	if (!fu_ebitdo_device_send (device,
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
	if (!fu_ebitdo_device_receive (device, NULL, 0, &error_local)) {
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
		fu_ebitdo_device_set_progress (device, offset, payload_len);
		if (!fu_ebitdo_device_send (device,
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
		if (!fu_ebitdo_device_receive (device, NULL, 0, &error_local)) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "Failed to get ACK for write firmware @0x%04x: %s",
				     offset, error_local->message);
			return FALSE;
		}
	}

	/* mark as complete */
	fu_ebitdo_device_set_progress (device, payload_len, payload_len);

	/* set the "encode id" which is likely a checksum, bluetooth pairing
	 * or maybe just security-through-obscurity -- also note:
	 * SET_ENCODE_ID enforces no read for success?! */
	serial_new[0] = priv->serial[0] ^ app_key_index[priv->serial[0] & 0x0f];
	serial_new[1] = priv->serial[1] ^ app_key_index[priv->serial[1] & 0x0f];
	serial_new[2] = priv->serial[2] ^ app_key_index[priv->serial[2] & 0x0f];
	if (!fu_ebitdo_device_send (device,
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
	if (!fu_ebitdo_device_send (device,
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
	if (!fu_ebitdo_device_receive (device, NULL, 0, &error_local)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "failed to get ACK for mark firmware as successful: %s",
			     error_local->message);
		return FALSE;
	}

	/* success! */
	return TRUE;
}

typedef struct {
	guint16			 vid;
	guint16			 pid;
	FuEbitdoDeviceKind	 kind;
} FuEbitdoVidPid;

static FuEbitdoDeviceKind
fu_ebitdo_device_detect_kind (GUsbDevice *usb_device)
{
	const FuEbitdoVidPid vidpids[] = {
		/* legacy VIDs */
		{ 0x0483, 0x5750, FU_EBITDO_DEVICE_KIND_BOOTLOADER },
		{ 0x1235, 0xab11, FU_EBITDO_DEVICE_KIND_FC30 },
		{ 0x1235, 0xab12, FU_EBITDO_DEVICE_KIND_NES30 },
		{ 0x1235, 0xab21, FU_EBITDO_DEVICE_KIND_SFC30 },
		{ 0x1235, 0xab20, FU_EBITDO_DEVICE_KIND_SNES30 },
		{ 0x1002, 0x9000, FU_EBITDO_DEVICE_KIND_FC30PRO },
		{ 0x2002, 0x9000, FU_EBITDO_DEVICE_KIND_NES30PRO },
		{ 0x8000, 0x1002, FU_EBITDO_DEVICE_KIND_FC30_ARCADE },
		/* new VID */
		{ 0x2dc8, 0x5750, FU_EBITDO_DEVICE_KIND_BOOTLOADER },
		{ 0x2dc8, 0xab11, FU_EBITDO_DEVICE_KIND_FC30 },
		{ 0x2dc8, 0xab12, FU_EBITDO_DEVICE_KIND_NES30 },
		{ 0x2dc8, 0xab21, FU_EBITDO_DEVICE_KIND_SFC30 },
		{ 0x2dc8, 0xab20, FU_EBITDO_DEVICE_KIND_SNES30 },
		{ 0x2dc8, 0x9000, FU_EBITDO_DEVICE_KIND_FC30PRO },
		{ 0x2dc8, 0x9001, FU_EBITDO_DEVICE_KIND_NES30PRO },
		{ 0x2dc8, 0x1002, FU_EBITDO_DEVICE_KIND_FC30_ARCADE },
		{ 0x0000, 0x0000, FU_EBITDO_DEVICE_KIND_UNKNOWN }
	};

	/* find correct kind */
	for (guint j = 0; vidpids[j].vid != 0x0000; j++) {
		if (g_usb_device_get_vid (usb_device) == vidpids[j].vid &&
		    g_usb_device_get_pid (usb_device) == vidpids[j].pid) {
			return vidpids[j].kind;
		}
	}
	return FU_EBITDO_DEVICE_KIND_UNKNOWN;
}

static gboolean
fu_ebitdo_device_probe (FuUsbDevice *device, GError **error)
{
	FuEbitdoDevice *self = FU_EBITDO_DEVICE (device);
	FuEbitdoDevicePrivate *priv = GET_PRIVATE (self);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	g_autofree gchar *name = NULL;

	/* not supported */
	priv->kind = fu_ebitdo_device_detect_kind (usb_device);
	if (priv->kind == FU_EBITDO_DEVICE_KIND_UNKNOWN) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "not supported with this device");
		return FALSE;
	}

	/* allowed, but requires manual bootloader step */
	fu_device_add_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_remove_delay (FU_DEVICE (device), FU_DEVICE_REMOVE_DELAY_USER_REPLUG);

	/* set name and vendor */
	name = g_strdup_printf ("%s Gamepad",
				fu_ebitdo_device_kind_to_string (priv->kind));
	fu_device_set_name (FU_DEVICE (device), name);
	fu_device_set_summary (FU_DEVICE (device),
			       "A redesigned classic game controller");
	fu_device_set_vendor (FU_DEVICE (device), "8Bitdo");

	/* add a hardcoded icon name */
	fu_device_add_icon (FU_DEVICE (device), "input-gaming");

	/* only the bootloader can do the update */
	if (priv->kind != FU_EBITDO_DEVICE_KIND_BOOTLOADER) {
		fu_device_add_guid (FU_DEVICE (device), "USB\\VID_0483&PID_5750");
		fu_device_add_guid (FU_DEVICE (device), "USB\\VID_2DC8&PID_5750");
		fu_device_add_flag (FU_DEVICE (device),
				    FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER);
	}

	/* success */
	return TRUE;
}

static void
fu_ebitdo_device_init (FuEbitdoDevice *device)
{
}

static void
fu_ebitdo_device_class_init (FuEbitdoDeviceClass *klass)
{
	FuUsbDeviceClass *klass_usb_device = FU_USB_DEVICE_CLASS (klass);
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
	FuEbitdoDevice *device;
	device = g_object_new (FU_TYPE_EBITDO_DEVICE,
			       "usb-device", usb_device,
			       NULL);
	return FU_EBITDO_DEVICE (device);
}
