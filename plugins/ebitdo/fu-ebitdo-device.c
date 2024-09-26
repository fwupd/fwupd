/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-ebitdo-device.h"
#include "fu-ebitdo-firmware.h"
#include "fu-ebitdo-struct.h"

struct _FuEbitdoDevice {
	FuUsbDevice parent_instance;
	guint32 serial[9];
};

G_DEFINE_TYPE(FuEbitdoDevice, fu_ebitdo_device, FU_TYPE_USB_DEVICE)

#define FU_EBITDO_USB_TIMEOUT		5000 /* ms */
#define FU_EBITDO_USB_BOOTLOADER_EP_IN	0x82
#define FU_EBITDO_USB_BOOTLOADER_EP_OUT 0x01
#define FU_EBITDO_USB_RUNTIME_EP_IN	0x81
#define FU_EBITDO_USB_RUNTIME_EP_OUT	0x02
#define FU_EBITDO_USB_EP_SIZE		64 /* bytes */

static gboolean
fu_ebitdo_device_send(FuEbitdoDevice *self,
		      FuEbitdoPktType type,
		      FuEbitdoPktCmd subtype,
		      FuEbitdoPktCmd cmd,
		      const guint8 *in,
		      gsize in_len,
		      GError **error)
{
	gsize actual_length;
	guint8 ep_out = FU_EBITDO_USB_RUNTIME_EP_OUT;
	g_autoptr(GByteArray) st_hdr = fu_struct_ebitdo_pkt_new();
	g_autoptr(GError) error_local = NULL;

	fu_byte_array_set_size(st_hdr, FU_EBITDO_USB_EP_SIZE, 0x0);

	/* different */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		ep_out = FU_EBITDO_USB_BOOTLOADER_EP_OUT;

	/* check size */
	if (in_len > 64 - 8) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "input buffer too large");
		return FALSE;
	}

	/* packet[0] is the total length of the packet */
	fu_struct_ebitdo_pkt_set_type(st_hdr, type);
	fu_struct_ebitdo_pkt_set_subtype(st_hdr, subtype);

	/* do we have a payload */
	if (in_len > 0) {
		fu_struct_ebitdo_pkt_set_cmd_len(st_hdr, in_len + 3);
		fu_struct_ebitdo_pkt_set_cmd(st_hdr, cmd);
		fu_struct_ebitdo_pkt_set_payload_len(st_hdr, in_len);
		if (!fu_memcpy_safe(st_hdr->data,
				    st_hdr->len,
				    FU_STRUCT_EBITDO_PKT_SIZE, /* dst */
				    in,
				    in_len,
				    0x0, /* src */
				    in_len,
				    error))
			return FALSE;
		fu_struct_ebitdo_pkt_set_pkt_len(st_hdr, in_len + 7);
	} else {
		fu_struct_ebitdo_pkt_set_cmd_len(st_hdr, in_len + 1);
		fu_struct_ebitdo_pkt_set_cmd(st_hdr, cmd);
		fu_struct_ebitdo_pkt_set_pkt_len(st_hdr, 5);
	}
	fu_dump_raw(G_LOG_DOMAIN, "->DEVICE", st_hdr->data, st_hdr->len);

	/* get data from device */
	if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(self),
					      ep_out,
					      st_hdr->data,
					      st_hdr->len,
					      &actual_length,
					      FU_EBITDO_USB_TIMEOUT,
					      NULL, /* cancellable */
					      &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to send to device on ep 0x%02x: %s",
			    (guint)FU_EBITDO_USB_BOOTLOADER_EP_OUT,
			    error_local->message);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ebitdo_device_receive(FuEbitdoDevice *self, guint8 *out, gsize out_len, GError **error)
{
	guint8 packet[FU_EBITDO_USB_EP_SIZE] = {0};
	gsize actual_length;
	guint8 ep_in = FU_EBITDO_USB_RUNTIME_EP_IN;
	g_autoptr(GByteArray) st_hdr = NULL;
	g_autoptr(GError) error_local = NULL;

	/* different */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		ep_in = FU_EBITDO_USB_BOOTLOADER_EP_IN;

	/* get data from device */
	if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(self),
					      ep_in,
					      packet,
					      sizeof(packet),
					      &actual_length,
					      FU_EBITDO_USB_TIMEOUT,
					      NULL, /* cancellable */
					      &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to retrieve from device on ep 0x%02x: %s",
			    (guint)ep_in,
			    error_local->message);
		return FALSE;
	}

	/* debug */
	fu_dump_raw(G_LOG_DOMAIN, "<-DEVICE", packet, actual_length);
	st_hdr = fu_struct_ebitdo_pkt_parse(packet, sizeof(packet), 0x0, error);
	if (st_hdr == NULL)
		return FALSE;

	/* get-version (bootloader) */
	if (fu_struct_ebitdo_pkt_get_type(st_hdr) == FU_EBITDO_PKT_TYPE_USER_CMD &&
	    fu_struct_ebitdo_pkt_get_subtype(st_hdr) == FU_EBITDO_PKT_CMD_UPDATE_FIRMWARE_DATA &&
	    fu_struct_ebitdo_pkt_get_cmd(st_hdr) == FU_EBITDO_PKT_CMD_FW_GET_VERSION) {
		if (out != NULL) {
			if (fu_struct_ebitdo_pkt_get_payload_len(st_hdr) < out_len) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "payload too small, expected %" G_GSIZE_FORMAT
					    " got %u",
					    out_len,
					    fu_struct_ebitdo_pkt_get_payload_len(st_hdr));
				return FALSE;
			}
			if (!fu_memcpy_safe(out,
					    out_len,
					    0x0, /* dst */
					    packet,
					    sizeof(packet),
					    FU_STRUCT_EBITDO_PKT_SIZE, /* src */
					    out_len,
					    error))
				return FALSE;
		}
		return TRUE;
	}

	/* get-version (firmware) -- not a packet, just raw data! */
	if (fu_struct_ebitdo_pkt_get_pkt_len(st_hdr) == FU_EBITDO_PKT_CMD_GET_VERSION_RESPONSE) {
		if (out != NULL) {
			if (out_len != 4) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "outbuf size wrong, expected 4 got %" G_GSIZE_FORMAT,
					    out_len);
				return FALSE;
			}
			if (!fu_memcpy_safe(out,
					    out_len,
					    0x0, /* dst */
					    packet,
					    sizeof(packet),
					    0x1, /* src */
					    4,
					    error))
				return FALSE;
		}
		return TRUE;
	}

	/* verification-id response */
	if (fu_struct_ebitdo_pkt_get_type(st_hdr) == FU_EBITDO_PKT_TYPE_USER_CMD &&
	    fu_struct_ebitdo_pkt_get_subtype(st_hdr) == FU_EBITDO_PKT_CMD_VERIFICATION_ID) {
		if (out != NULL) {
			if (fu_struct_ebitdo_pkt_get_cmd_len(st_hdr) != out_len) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "outbuf size wrong, expected %" G_GSIZE_FORMAT
					    " got %i",
					    out_len,
					    fu_struct_ebitdo_pkt_get_cmd_len(st_hdr));
				return FALSE;
			}
			if (!fu_memcpy_safe(out,
					    out_len,
					    0x0, /* dst */
					    packet,
					    sizeof(packet),
					    FU_STRUCT_EBITDO_PKT_SIZE - 3, /* src */
					    fu_struct_ebitdo_pkt_get_cmd_len(st_hdr),
					    error))
				return FALSE;
		}
		return TRUE;
	}

	/* update-firmware-data */
	if (fu_struct_ebitdo_pkt_get_type(st_hdr) == FU_EBITDO_PKT_TYPE_USER_CMD &&
	    fu_struct_ebitdo_pkt_get_subtype(st_hdr) == FU_EBITDO_PKT_CMD_UPDATE_FIRMWARE_DATA &&
	    fu_struct_ebitdo_pkt_get_payload_len(st_hdr) == 0x00) {
		if (fu_struct_ebitdo_pkt_get_cmd(st_hdr) != FU_EBITDO_PKT_CMD_ACK) {
			g_set_error(
			    error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "write failed, got %s",
			    fu_ebitdo_pkt_cmd_to_string(fu_struct_ebitdo_pkt_get_cmd(st_hdr)));
			return FALSE;
		}
		return TRUE;
	}

	/* unhandled */
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "unexpected device response");
	return FALSE;
}

static gchar *
fu_ebitdo_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return g_strdup_printf("%u.%02u", (guint)version_raw / 100, (guint)version_raw % 100);
}

static gboolean
fu_ebitdo_device_validate(FuEbitdoDevice *self, GError **error)
{
	const gchar *ven;
	const gchar *allowlist[] = {"8Bitdo", "8BitDo", "SFC30", NULL};

	/* this is a new, always valid, VID */
	if (fu_device_get_vid(FU_DEVICE(self)) == 0x2dc8)
		return TRUE;

	/* verify the vendor prefix against a allowlist */
	ven = fu_device_get_vendor(FU_DEVICE(self));
	if (ven == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "could not check vendor descriptor: ");
		return FALSE;
	}
	for (guint i = 0; allowlist[i] != NULL; i++) {
		if (g_str_has_prefix(ven, allowlist[i]))
			return TRUE;
	}
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_DATA,
		    "vendor '%s' did not match allowlist, "
		    "probably not a 8BitDo device…",
		    ven);
	return FALSE;
}

static gboolean
fu_ebitdo_device_open(FuDevice *device, GError **error)
{
	FuEbitdoDevice *self = FU_EBITDO_DEVICE(device);

	/* FuUsbDevice->open */
	if (!FU_DEVICE_CLASS(fu_ebitdo_device_parent_class)->open(device, error))
		return FALSE;

	/* open, then ensure this is actually 8BitDo hardware */
	if (!fu_ebitdo_device_validate(self, error))
		return FALSE;
	if (!fu_usb_device_claim_interface(FU_USB_DEVICE(self),
					   0, /* 0 = idx? */
					   FU_USB_DEVICE_CLAIM_FLAG_KERNEL_DRIVER,
					   error)) {
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_ebitdo_device_setup(FuDevice *device, GError **error)
{
	FuEbitdoDevice *self = FU_EBITDO_DEVICE(device);
	guint32 version_tmp = 0;
	guint32 serial_tmp[9] = {0};

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_ebitdo_device_parent_class)->setup(device, error))
		return FALSE;

	/* in firmware mode */
	if (!fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		if (!fu_ebitdo_device_send(self,
					   FU_EBITDO_PKT_TYPE_USER_CMD,
					   FU_EBITDO_PKT_CMD_GET_VERSION,
					   0,
					   NULL,
					   0, /* in */
					   error)) {
			return FALSE;
		}
		if (!fu_ebitdo_device_receive(self,
					      (guint8 *)&version_tmp,
					      sizeof(version_tmp),
					      error)) {
			return FALSE;
		}
		fu_device_set_version_raw(FU_DEVICE(self), GUINT32_FROM_LE(version_tmp));
		return TRUE;
	}

	/* get version */
	if (!fu_ebitdo_device_send(self,
				   FU_EBITDO_PKT_TYPE_USER_CMD,
				   FU_EBITDO_PKT_CMD_UPDATE_FIRMWARE_DATA,
				   FU_EBITDO_PKT_CMD_FW_GET_VERSION,
				   NULL,
				   0, /* in */
				   error)) {
		return FALSE;
	}
	if (!fu_ebitdo_device_receive(self, (guint8 *)&version_tmp, sizeof(version_tmp), error)) {
		return FALSE;
	}
	fu_device_set_version_raw(device, GUINT32_FROM_LE(version_tmp));

	/* get verification ID */
	if (!fu_ebitdo_device_send(self,
				   FU_EBITDO_PKT_TYPE_USER_CMD,
				   FU_EBITDO_PKT_CMD_GET_VERIFICATION_ID,
				   0x00, /* cmd */
				   NULL,
				   0,
				   error)) {
		return FALSE;
	}
	if (!fu_ebitdo_device_receive(self, (guint8 *)&serial_tmp, sizeof(serial_tmp), error)) {
		return FALSE;
	}
	for (guint i = 0; i < 9; i++)
		self->serial[i] = GUINT32_FROM_LE(serial_tmp[i]);

	/* success */
	return TRUE;
}

static gboolean
fu_ebitdo_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	g_autoptr(FwupdRequest) request = fwupd_request_new();

	/* not required */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	/* generate a message if not already set from the metadata */
	if (fu_device_get_update_message(device) == NULL) {
		g_autoptr(GString) msg = g_string_new(NULL);
		g_string_append(msg, "Not in bootloader mode: Disconnect the controller, ");
		switch (fu_device_get_pid(device)) {
		case 0xab11: /* FC30 */
		case 0xab12: /* NES30 */
		case 0xab21: /* SFC30 */
		case 0xab20: /* SNES30 */
		case 0x9012: /* SN30v2 */
			g_string_append(msg,
					"hold down L+R+START for 3 seconds until "
					"both LED lights flashing, ");
			break;
		case 0x9000: /* FC30PRO */
		case 0x9001: /* NES30PRO */
			g_string_append(msg,
					"hold down RETURN+POWER for 3 seconds until "
					"both LED lights flashing, ");
			break;
		case 0x1002: /* FC30-ARCADE */
			g_string_append(msg,
					"hold down L1+R1+HOME for 3 seconds until "
					"both blue LED and green LED blink, ");
			break;
		case 0x6000: /* SF30 pro: Dinput mode */
		case 0x6001: /* SN30 pro: Dinput mode */
		case 0x6002: /* SN30 pro+: Dinput mode */
		case 0x028e: /* SF30/SN30 pro: Xinput mode */
		case 0x5006: /* M30 */
			g_string_append(msg,
					"press and hold L1+R1+START for 3 seconds "
					"until the LED on top blinks red, ");
			break;
		case 0x2100: /* SN30 for Android */
		case 0x2101: /* SN30 for Android */
			g_string_append(msg,
					"press and hold LB+RB+Xbox buttons "
					"both white LED and green LED blink, ");
			break;
		case 0x9015: /* N30 Pro 2 */
			g_string_append(msg,
					"press and hold L1+R1+START buttons "
					"until the yellow LED blinks, ");
			break;
		default:
			g_string_append(msg, "do what it says in the manual, ");
			break;
		}
		g_string_append(msg, "then re-connect controller");
		fu_device_set_update_message(device, msg->str);
	}

	/* wait */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	/* emit request */
	fwupd_request_set_kind(request, FWUPD_REQUEST_KIND_IMMEDIATE);
	fwupd_request_set_id(request, FWUPD_REQUEST_ID_REMOVE_REPLUG);
	fwupd_request_set_message(request, fu_device_get_update_message(device));
	fwupd_request_set_image(request, fu_device_get_update_image(device));
	return fu_device_emit_request(device, request, progress, error);
}

static gboolean
fu_ebitdo_device_write_firmware(FuDevice *device,
				FuFirmware *firmware,
				FuProgress *progress,
				FwupdInstallFlags flags,
				GError **error)
{
	FuEbitdoDevice *self = FU_EBITDO_DEVICE(device);
	const guint8 *buf;
	gsize bufsz = 0;
	guint32 serial_new[3];
	g_autoptr(GBytes) fw_hdr = NULL;
	g_autoptr(GInputStream) stream_payload = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;
	const guint32 app_key_index[16] = {0x186976e5,
					   0xcac67acd,
					   0x38f27fee,
					   0x0a4948f1,
					   0xb75b7753,
					   0x1f8ffa5c,
					   0xbff8cf43,
					   0xc4936167,
					   0x92bd03f0,
					   0x5573c6ed,
					   0x57d8845b,
					   0x827197ac,
					   0xb91901c9,
					   0x3917edfe,
					   0xbcd6344f,
					   0xcf9e23b5};

	/* not in bootloader mode */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NEEDS_USER_ACTION,
				    "Not in bootloader mode");
		return FALSE;
	}

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "header");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 97, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 2, NULL);

	/* get header and payload */
	fw_hdr = fu_firmware_get_image_by_id_bytes(firmware, FU_FIRMWARE_ID_HEADER, error);
	if (fw_hdr == NULL)
		return FALSE;
	stream_payload = fu_firmware_get_stream(firmware, error);
	if (stream_payload == NULL)
		return FALSE;

	/* set up the firmware header */
	buf = g_bytes_get_data(fw_hdr, &bufsz);
	if (!fu_ebitdo_device_send(self,
				   FU_EBITDO_PKT_TYPE_USER_CMD,
				   FU_EBITDO_PKT_CMD_UPDATE_FIRMWARE_DATA,
				   FU_EBITDO_PKT_CMD_FW_UPDATE_HEADER,
				   buf,
				   bufsz,
				   error)) {
		g_prefix_error(error, "failed to set up firmware header: ");
		return FALSE;
	}
	if (!fu_ebitdo_device_receive(self, NULL, 0, error)) {
		g_prefix_error(error, "failed to get ACK for fw update header: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* flash the firmware in 32 byte blocks */
	chunks = fu_chunk_array_new_from_stream(stream_payload, 0x0, 32, error);
	if (chunks == NULL)
		return FALSE;
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		g_debug("writing %u bytes to 0x%04x of 0x%04x",
			(guint)fu_chunk_get_data_sz(chk),
			(guint)fu_chunk_get_address(chk),
			(guint)fu_chunk_get_data_sz(chk));
		if (!fu_ebitdo_device_send(self,
					   FU_EBITDO_PKT_TYPE_USER_CMD,
					   FU_EBITDO_PKT_CMD_UPDATE_FIRMWARE_DATA,
					   FU_EBITDO_PKT_CMD_FW_UPDATE_DATA,
					   fu_chunk_get_data(chk),
					   fu_chunk_get_data_sz(chk),
					   error)) {
			g_prefix_error(error,
				       "failed to write firmware @0x%04x: ",
				       (guint)fu_chunk_get_address(chk));
			return FALSE;
		}
		if (!fu_ebitdo_device_receive(self, NULL, 0, error)) {
			g_prefix_error(error,
				       "failed to get ACK for write firmware @0x%04x: ",
				       (guint)fu_chunk_get_address(chk));
			return FALSE;
		}
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						i + 1,
						fu_chunk_array_length(chunks));
	}
	fu_progress_step_done(progress);

	/* set the "encode id" which is likely a checksum, bluetooth pairing
	 * or maybe just security-through-obscurity -- also note:
	 * SET_ENCODE_ID enforces no read for success?! */
	serial_new[0] = self->serial[0] ^ app_key_index[self->serial[0] & 0x0f];
	serial_new[1] = self->serial[1] ^ app_key_index[self->serial[1] & 0x0f];
	serial_new[2] = self->serial[2] ^ app_key_index[self->serial[2] & 0x0f];
	if (!fu_ebitdo_device_send(self,
				   FU_EBITDO_PKT_TYPE_USER_CMD,
				   FU_EBITDO_PKT_CMD_UPDATE_FIRMWARE_DATA,
				   FU_EBITDO_PKT_CMD_FW_SET_ENCODE_ID,
				   (guint8 *)serial_new,
				   sizeof(serial_new),
				   error)) {
		g_prefix_error(error, "failed to set encoding ID: ");
		return FALSE;
	}

	/* mark flash as successful */
	if (!fu_ebitdo_device_send(self,
				   FU_EBITDO_PKT_TYPE_USER_CMD,
				   FU_EBITDO_PKT_CMD_UPDATE_FIRMWARE_DATA,
				   FU_EBITDO_PKT_CMD_FW_UPDATE_OK,
				   NULL,
				   0,
				   error)) {
		g_prefix_error(error, "failed to mark firmware as successful: ");
		return FALSE;
	}
	if (!fu_ebitdo_device_receive(self, NULL, 0, &error_local)) {
		g_prefix_error(&error_local, "failed to get ACK for mark firmware as successful: ");
		if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_WILL_DISAPPEAR)) {
			fu_device_set_remove_delay(device, 0);
			g_debug("%s", error_local->message);
			return TRUE;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success! */
	return TRUE;
}

static gboolean
fu_ebitdo_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	g_autoptr(GError) error_local = NULL;

	/* when doing a soft-reboot the device does not re-enumerate properly
	 * so manually reboot the FuUsbDevice */
	if (!fu_usb_device_reset(FU_USB_DEVICE(device), &error_local)) {
		g_prefix_error(&error_local, "failed to force-reset device: ");
		if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_WILL_DISAPPEAR)) {
			fu_device_set_remove_delay(device, 0);
			g_debug("%s", error_local->message);
			return TRUE;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}

	/* not all 8bito devices come back in the right mode */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_WILL_DISAPPEAR))
		fu_device_set_remove_delay(device, 0);
	else
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	/* success! */
	return TRUE;
}

static gboolean
fu_ebitdo_device_probe(FuDevice *device, GError **error)
{
	/* allowed, but requires manual bootloader step */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_remove_delay(device, FU_DEVICE_REMOVE_DELAY_USER_REPLUG);

	/* set name and vendor */
	fu_device_set_summary(device, "A redesigned classic game controller");
	fu_device_set_vendor(device, "8BitDo");

	/* add a hardcoded icon name */
	fu_device_add_icon(device, "input-gaming");

	/* only the bootloader can do the update */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		fu_device_add_counterpart_guid(device, "USB\\VID_0483&PID_5750");
		fu_device_add_counterpart_guid(device, "USB\\VID_2DC8&PID_5750");
	}

	/* success */
	return TRUE;
}

static void
fu_ebitdo_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_NO_PROFILE);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 97, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_ebitdo_device_init(FuEbitdoDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.8bitdo");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ADD_COUNTERPART_GUIDS);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_request_flag(FU_DEVICE(self), FWUPD_REQUEST_FLAG_NON_GENERIC_MESSAGE);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_EBITDO_FIRMWARE);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
}

static void
fu_ebitdo_device_class_init(FuEbitdoDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->write_firmware = fu_ebitdo_device_write_firmware;
	device_class->setup = fu_ebitdo_device_setup;
	device_class->detach = fu_ebitdo_device_detach;
	device_class->attach = fu_ebitdo_device_attach;
	device_class->open = fu_ebitdo_device_open;
	device_class->probe = fu_ebitdo_device_probe;
	device_class->set_progress = fu_ebitdo_device_set_progress;
	device_class->convert_version = fu_ebitdo_device_convert_version;
}
