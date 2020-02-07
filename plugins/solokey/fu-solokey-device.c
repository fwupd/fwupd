/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-chunk.h"
#include "fu-solokey-device.h"
#include "fu-solokey-firmware.h"

struct _FuSolokeyDevice {
	FuUsbDevice		 parent_instance;
	guint32			 cid;
};

G_DEFINE_TYPE (FuSolokeyDevice, fu_solokey_device, FU_TYPE_USB_DEVICE)

#define SOLO_EXTENSION_VERSION			0x14
#define SOLO_BOOTLOADER_WRITE			0x40
#define SOLO_BOOTLOADER_DONE			0x41
#define SOLO_BOOTLOADER_VERSION			0x44
#define SOLO_BOOTLOADER_HID_CMD_BOOT		0x50

#define SOLO_USB_TIMEOUT			5000 /* ms */
#define SOLO_USB_HID_EP				0x0001
#define SOLO_USB_HID_EP_IN			(SOLO_USB_HID_EP | 0x80)
#define SOLO_USB_HID_EP_OUT			(SOLO_USB_HID_EP | 0x00)
#define SOLO_USB_HID_EP_SIZE			64

static void
fu_solokey_device_exchange (GByteArray *req, guint8 cmd, guint32 addr, GByteArray *ibuf)
{
	guint8 buf_addr[4] = { 0x00 };
	guint8 buf_len[2] = { 0x00 };

	/* command */
	fu_byte_array_append_uint8 (req, cmd);

	/* first *3* bytes of the LE address */
	fu_common_write_uint32 (buf_addr, addr, G_LITTLE_ENDIAN);
	g_byte_array_append (req, buf_addr, 3);

	/* "random" number :/ */
	g_byte_array_append (req, (const guint8 *) "\x8C\x27\x90\xf6", 4);

	/* uint16 length then optional (or dummy) data */
	if (ibuf != NULL) {
		fu_common_write_uint16 (buf_len, ibuf->len, G_BIG_ENDIAN);
		g_byte_array_append (req, buf_len, sizeof(buf_len));
		g_byte_array_append (req, ibuf->data, ibuf->len);
		return;
	}

	/* dummy data */
	fu_common_write_uint16 (buf_len, 16, G_BIG_ENDIAN);
	g_byte_array_append (req, buf_len, sizeof(buf_len));
	for (guint i = 0; i < 16; i++)
		fu_byte_array_append_uint8 (req, 'A');
}

static gboolean
fu_solokey_device_probe (FuUsbDevice *device, GError **error)
{
	/* always disregard the bcdVersion */
	fu_device_set_version (FU_DEVICE (device), NULL, FWUPD_VERSION_FORMAT_UNKNOWN);
	return TRUE;
}

static gboolean
fu_solokey_device_open (FuUsbDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);
	g_autofree gchar *product = NULL;
	g_auto(GStrv) split = NULL;

	/* got the version using the HID API */
	if (!g_usb_device_set_configuration (usb_device, 0x0001, error))
		return FALSE;
	if (!g_usb_device_claim_interface (usb_device, 0x0000,
					   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					   error)) {
		return FALSE;
	}

	/* parse product ID */
	product = g_usb_device_get_string_descriptor (usb_device,
						      g_usb_device_get_product_index (usb_device),
						      error);
	if (product == NULL)
		return FALSE;
	split = g_strsplit (product, " ", -1);
	if (g_strv_length (split) < 2) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "product not parsable, got '%s'",
			     product);
		return FALSE;
	}
	if (g_strcmp0 (split[0], "Solo") != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "product not expected format, got '%s'",
			     product);
		return FALSE;
	}
	if (g_strcmp0 (split[1], "Hacker") == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "Only Solo Secure supported");
		return FALSE;
	}
	if (g_strcmp0 (split[1], "Bootloader") == 0) {
		fu_device_set_version_bootloader (FU_DEVICE (device), split[2]);
		fu_device_add_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		fu_device_remove_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER);
	} else if (g_strcmp0 (split[1], "Keys") == 0 && g_strcmp0 (split[2], "Solo") == 0) {
		fu_device_add_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		fu_device_remove_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER);
	} else {
		fu_device_set_version (FU_DEVICE (device), split[1], FWUPD_VERSION_FORMAT_TRIPLET);
		fu_device_remove_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		fu_device_add_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_solokey_device_close (FuUsbDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);

	/* rebind kernel driver so it works as a security key again... */
	if (!g_usb_device_release_interface (usb_device, 0x0000,
					     G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					     error)) {
		g_prefix_error (error, "failed to release interface: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_solokey_device_packet_tx (FuSolokeyDevice *self, GByteArray *req, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	gsize actual_length = 0;

	/* round up to the endpoint size */
	for (guint i = req->len; i < SOLO_USB_HID_EP_SIZE; i++)
		fu_byte_array_append_uint8 (req, 0x0);

	/* request */
	if (g_getenv ("FWUPD_SOLOKEY_VERBOSE") != NULL) {
		fu_common_dump_full (G_LOG_DOMAIN, "REQ", req->data, req->len,
				     16, FU_DUMP_FLAGS_SHOW_ADDRESSES);
	}

	/* do not hit hardware */
	if (g_getenv ("FWUPD_SOLOKEY_EMULATE") != NULL)
		return TRUE;

	if (!g_usb_device_interrupt_transfer (usb_device,
					      SOLO_USB_HID_EP_OUT,
					      req->data,
					      req->len,
					      &actual_length,
					      SOLO_USB_TIMEOUT,
					      NULL, /* cancellable */
					      error)) {
		g_prefix_error (error, "failed to send request: ");
		return FALSE;
	}
	if (actual_length != req->len) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "request not all sent, got %" G_GSIZE_FORMAT,
			     actual_length);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_solokey_device_packet_rx (FuSolokeyDevice *self, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	gsize actual_length = 0;
	g_autoptr(GByteArray) res = g_byte_array_new ();
	guint8 buf[SOLO_USB_HID_EP_SIZE] = { 0x00 };

	/* return anything */
	if (g_getenv ("FWUPD_SOLOKEY_EMULATE") != NULL)
		return g_steal_pointer (&res);

	/* read reply */
	if (!g_usb_device_interrupt_transfer (usb_device,
					      SOLO_USB_HID_EP_IN,
					      buf, sizeof(buf),
					      &actual_length,
					      SOLO_USB_TIMEOUT,
					      NULL, /* cancellable */
					      error)) {
		g_prefix_error (error, "failed to get reply: ");
		return NULL;
	}
	if (g_getenv ("FWUPD_SOLOKEY_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "RES", buf, actual_length);

	/* copy back optional buf */
	g_byte_array_append (res, buf, actual_length);
	return g_steal_pointer (&res);
}

static GByteArray *
fu_solokey_device_packet (FuSolokeyDevice *self, guint8 cmd,
			  GByteArray *payload, GError **error)
{
	guint8 buf_cid[4] = { 0x00 };
	guint8 buf_len[2] = { 0x00 };
	guint8 cmd_id = cmd | 0x80;
	guint16 first_chunk_size;
	g_autoptr(GByteArray) req = g_byte_array_new ();
	g_autoptr(GByteArray) res = NULL;

	/* U2F header */
	fu_common_write_uint32 (buf_cid, self->cid, G_LITTLE_ENDIAN);
	g_byte_array_append (req, buf_cid, sizeof(buf_cid));
	g_byte_array_append (req, &cmd_id, sizeof(cmd_id));

	/* no payload */
	if (payload == NULL) {
		if (!fu_solokey_device_packet_tx (self, req, error))
			return NULL;
		return fu_solokey_device_packet_rx (self, error);
	}

	/* sanity check */
	if (payload->len > 7609) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "payload impossible size, got %x",
			     payload->len);
		return NULL;
	}

	/* initialization packet */
	first_chunk_size = payload->len;
	if (payload->len > SOLO_USB_HID_EP_SIZE - 7)
		first_chunk_size = SOLO_USB_HID_EP_SIZE - 7;
	fu_common_write_uint16 (buf_len, payload->len, G_BIG_ENDIAN);
	g_byte_array_append (req, buf_len, sizeof(buf_len));
	g_byte_array_append (req, payload->data, first_chunk_size);
	if (!fu_solokey_device_packet_tx (self, req, error))
		return NULL;

	/* continuation packets */
	if (payload->len > first_chunk_size) {
		g_autoptr(GPtrArray) chunks = NULL;
		chunks = fu_chunk_array_new (payload->data + first_chunk_size,
					     payload->len - first_chunk_size,
					     0x00,	/* addr start */
					     0x00,	/* page_sz */
					     SOLO_USB_HID_EP_SIZE - 5);
		for (guint i = 0; i < chunks->len; i++) {
			FuChunk *chk = g_ptr_array_index (chunks, i);
			guint8 seq = chk->idx;
			g_autoptr(GByteArray) req2 = g_byte_array_new ();
			g_byte_array_append (req2, buf_cid, sizeof(buf_cid));
			g_byte_array_append (req2, &seq, sizeof(seq));
			g_byte_array_append (req2, chk->data, chk->data_sz);
			if (!fu_solokey_device_packet_tx (self, req2, error))
				return NULL;
		}
	}

	/* do not hit hardware */
	if (g_getenv ("FWUPD_SOLOKEY_EMULATE") != NULL)
		return g_byte_array_new ();

	/* success */
	res = fu_solokey_device_packet_rx (self, error);
	if (res == NULL)
		return NULL;
	if (res->len != SOLO_USB_HID_EP_SIZE) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "result invalid size, got %x", res->len);
		return NULL;
	}
	if (memcmp (res->data + 0, buf_cid, sizeof(buf_cid)) != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "CID invalid, got %x",
			     fu_common_read_uint32 (res->data + 0, G_BIG_ENDIAN));
		return NULL;
	}
	if (res->data[4] != cmd_id) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "command ID invalid, got %x", res->data[4]);
		return NULL;
	}
	return g_steal_pointer (&res);
}

static gboolean
fu_solokey_device_setup_cid (FuSolokeyDevice *self, GError **error)
{
	g_autoptr(GByteArray) nonce = g_byte_array_new ();
	g_autoptr(GByteArray) res = NULL;

	/* do not hit hardware */
	if (g_getenv ("FWUPD_SOLOKEY_EMULATE") != NULL)
		return TRUE;

	/* get a channel ID */
	for (guint i = 0; i < 8; i++)
		fu_byte_array_append_uint8 (nonce, g_random_int_range (0x00, 0xff));
	res = fu_solokey_device_packet (self, 0x06, nonce, error);
	if (res == NULL)
		return FALSE;
	if (fu_common_read_uint16 (res->data + 5, G_LITTLE_ENDIAN) < 0x11) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "INIT length invalid");
		return FALSE;
	}
	if (memcmp (res->data + 7, nonce->data, 8) != 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "nonce invalid");
		return FALSE;
	}
	self->cid = fu_common_read_uint32 (res->data + 7 + 8, G_LITTLE_ENDIAN);
	g_debug ("CID to use for device: %04x", self->cid);
	return TRUE;
}

static gboolean
fu_solokey_device_get_version_bl (FuSolokeyDevice *self, GError **error)
{
	g_autofree gchar *version = NULL;
	g_autoptr(GByteArray) req = g_byte_array_new ();
	g_autoptr(GByteArray) res = NULL;

	/* pass through data */
	fu_solokey_device_exchange (req, SOLO_BOOTLOADER_VERSION, 0x00, NULL);
	res = fu_solokey_device_packet (self, SOLO_BOOTLOADER_HID_CMD_BOOT, req, error);
	if (res == NULL)
		return FALSE;
	version = g_strdup_printf ("%u.%u.%u", res->data[8], res->data[9], res->data[10]);
	fu_device_set_version_bootloader (FU_DEVICE (self), version);
	return TRUE;
}

static gboolean
fu_solokey_device_setup (FuDevice *device, GError **error)
{
	FuSolokeyDevice *self = FU_SOLOKEY_DEVICE (device);

	/* get channel ID */
	if (!fu_solokey_device_setup_cid (self, error))
		return FALSE;

	/* verify version */
	if (fu_device_has_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		if (!fu_solokey_device_get_version_bl (self, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_solokey_device_verify (FuSolokeyDevice *self, GBytes *fw_sig, GError **error)
{
	g_autoptr(GByteArray) req = g_byte_array_new ();
	g_autoptr(GByteArray) res = NULL;
	g_autoptr(GByteArray) sig = g_byte_array_new ();

	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_VERIFY);
	g_byte_array_append (sig, g_bytes_get_data (fw_sig, NULL), g_bytes_get_size (fw_sig));
	fu_solokey_device_exchange (req, SOLO_BOOTLOADER_DONE, 0x00, sig);
	res = fu_solokey_device_packet (self, SOLO_BOOTLOADER_HID_CMD_BOOT, req, error);
	if (res == NULL)
		return FALSE;
	return TRUE;
}

static FuFirmware *
fu_solokey_device_prepare_firmware (FuDevice *device,
				    GBytes *fw,
				    FwupdInstallFlags flags,
				    GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_solokey_firmware_new ();
	fu_device_set_status (device, FWUPD_STATUS_DECOMPRESSING);
	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;
	return g_steal_pointer (&firmware);
}

static gboolean
fu_solokey_device_write_firmware (FuDevice *device,
				  FuFirmware *firmware,
				  FwupdInstallFlags flags,
				  GError **error)
{
	FuSolokeyDevice *self = FU_SOLOKEY_DEVICE (device);
	g_autoptr(FuFirmwareImage) img = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GBytes) fw_sig = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* get main image */
	img = fu_firmware_get_image_by_id (firmware, NULL, error);
	if (img == NULL)
		return FALSE;

	/* build packets */
	fw = fu_firmware_image_write (img, error);
	if (fw == NULL)
		return FALSE;
	chunks = fu_chunk_array_new_from_bytes (fw,
						fu_firmware_image_get_addr (img),
						0x00,	/* page_sz */
						2048);

	/* write each block */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		g_autoptr(GByteArray) buf = g_byte_array_new ();
		g_autoptr(GByteArray) req = g_byte_array_new ();
		g_autoptr(GByteArray) res = NULL;
		g_autoptr(GError) error_local = NULL;

		g_byte_array_append (buf, chk->data, chk->data_sz);
		fu_solokey_device_exchange (req, SOLO_BOOTLOADER_WRITE, chk->address, buf);
		res = fu_solokey_device_packet (self, SOLO_BOOTLOADER_HID_CMD_BOOT, req, &error_local);
		if (res == NULL) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_WRITE,
				     "failed to write: %s",
				     error_local->message);
			return FALSE;
		}

		/* update progress */
		fu_device_set_progress_full (device, (gsize) i, (gsize) chunks->len);
	}

	/* verify the signature and reboot back to runtime */
	fw_sig = fu_firmware_get_image_by_id_bytes (firmware,
						    FU_FIRMWARE_IMAGE_ID_SIGNATURE,
						    error);
	if (fw_sig == NULL)
		return FALSE;
	return fu_solokey_device_verify (self, fw_sig, error);
}

static void
fu_solokey_device_init (FuSolokeyDevice *self)
{
	self->cid = 0xffffffff;
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_remove_delay (FU_DEVICE (self), FU_DEVICE_REMOVE_DELAY_USER_REPLUG);
	fu_device_set_protocol (FU_DEVICE (self), "com.solokeys");
	fu_device_set_name (FU_DEVICE (self), "Solo Secure");
	fu_device_set_summary (FU_DEVICE (self), "An open source FIDO2 security key");
	fu_device_add_icon (FU_DEVICE (self), "applications-internet");
}

static void
fu_solokey_device_class_init (FuSolokeyDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUsbDeviceClass *klass_usb_device = FU_USB_DEVICE_CLASS (klass);
	klass_device->write_firmware = fu_solokey_device_write_firmware;
	klass_device->prepare_firmware = fu_solokey_device_prepare_firmware;
	klass_device->setup = fu_solokey_device_setup;
	klass_usb_device->open = fu_solokey_device_open;
	klass_usb_device->close = fu_solokey_device_close;
	klass_usb_device->probe = fu_solokey_device_probe;
}
