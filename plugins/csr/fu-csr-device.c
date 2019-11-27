/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-chunk.h"
#include "fu-csr-device.h"
#include "fu-ihex-firmware.h"

#include "dfu-common.h"

/**
 * FU_CSR_DEVICE_QUIRK_FLAG_REQUIRE_DELAY:
 *
 * Respect the write timeout value when performing actions. This is sometimes
 * set to a huge amount of time, and so is not used by default.
 *
 * Since: 1.0.3
 */
#define FU_CSR_DEVICE_FLAG_REQUIRE_DELAY	"require-delay"

typedef enum {
	FU_CSR_DEVICE_QUIRK_NONE		= 0,
	FU_CSR_DEVICE_QUIRK_REQUIRE_DELAY	= (1 << 0),
	FU_CSR_DEVICE_QUIRK_LAST
} FuCsrDeviceQuirks;

struct _FuCsrDevice
{
	FuUsbDevice		 parent_instance;
	FuCsrDeviceQuirks	 quirks;
	DfuState		 dfu_state;
	guint32			 dnload_timeout;
};

G_DEFINE_TYPE (FuCsrDevice, fu_csr_device, FU_TYPE_USB_DEVICE)

#define FU_CSR_REPORT_ID_COMMAND		0x01
#define FU_CSR_REPORT_ID_STATUS			0x02
#define FU_CSR_REPORT_ID_CONTROL		0x03

#define FU_CSR_COMMAND_HEADER_SIZE		6	/* bytes */
#define FU_CSR_COMMAND_UPGRADE			0x01

#define FU_CSR_STATUS_HEADER_SIZE		7

#define FU_CSR_CONTROL_HEADER_SIZE		2	/* bytes */
#define FU_CSR_CONTROL_CLEAR_STATUS		0x04
#define FU_CSR_CONTROL_RESET			0xff

/* maximum firmware packet, including the command header */
#define FU_CSR_PACKET_DATA_SIZE			1023	/* bytes */

#define FU_CSR_DEVICE_TIMEOUT			5000	/* ms */

static void
fu_csr_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuCsrDevice *self = FU_CSR_DEVICE (device);
	fu_common_string_append_kv (str, idt, "State", dfu_state_to_string (self->dfu_state));
	fu_common_string_append_ku (str, idt, "DownloadTimeout", self->dnload_timeout);
}

static gboolean
fu_csr_device_attach (FuDevice *device, GError **error)
{
	FuCsrDevice *self = FU_CSR_DEVICE (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	gsize sz = 0;
	guint8 buf[] = { FU_CSR_REPORT_ID_CONTROL, FU_CSR_CONTROL_RESET };

	if (g_getenv ("FWUPD_CSR_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "Reset", buf, sz);
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    FU_HID_REPORT_SET,				/* bRequest */
					    FU_HID_FEATURE | FU_CSR_REPORT_ID_CONTROL,	/* wValue */
					    0x0000,					/* wIndex */
					    buf, sizeof(buf), &sz,
					    FU_CSR_DEVICE_TIMEOUT, /* timeout */
					    NULL, error)) {
		g_prefix_error (error, "Failed to ClearStatus: ");
		return FALSE;
	}

	/* check packet */
	if (sz != FU_CSR_CONTROL_HEADER_SIZE) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Reset packet was %" G_GSIZE_FORMAT " expected %i",
			     sz, FU_CSR_CONTROL_HEADER_SIZE);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_csr_device_get_status (FuCsrDevice *self, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	gsize sz = 0;
	guint8 buf[64] = {0};

	/* hit hardware */
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    FU_HID_REPORT_GET,				/* bRequest */
					    FU_HID_FEATURE | FU_CSR_REPORT_ID_STATUS,	/* wValue */
					    0x0000,					/* wIndex */
					    buf, sizeof(buf), &sz,
					    FU_CSR_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "Failed to GetStatus: ");
		return FALSE;
	}
	if (g_getenv ("FWUPD_CSR_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "GetStatus", buf, sz);

	/* check packet */
	if (sz != FU_CSR_STATUS_HEADER_SIZE) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "GetStatus packet was %" G_GSIZE_FORMAT " expected %i",
			     sz, FU_CSR_STATUS_HEADER_SIZE);
		return FALSE;
	}
	if (buf[0] != FU_CSR_REPORT_ID_STATUS) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "GetStatus packet-id was %i expected %i",
			     buf[0], FU_CSR_REPORT_ID_STATUS);
		return FALSE;
	}

	self->dfu_state = buf[5];
	self->dnload_timeout = buf[2] + (((guint32) buf[3]) << 8) + (((guint32) buf[4]) << 16);
	g_debug ("timeout=%" G_GUINT32_FORMAT, self->dnload_timeout);
	g_debug ("state=%s", dfu_state_to_string (self->dfu_state));
	g_debug ("status=%s", dfu_status_to_string (buf[6]));
	return TRUE;
}


static gboolean
fu_csr_device_clear_status (FuCsrDevice *self, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	gsize sz = 0;
	guint8 buf[] = { FU_CSR_REPORT_ID_CONTROL,
			 FU_CSR_CONTROL_CLEAR_STATUS };

	/* only clear the status if the state is error */
	if (!fu_csr_device_get_status (self, error))
		return FALSE;
	if (self->dfu_state != DFU_STATE_DFU_ERROR)
		return TRUE;

	/* hit hardware */
	if (g_getenv ("FWUPD_CSR_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "ClearStatus", buf, sz);
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    FU_HID_REPORT_SET,				/* bRequest */
					    FU_HID_FEATURE | FU_CSR_REPORT_ID_CONTROL,	/* wValue */
					    0x0000,					/* wIndex */
					    buf, sizeof(buf), &sz,
					    FU_CSR_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "Failed to ClearStatus: ");
		return FALSE;
	}

	/* check packet */
	if (sz != FU_CSR_CONTROL_HEADER_SIZE) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "ClearStatus packet was %" G_GSIZE_FORMAT " expected %i",
			     sz, FU_CSR_CONTROL_HEADER_SIZE);
		return FALSE;
	}

	/* check the hardware again */
	return fu_csr_device_get_status (self, error);
}

static GBytes *
fu_csr_device_upload_chunk (FuCsrDevice *self, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	gsize sz = 0;
	guint16 data_sz;
	guint8 buf[64] = {0};

	/* hit hardware */
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    FU_HID_REPORT_GET,				/* bRequest */
					    FU_HID_FEATURE | FU_CSR_REPORT_ID_COMMAND,	/* wValue */
					    0x0000,					/* wIndex */
					    buf, sizeof(buf), &sz,
					    FU_CSR_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "Failed to ReadFirmware: ");
		return NULL;
	}
	if (g_getenv ("FWUPD_CSR_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "ReadFirmware", buf, sz);

	/* too small to parse */
	if (sz < FU_CSR_COMMAND_HEADER_SIZE) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "ReadFirmware packet too small, got %" G_GSIZE_FORMAT,
			     sz);
		return NULL;
	}

	/* check command byte */
	if (buf[0] != FU_CSR_REPORT_ID_COMMAND) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "wrong report ID %u", buf[0]);
		return NULL;
	}

	/* check the length */
	data_sz = fu_common_read_uint16 (&buf[1], G_LITTLE_ENDIAN);
	if (data_sz + FU_CSR_COMMAND_HEADER_SIZE != (guint16) sz) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "wrong data length %" G_GUINT16_FORMAT,
			     data_sz);
		return NULL;
	}

	/* return as bytes */
	return g_bytes_new (buf + FU_CSR_COMMAND_HEADER_SIZE,
			    sz - FU_CSR_COMMAND_HEADER_SIZE);
}

static FuFirmware *
fu_csr_device_upload (FuDevice *device, GError **error)
{
	FuCsrDevice *self = FU_CSR_DEVICE (device);
	g_autoptr(GPtrArray) chunks = NULL;
	g_autoptr(GBytes) fw = NULL;
	guint32 total_sz = 0;
	gsize done_sz = 0;

	/* notify UI */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_READ);

	chunks = g_ptr_array_new_with_free_func ((GDestroyNotify) g_bytes_unref);
	for (guint32 i = 0; i < 0x3ffffff; i++) {
		g_autoptr(GBytes) chunk = NULL;
		gsize chunk_sz;

		/* hit hardware */
		chunk = fu_csr_device_upload_chunk (self, error);
		if (chunk == NULL)
			return NULL;
		chunk_sz = g_bytes_get_size (chunk);

		/* get the total size using the CSR header */
		if (i == 0 && chunk_sz >= 10) {
			const guint8 *buf = g_bytes_get_data (chunk, NULL);
			if (memcmp (buf, "CSR-dfu", 7) == 0) {
				guint16 hdr_ver;
				guint16 hdr_len;
				hdr_ver = fu_common_read_uint16 (buf + 8, G_LITTLE_ENDIAN);
				if (hdr_ver != 0x03) {
					g_set_error (error,
						     FWUPD_ERROR,
						     FWUPD_ERROR_INTERNAL,
						     "CSR header version is "
						     "invalid %" G_GUINT16_FORMAT,
						     hdr_ver);
					return NULL;
				}
				total_sz = fu_common_read_uint32 (buf + 10, G_LITTLE_ENDIAN);
				if (total_sz == 0) {
					g_set_error (error,
						     FWUPD_ERROR,
						     FWUPD_ERROR_INTERNAL,
						     "CSR header data length "
						     "invalid %" G_GUINT32_FORMAT,
						     total_sz);
					return NULL;
				}
				hdr_len = fu_common_read_uint16 (buf + 14, G_LITTLE_ENDIAN);
				g_debug ("CSR header length: %" G_GUINT16_FORMAT, hdr_len);
			}
		}

		/* add to chunk array */
		done_sz += chunk_sz;
		g_ptr_array_add (chunks, g_steal_pointer (&chunk));
		fu_device_set_progress_full (device, done_sz, (gsize) total_sz);

		/* we're done */
		if (chunk_sz < 64 - FU_CSR_COMMAND_HEADER_SIZE)
			break;
	}

	/* notify UI */
	fw = dfu_utils_bytes_join_array (chunks);
	return fu_firmware_new_from_bytes (fw);
}

static gboolean
fu_csr_device_download_chunk (FuCsrDevice *self, guint16 idx, GBytes *chunk, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	const guint8 *chunk_data;
	gsize chunk_sz = 0;
	gsize write_sz = 0;
	guint8 buf[FU_CSR_PACKET_DATA_SIZE] = {0};

	/* too large? */
	chunk_data = g_bytes_get_data (chunk, &chunk_sz);
	if (chunk_sz + FU_CSR_COMMAND_HEADER_SIZE > FU_CSR_PACKET_DATA_SIZE) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "packet was too large: %" G_GSIZE_FORMAT, chunk_sz);
		return FALSE;
	}
	g_debug ("writing %" G_GSIZE_FORMAT " bytes of data", chunk_sz);

	/* create packet */
	buf[0] = FU_CSR_REPORT_ID_COMMAND;
	buf[1] = FU_CSR_COMMAND_UPGRADE;
	fu_common_write_uint16 (&buf[2], idx, G_LITTLE_ENDIAN);
	fu_common_write_uint16 (&buf[4], chunk_sz, G_LITTLE_ENDIAN);
	if (!fu_memcpy_safe (buf, sizeof(buf), FU_CSR_COMMAND_HEADER_SIZE,	/* dst */
			     chunk_data, chunk_sz, 0x0,				/* src */
			     chunk_sz, error))
		return FALSE;

	/* hit hardware */
	if (g_getenv ("FWUPD_CSR_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "Upgrade", buf, sizeof(buf));
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    FU_HID_REPORT_SET,				/* bRequest */
					    FU_HID_FEATURE | FU_CSR_REPORT_ID_COMMAND,	/* wValue */
					    0x0000,					/* wIndex */
					    buf,
					    sizeof(buf),
					    &write_sz,
					    FU_CSR_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "Failed to Upgrade: ");
		return FALSE;
	}

	/* check packet */
	if (write_sz != sizeof(buf)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Not all packet written for upgrade got "
			     "%" G_GSIZE_FORMAT " expected %" G_GSIZE_FORMAT,
			     write_sz, sizeof(buf));
		return FALSE;
	}

	/* wait for hardware */
	if (self->quirks & FU_CSR_DEVICE_QUIRK_REQUIRE_DELAY) {
		g_debug ("sleeping for %ums", self->dnload_timeout);
		g_usleep (self->dnload_timeout * 1000);
	}

	/* get status */
	if (!fu_csr_device_get_status (self, error))
		return FALSE;

	/* is still busy */
	if (self->dfu_state == DFU_STATE_DFU_DNBUSY) {
		g_debug ("busy, so sleeping a bit longer");
		g_usleep (G_USEC_PER_SEC);
		if (!fu_csr_device_get_status (self, error))
			return FALSE;
	}

	/* not correct */
	if (self->dfu_state != DFU_STATE_DFU_DNLOAD_IDLE &&
	    self->dfu_state != DFU_STATE_DFU_IDLE) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "device did not return to IDLE");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static FuFirmware *
fu_csr_device_prepare_firmware (FuDevice *device,
				GBytes *fw,
				FwupdInstallFlags flags,
				GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_ihex_firmware_new ();

	/* parse the file */
	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;
	if (g_getenv ("FWUPD_CSR_VERBOSE") != NULL) {
		g_autofree gchar *fw_str = NULL;
		fw_str = fu_firmware_to_string (firmware);
		g_debug ("%s", fw_str);
	}

	/* success */
	return g_steal_pointer (&firmware);
}

static gboolean
fu_csr_device_download (FuDevice *device,
			FuFirmware *firmware,
			FwupdInstallFlags flags,
			GError **error)
{
	FuCsrDevice *self = FU_CSR_DEVICE (device);
	guint16 idx;
	g_autoptr(GBytes) blob_empty = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* get default image */
	blob = fu_firmware_get_image_default_bytes (firmware, error);
	if (blob == NULL)
		return FALSE;

	/* notify UI */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);

	/* create chunks */
	chunks = fu_chunk_array_new_from_bytes (blob, 0x0, 0x0,
						FU_CSR_PACKET_DATA_SIZE - FU_CSR_COMMAND_HEADER_SIZE);

	/* send to hardware */
	for (idx = 0; idx < chunks->len; idx++) {
		FuChunk *chk = g_ptr_array_index (chunks, idx);
		g_autoptr(GBytes) blob_tmp = g_bytes_new_static (chk->data, chk->data_sz);

		/* send packet */
		if (!fu_csr_device_download_chunk (self, idx, blob_tmp, error))
			return FALSE;

		/* update progress */
		fu_device_set_progress_full (device,
					     (gsize) idx, (gsize) chunks->len);
	}

	/* all done */
	blob_empty = g_bytes_new (NULL, 0);
	return fu_csr_device_download_chunk (self, idx, blob_empty, error);
}

static gboolean
fu_csr_device_probe (FuUsbDevice *device, GError **error)
{
	FuCsrDevice *self = FU_CSR_DEVICE (device);

	/* devices have to be whitelisted */
	if (fu_device_has_custom_flag (FU_DEVICE (device),
				       FU_CSR_DEVICE_FLAG_REQUIRE_DELAY))
		self->quirks = FU_CSR_DEVICE_QUIRK_REQUIRE_DELAY;

	/* hardcoded */
	fu_device_add_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_UPDATABLE);

	/* success */
	return TRUE;
}

static gboolean
fu_csr_device_open (FuUsbDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);

	/* open device and clear status */
	if (!g_usb_device_claim_interface (usb_device, 0x00, /* HID */
					   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					   error)) {
		g_prefix_error (error, "failed to claim HID interface: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_csr_device_setup (FuDevice *device, GError **error)
{
	FuCsrDevice *self = FU_CSR_DEVICE (device);

	if (!fu_csr_device_clear_status (self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_csr_device_close (FuUsbDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);

	/* we're done here */
	if (!g_usb_device_release_interface (usb_device, 0x00, /* HID */
					     G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					     error)) {
		g_prefix_error (error, "failed to release interface: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_csr_device_init (FuCsrDevice *self)
{
	fu_device_set_protocol (FU_DEVICE (self), "com.qualcomm.dfu");
}

static void
fu_csr_device_class_init (FuCsrDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUsbDeviceClass *klass_usb_device = FU_USB_DEVICE_CLASS (klass);
	klass_device->to_string = fu_csr_device_to_string;
	klass_device->write_firmware = fu_csr_device_download;
	klass_device->read_firmware = fu_csr_device_upload;
	klass_device->prepare_firmware = fu_csr_device_prepare_firmware;
	klass_device->attach = fu_csr_device_attach;
	klass_device->setup = fu_csr_device_setup;
	klass_usb_device->open = fu_csr_device_open;
	klass_usb_device->close = fu_csr_device_close;
	klass_usb_device->probe = fu_csr_device_probe;
}
