/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>
#include <string.h>

#include "fu-dfu-csr-device.h"
#include "fu-dfu-common.h"

/**
 * FU_DFU_CSR_DEVICE_FLAG_REQUIRE_DELAY:
 *
 * Respect the write timeout value when performing actions. This is sometimes
 * set to a huge amount of time, and so is not used by default.
 *
 * Since: 1.0.3
 */
#define FU_DFU_CSR_DEVICE_FLAG_REQUIRE_DELAY		(1 << 0)

struct _FuDfuCsrDevice
{
	FuHidDevice		 parent_instance;
	FuDfuState		 dfu_state;
	guint32			 dnload_timeout;
};

G_DEFINE_TYPE (FuDfuCsrDevice, fu_dfu_csr_device, FU_TYPE_HID_DEVICE)

#define FU_DFU_CSR_REPORT_ID_COMMAND		0x01
#define FU_DFU_CSR_REPORT_ID_STATUS		0x02
#define FU_DFU_CSR_REPORT_ID_CONTROL		0x03

#define FU_DFU_CSR_COMMAND_HEADER_SIZE		6	/* bytes */
#define FU_DFU_CSR_COMMAND_UPGRADE		0x01

#define FU_DFU_CSR_STATUS_HEADER_SIZE		7

#define FU_DFU_CSR_CONTROL_HEADER_SIZE		2	/* bytes */
#define FU_DFU_CSR_CONTROL_CLEAR_STATUS		0x04
#define FU_DFU_CSR_CONTROL_RESET		0xff

/* maximum firmware packet, including the command header */
#define FU_DFU_CSR_PACKET_DATA_SIZE		1023	/* bytes */

#define FU_DFU_CSR_DEVICE_TIMEOUT		5000	/* ms */

static void
fu_dfu_csr_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuDfuCsrDevice *self = FU_DFU_CSR_DEVICE (device);
	fu_common_string_append_kv (str, idt, "State", fu_dfu_state_to_string (self->dfu_state));
	fu_common_string_append_ku (str, idt, "DownloadTimeout", self->dnload_timeout);
}

static gboolean
fu_dfu_csr_device_attach (FuDevice *device, GError **error)
{
	guint8 buf[] = { FU_DFU_CSR_REPORT_ID_CONTROL, FU_DFU_CSR_CONTROL_RESET };
	if (!fu_hid_device_set_report (FU_HID_DEVICE (device),
				       FU_DFU_CSR_REPORT_ID_CONTROL,
				       buf, sizeof(buf),
				       FU_DFU_CSR_DEVICE_TIMEOUT,
				       FU_HID_DEVICE_FLAG_IS_FEATURE,
				       error)) {
		g_prefix_error (error, "failed to attach: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_dfu_csr_device_get_status (FuDfuCsrDevice *self, GError **error)
{
	guint8 buf[64] = {0};

	/* hit hardware */
	if (!fu_hid_device_get_report (FU_HID_DEVICE (self),
				       FU_DFU_CSR_REPORT_ID_STATUS,
				       buf, sizeof(buf),
				       FU_DFU_CSR_DEVICE_TIMEOUT,
				       FU_HID_DEVICE_FLAG_ALLOW_TRUNC |
				       FU_HID_DEVICE_FLAG_IS_FEATURE,
				       error)) {
		g_prefix_error (error, "failed to GetStatus: ");
		return FALSE;
	}

	/* check packet */
	if (buf[0] != FU_DFU_CSR_REPORT_ID_STATUS) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "GetStatus packet-id was %i expected %i",
			     buf[0], FU_DFU_CSR_REPORT_ID_STATUS);
		return FALSE;
	}

	self->dfu_state = buf[5];
	self->dnload_timeout = buf[2] + (((guint32) buf[3]) << 8) + (((guint32) buf[4]) << 16);
	g_debug ("timeout=%" G_GUINT32_FORMAT, self->dnload_timeout);
	g_debug ("state=%s", fu_dfu_state_to_string (self->dfu_state));
	g_debug ("status=%s", fu_dfu_status_to_string (buf[6]));
	return TRUE;
}


static gboolean
fu_dfu_csr_device_clear_status (FuDfuCsrDevice *self, GError **error)
{
	guint8 buf[] = { FU_DFU_CSR_REPORT_ID_CONTROL,
			 FU_DFU_CSR_CONTROL_CLEAR_STATUS };

	/* only clear the status if the state is error */
	if (!fu_dfu_csr_device_get_status (self, error))
		return FALSE;
	if (self->dfu_state != FU_DFU_STATE_DFU_ERROR)
		return TRUE;

	/* hit hardware */
	if (!fu_hid_device_set_report (FU_HID_DEVICE (self),
				       FU_DFU_CSR_REPORT_ID_CONTROL,
				       buf, sizeof(buf),
				       FU_DFU_CSR_DEVICE_TIMEOUT,
				       FU_HID_DEVICE_FLAG_IS_FEATURE,
				       error)) {
		g_prefix_error (error, "failed to ClearStatus: ");
		return FALSE;
	}

	/* check the hardware again */
	return fu_dfu_csr_device_get_status (self, error);
}

static GBytes *
fu_dfu_csr_device_upload_chunk (FuDfuCsrDevice *self, GError **error)
{
	guint16 data_sz;
	guint8 buf[64] = {0};

	/* hit hardware */
	if (!fu_hid_device_get_report (FU_HID_DEVICE (self),
				       FU_DFU_CSR_REPORT_ID_COMMAND,
				       buf, sizeof(buf),
				       FU_DFU_CSR_DEVICE_TIMEOUT,
				       FU_HID_DEVICE_FLAG_ALLOW_TRUNC |
				       FU_HID_DEVICE_FLAG_IS_FEATURE,
				       error)) {
		g_prefix_error (error, "failed to ReadFirmware: ");
		return NULL;
	}

	/* check command byte */
	if (buf[0] != FU_DFU_CSR_REPORT_ID_COMMAND) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "wrong report ID %u", buf[0]);
		return NULL;
	}

	/* check the length */
	data_sz = fu_common_read_uint16 (&buf[1], G_LITTLE_ENDIAN);
	if (data_sz + FU_DFU_CSR_COMMAND_HEADER_SIZE != (guint16) sizeof(buf)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "wrong data length %" G_GUINT16_FORMAT,
			     data_sz);
		return NULL;
	}

	/* return as bytes */
	return g_bytes_new (buf + FU_DFU_CSR_COMMAND_HEADER_SIZE,
			    sizeof(buf) - FU_DFU_CSR_COMMAND_HEADER_SIZE);
}

static GBytes *
fu_dfu_csr_device_upload(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDfuCsrDevice *self = FU_DFU_CSR_DEVICE (device);
	g_autoptr(GPtrArray) chunks = NULL;
	guint32 total_sz = 0;
	gsize done_sz = 0;

	/* notify UI */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_READ);

	chunks = g_ptr_array_new_with_free_func ((GDestroyNotify) g_bytes_unref);
	for (guint32 i = 0; i < 0x3ffffff; i++) {
		g_autoptr(GBytes) chunk = NULL;
		gsize chunk_sz;

		/* hit hardware */
		chunk = fu_dfu_csr_device_upload_chunk (self, error);
		if (chunk == NULL)
			return NULL;
		chunk_sz = g_bytes_get_size (chunk);

		/* get the total size using the DFU_CSR header */
		if (i == 0 && chunk_sz >= 10) {
			const guint8 *buf = g_bytes_get_data (chunk, NULL);
			if (memcmp (buf, "DFU_CSR-dfu", 7) == 0) {
				guint16 hdr_ver;
				guint16 hdr_len;
				if (!fu_common_read_uint16_safe	(buf, chunk_sz, 8,
								 &hdr_ver, G_LITTLE_ENDIAN,
								 error))
					return NULL;
				if (hdr_ver != 0x03) {
					g_set_error (error,
						     FWUPD_ERROR,
						     FWUPD_ERROR_INTERNAL,
						     "DFU_CSR header version is "
						     "invalid %" G_GUINT16_FORMAT,
						     hdr_ver);
					return NULL;
				}
				if (!fu_common_read_uint32_safe	(buf, chunk_sz, 10,
								 &total_sz, G_LITTLE_ENDIAN,
								 error))
					return NULL;
				if (total_sz == 0) {
					g_set_error (error,
						     FWUPD_ERROR,
						     FWUPD_ERROR_INTERNAL,
						     "DFU_CSR header data length "
						     "invalid %" G_GUINT32_FORMAT,
						     total_sz);
					return NULL;
				}
				if (!fu_common_read_uint16_safe	(buf, chunk_sz, 14,
								 &hdr_len, G_LITTLE_ENDIAN,
								 error))
					return NULL;
				g_debug ("DFU_CSR header length: %" G_GUINT16_FORMAT, hdr_len);
			}
		}

		/* add to chunk array */
		done_sz += chunk_sz;
		g_ptr_array_add (chunks, g_steal_pointer (&chunk));
		fu_progress_set_percentage_full(progress, done_sz, (gsize)total_sz);

		/* we're done */
		if (chunk_sz < 64 - FU_DFU_CSR_COMMAND_HEADER_SIZE)
			break;
	}

	/* notify UI */
	return fu_dfu_utils_bytes_join_array (chunks);
}

static gboolean
fu_dfu_csr_device_download_chunk (FuDfuCsrDevice *self, guint16 idx, GBytes *chunk, GError **error)
{
	const guint8 *chunk_data;
	gsize chunk_sz = 0;
	guint8 buf[FU_DFU_CSR_PACKET_DATA_SIZE] = {0};

	/* too large? */
	chunk_data = g_bytes_get_data (chunk, &chunk_sz);
	if (chunk_sz + FU_DFU_CSR_COMMAND_HEADER_SIZE > FU_DFU_CSR_PACKET_DATA_SIZE) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "packet was too large: %" G_GSIZE_FORMAT, chunk_sz);
		return FALSE;
	}
	g_debug ("writing %" G_GSIZE_FORMAT " bytes of data", chunk_sz);

	/* create packet */
	buf[0] = FU_DFU_CSR_REPORT_ID_COMMAND;
	buf[1] = FU_DFU_CSR_COMMAND_UPGRADE;
	fu_common_write_uint16 (&buf[2], idx, G_LITTLE_ENDIAN);
	fu_common_write_uint16 (&buf[4], chunk_sz, G_LITTLE_ENDIAN);
	if (!fu_memcpy_safe (buf, sizeof(buf), FU_DFU_CSR_COMMAND_HEADER_SIZE,	/* dst */
			     chunk_data, chunk_sz, 0x0,				/* src */
			     chunk_sz, error))
		return FALSE;

	/* hit hardware */
	if (!fu_hid_device_set_report (FU_HID_DEVICE (self),
				       FU_DFU_CSR_REPORT_ID_COMMAND,
				       buf, sizeof(buf),
				       FU_DFU_CSR_DEVICE_TIMEOUT,
				       FU_HID_DEVICE_FLAG_IS_FEATURE,
				       error)) {
		g_prefix_error (error, "failed to Upgrade: ");
		return FALSE;
	}

	/* wait for hardware */
	if (fu_device_has_private_flag (FU_DEVICE (self),
					FU_DFU_CSR_DEVICE_FLAG_REQUIRE_DELAY)) {
		g_debug ("sleeping for %ums", self->dnload_timeout);
		g_usleep (self->dnload_timeout * 1000);
	}

	/* get status */
	if (!fu_dfu_csr_device_get_status (self, error))
		return FALSE;

	/* is still busy */
	if (self->dfu_state == FU_DFU_STATE_DFU_DNBUSY) {
		g_debug ("busy, so sleeping a bit longer");
		g_usleep (G_USEC_PER_SEC);
		if (!fu_dfu_csr_device_get_status (self, error))
			return FALSE;
	}

	/* not correct */
	if (self->dfu_state != FU_DFU_STATE_DFU_DNLOAD_IDLE &&
	    self->dfu_state != FU_DFU_STATE_DFU_IDLE) {
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
fu_dfu_csr_device_prepare_firmware (FuDevice *device,
				    GBytes *fw,
				    FwupdInstallFlags flags,
				    GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_dfu_firmware_new ();

	/* parse the file */
	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;
	if (g_getenv ("FWUPD_DFU_CSR_VERBOSE") != NULL) {
		g_autofree gchar *fw_str = NULL;
		fw_str = fu_firmware_to_string (firmware);
		g_debug ("%s", fw_str);
	}

	/* success */
	return g_steal_pointer (&firmware);
}

static gboolean
fu_dfu_csr_device_download(FuDevice *device,
			   FuFirmware *firmware,
			   FuProgress *progress,
			   FwupdInstallFlags flags,
			   GError **error)
{
	FuDfuCsrDevice *self = FU_DFU_CSR_DEVICE (device);
	guint16 idx;
	g_autoptr(GBytes) blob_empty = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* get default image */
	blob = fu_firmware_get_bytes (firmware, error);
	if (blob == NULL)
		return FALSE;

	/* notify UI */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);

	/* create chunks */
	chunks = fu_chunk_array_new_from_bytes (blob, 0x0, 0x0,
						FU_DFU_CSR_PACKET_DATA_SIZE - FU_DFU_CSR_COMMAND_HEADER_SIZE);

	/* send to hardware */
	for (idx = 0; idx < chunks->len; idx++) {
		FuChunk *chk = g_ptr_array_index (chunks, idx);
		g_autoptr(GBytes) blob_tmp = fu_chunk_get_bytes (chk);

		/* send packet */
		if (!fu_dfu_csr_device_download_chunk (self, idx, blob_tmp, error))
			return FALSE;

		/* update progress */
		fu_progress_set_percentage_full(progress, (gsize)idx, (gsize)chunks->len);
	}

	/* all done */
	blob_empty = g_bytes_new (NULL, 0);
	return fu_dfu_csr_device_download_chunk (self, idx, blob_empty, error);
}

static gboolean
fu_dfu_csr_device_probe (FuDevice *device, GError **error)
{
	/* FuUsbDevice->probe */
	if (!FU_DEVICE_CLASS (fu_dfu_csr_device_parent_class)->probe (device, error))
		return FALSE;

	/* hardcoded */
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);

	/* success */
	return TRUE;
}

static gboolean
fu_dfu_csr_device_setup (FuDevice *device, GError **error)
{
	FuDfuCsrDevice *self = FU_DFU_CSR_DEVICE (device);

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS (fu_dfu_csr_device_parent_class)->setup (device, error))
		return FALSE;

	if (!fu_dfu_csr_device_clear_status (self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_dfu_csr_device_init (FuDfuCsrDevice *self)
{
	fu_device_add_protocol (FU_DEVICE (self), "com.qualcomm.dfu");
	fu_device_add_internal_flag (FU_DEVICE (self), FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID);
	fu_device_register_private_flag (FU_DEVICE (self),
					FU_DFU_CSR_DEVICE_FLAG_REQUIRE_DELAY,
					"require-delay");
}

static void
fu_dfu_csr_device_class_init (FuDfuCsrDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->to_string = fu_dfu_csr_device_to_string;
	klass_device->write_firmware = fu_dfu_csr_device_download;
	klass_device->dump_firmware = fu_dfu_csr_device_upload;
	klass_device->prepare_firmware = fu_dfu_csr_device_prepare_firmware;
	klass_device->attach = fu_dfu_csr_device_attach;
	klass_device->setup = fu_dfu_csr_device_setup;
	klass_device->probe = fu_dfu_csr_device_probe;
}
